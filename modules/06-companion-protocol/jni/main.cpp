#include <android/log.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// --- Arming, reused from Labs 3/4/5 ---------------------------------------
//
// Same file, same contract, same failure-closed behaviour as
// 03-armed-once/jni/main.cpp, 04-plt-hook/jni/main.cpp, and
// 05-main-thread/jni/main.cpp: one line of plain text in target.txt, read
// through Api::getModuleDir(), matched exactly (not as a package-name
// prefix) against args->nice_name. See 03-armed-once/README.md for the
// full reasoning; it is not repeated here.
static constexpr const char *kConfigFile = "target.txt";
static constexpr size_t kConfigMax = 255;

static void readTarget(Api *api, char *out, size_t outSize) {
    out[0] = '\0';
    int dirFd = api->getModuleDir();
    if (dirFd < 0) {
        LOGW("getModuleDir failed; module will not arm");
        return;
    }
    int fd = openat(dirFd, kConfigFile, O_RDONLY);
    close(dirFd);
    if (fd < 0) {
        LOGW("could not open %s in module dir; module will not arm", kConfigFile);
        return;
    }
    ssize_t n = read(fd, out, outSize - 1);
    close(fd);
    if (n <= 0) {
        LOGW("%s is empty or unreadable; module will not arm", kConfigFile);
        out[0] = '\0';
        return;
    }
    if ((size_t) n == outSize - 1) {
        LOGW("%s is longer than %zu bytes and was truncated; module will not arm",
             kConfigFile, outSize - 1);
        out[0] = '\0';
        return;
    }
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
}

// --- The wire protocol -----------------------------------------------------
//
// This is the actual subject of Lab 6: everything above is the arming
// plumbing this module inherits so the exchange below only happens for the
// one process we chose. What follows is shared, byte-for-byte, between the
// client side (running as the app, no privilege) and the companion side
// (running as root, in a separate daemon process) - it is compiled into
// both `main.cpp` builds because there is only one `main.cpp`, and it must
// describe the same bytes on both ends or the exchange desyncs.
//
// Every message - request or response - starts with a fixed 12-byte header:
//
//   offset 0: uint32  version   protocol version, checked before anything
//             else is trusted. This module starts at version 1 on purpose:
//             a protocol that adds a version field "later, once it turns
//             out to matter" already shipped every client and companion
//             that assumed there would never be a second version.
//   offset 4: uint32  opcode    what this message is / is asking for.
//   offset 8: uint32  length    number of payload bytes that follow this
//             header, 0 if the message carries no payload.
//
// followed by exactly `length` bytes of payload, and nothing else - no
// terminator, no delimiter. The length prefix is what makes this framing
// work over a stream socket: a single write() on one side is not
// guaranteed to arrive as a single read() on the other (the kernel is free
// to split it across TCP-like stream semantics even on AF_UNIX
// SOCK_STREAM), so both sides read the header first, learn exactly how
// many more bytes to expect, and then read exactly that many - never
// "read once and hope it was everything."
//
// The header fields are encoded with htonl()/ntohl() rather than copied in
// as a raw C struct. Both processes here happen to run the same compiled
// code on the same CPU, so raw struct copying would work today - but nothing
// about REGISTER_ZYGISK_COMPANION's contract promises the client and the
// companion share a struct layout (a future version of this module built
// for a 32-bit ABI target talking over the header's documented "ABI aware"
// companion connection is exactly the case where an assumed layout would
// quietly break), and encoding explicitly costs three function calls. Do it
// properly from message one, same as the version field.
static constexpr uint32_t kProtocolVersion = 1;

// The only request opcode a client may send. Anything else reaching the
// companion is by definition either a bug or the negative-control probe
// below, and both are handled the same way: rejected, not crashed on.
static constexpr uint32_t kOpReadSecretReq = 1;
// Response opcodes, sent only by the companion.
static constexpr uint32_t kOpReadSecretResp = 2;
static constexpr uint32_t kOpError = 3;

// The hard cap on any payload this protocol will ever carry, in either
// direction. This is what stands between a client-supplied `length` field
// and an unbounded allocation or an unbounded read: the companion checks
// `length` against this constant *before* it reads a single payload byte,
// let alone allocates anything sized by that field. 4 KiB is generous for
// a short secret string or a short error message and nothing this protocol
// sends is expected to be anywhere near it.
static constexpr uint32_t kMaxPayload = 4096;

static constexpr size_t kHeaderSize = 12;

struct MsgHeader {
    uint32_t version;
    uint32_t opcode;
    uint32_t length;
};

static void encodeHeader(const MsgHeader &h, uint8_t out[kHeaderSize]) {
    uint32_t v = htonl(h.version), o = htonl(h.opcode), l = htonl(h.length);
    memcpy(out + 0, &v, 4);
    memcpy(out + 4, &o, 4);
    memcpy(out + 8, &l, 4);
}

static void decodeHeader(const uint8_t in[kHeaderSize], MsgHeader *h) {
    uint32_t v, o, l;
    memcpy(&v, in + 0, 4);
    memcpy(&o, in + 4, 4);
    memcpy(&l, in + 8, 4);
    h->version = ntohl(v);
    h->opcode = ntohl(o);
    h->length = ntohl(l);
}

// --- read-exactly / write-exactly -----------------------------------------
//
// A plain read()/write() pair is the classic first bug in hand-rolled
// socket code: it works in testing, on localhost, with small messages, and
// then fails the first time a message is split across two reads (or a
// write is split across two syscalls) under real scheduling. These two
// helpers loop until either the full byte count has moved or the peer is
// definitively gone, and treat EINTR as "try again," not as an error -
// a signal landing mid-syscall is not the peer hanging up.
//
// A timeout (SO_RCVTIMEO / SO_SNDTIMEO, set once on the client's socket
// below) turns a stalled read()/write() into an EAGAIN/EWOULDBLOCK error
// here rather than a silent hang - readExact/writeExact treat that exactly
// like any other fatal error: stop looping and report failure to the
// caller. Neither helper enforces a timeout itself; it only has to not
// swallow the one the socket option already applied.
static bool readExact(int fd, void *buf, size_t n) {
    auto *p = static_cast<uint8_t *>(buf);
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r > 0) {
            got += (size_t) r;
            continue;
        }
        if (r == 0) {
            LOGW("readExact: peer closed the connection after %zu/%zu bytes", got, n);
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOGW("readExact: timed out after %zu/%zu bytes", got, n);
            return false;
        }
        LOGW("readExact: read() failed: %s", strerror(errno));
        return false;
    }
    return true;
}

static bool writeExact(int fd, const void *buf, size_t n) {
    const auto *p = static_cast<const uint8_t *>(buf);
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, p + sent, n - sent);
        if (w > 0) {
            sent += (size_t) w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            LOGW("writeExact: timed out after %zu/%zu bytes", sent, n);
            return false;
        }
        LOGW("writeExact: write() failed (returned %zd): %s", w, strerror(errno));
        return false;
    }
    return true;
}

// Send one message: header, then payload if any. A single logical
// operation from the caller's point of view, so callers cannot forget the
// payload half of a message that has one.
static bool sendMsg(int fd, uint32_t opcode, const void *payload, uint32_t len) {
    MsgHeader h{kProtocolVersion, opcode, len};
    uint8_t hdr[kHeaderSize];
    encodeHeader(h, hdr);
    if (!writeExact(fd, hdr, kHeaderSize)) return false;
    if (len > 0 && !writeExact(fd, payload, len)) return false;
    return true;
}

// --- The root-only resource --------------------------------------------
//
// A file the companion can read and the app process genuinely cannot,
// because it lives inside /data/adb, which root-management implementations
// (Magisk, KernelSU) create with mode 0700 owned by root:root. No app UID
// has search (x) permission on that directory, so even *stat-ing* a path
// inside it fails with EACCES before file-level permissions matter at all.
// The companion process is root (it is the daemon REGISTER_ZYGISK_COMPANION
// spawns), so open() on this same path succeeds for it unconditionally.
// See README.md "Configuration" for how to create this file before running
// the module - it does not ship one, on purpose: shipping a secret inside
// the module package would make it not-root-only again.
static constexpr const char *kSecretPath = "/data/adb/zygisklab_secret.txt";

// --- The companion (root side) --------------------------------------------
//
// Runs in a separate daemon process, launched and kept alive by the root
// provider (Magisk/KernelSU's zygiskd), never in the app process. Per the
// header's own comment on REGISTER_ZYGISK_COMPANION: "the function can run
// concurrently on multiple threads" - one invocation per connected client,
// potentially in parallel. This handler keeps no mutable state outside its
// own stack (kSecretPath is read fresh on every request, not cached into a
// global), so there is nothing here for concurrent invocations to race on.
//
// One connection can carry more than one request: the loop below keeps
// serving messages on the same socket until the client closes it or a
// framing error makes the stream unrecoverable. This module's own client
// uses that to send its negative-control probes and its real request
// back-to-back over one connection, so the trace can show the companion
// surviving the bad input and then answering the next, valid, request -
// proof it is the same live process, not a fresh one that never saw the
// malformed message.
static void respondError(int fd, const char *reason) {
    size_t n = strnlen(reason, kMaxPayload);
    LOGW("companion: rejecting request: %s", reason);
    sendMsg(fd, kOpError, reason, (uint32_t) n);
}

static void handleReadSecret(int fd) {
    int sfd = open(kSecretPath, O_RDONLY);
    if (sfd < 0) {
        char reason[128];
        snprintf(reason, sizeof(reason), "open(%s) failed: %s", kSecretPath, strerror(errno));
        LOGE("companion: %s (see README.md Configuration to create this file)", reason);
        respondError(fd, reason);
        return;
    }
    uint8_t buf[kMaxPayload];
    ssize_t n = read(sfd, buf, sizeof(buf));
    close(sfd);
    if (n < 0) {
        char reason[128];
        snprintf(reason, sizeof(reason), "read(%s) failed: %s", kSecretPath, strerror(errno));
        LOGE("companion: %s", reason);
        respondError(fd, reason);
        return;
    }
    LOGI("companion: read %zd byte(s) from %s (root-only path) for the connected client",
         n, kSecretPath);
    if (!sendMsg(fd, kOpReadSecretResp, buf, (uint32_t) n)) {
        LOGW("companion: failed to send secret response");
    }
}

static void companion_handler(int fd) {
    LOGI("companion: client connected");
    for (;;) {
        uint8_t hdrBuf[kHeaderSize];
        if (!readExact(fd, hdrBuf, kHeaderSize)) {
            LOGI("companion: connection ended");
            return;
        }
        MsgHeader h;
        decodeHeader(hdrBuf, &h);
        LOGI("companion: request header: version=%u opcode=%u length=%u",
             h.version, h.opcode, h.length);

        // Every field below is untrusted input from a process the
        // companion does not have to trust just because it is talking to
        // it: validate version, opcode, and length - in that order, and
        // fully - before doing anything the request asked for. The length
        // check in particular happens *before* any read() of payload
        // bytes and before any buffer sized by it is touched: `buf` below
        // is a fixed kMaxPayload stack array, never a client-length-sized
        // allocation.
        if (h.version != kProtocolVersion) {
            char reason[64];
            snprintf(reason, sizeof(reason), "unsupported version %u (want %u)",
                      h.version, kProtocolVersion);
            respondError(fd, reason);
            continue;
        }
        if (h.opcode != kOpReadSecretReq) {
            char reason[64];
            snprintf(reason, sizeof(reason), "unknown opcode %u", h.opcode);
            respondError(fd, reason);
            continue;
        }
        if (h.length > kMaxPayload) {
            char reason[96];
            snprintf(reason, sizeof(reason),
                      "length %u exceeds cap %u; rejected before reading payload",
                      h.length, kMaxPayload);
            // Deliberately does NOT attempt to read h.length bytes here -
            // that is precisely the bug this check exists to avoid. The
            // client, for this exact test, does not send that many bytes
            // either (see sendMalformedOversizedLength() below), so both
            // sides of the stream stay in sync: the companion never tries
            // to consume payload bytes that were never sent.
            respondError(fd, reason);
            continue;
        }

        uint8_t payload[kMaxPayload];
        if (h.length > 0 && !readExact(fd, payload, h.length)) {
            LOGW("companion: failed reading %u-byte payload; dropping connection", h.length);
            return;
        }

        // Only one request opcode exists past validation, but the switch
        // is written as a switch (not an if) so a second opcode added
        // later has an obvious place to go, next to this comment, instead
        // of turning into another top-level early return.
        switch (h.opcode) {
            case kOpReadSecretReq:
                handleReadSecret(fd);
                break;
            default:
                // Unreachable: filtered above. Kept as a defined case
                // rather than omitted, so -Wswitch would catch a new
                // opcode added to the enum-like constants above without a
                // matching handler here.
                respondError(fd, "internal: unhandled opcode");
                break;
        }
    }
}

REGISTER_ZYGISK_COMPANION(companion_handler)

// --- The client (app-process side) ----------------------------------------

// The negative control: two deliberately malformed requests, sent before
// the real one, over the same connection. Neither should be answered with
// the secret, and neither should end the connection - the companion must
// reject both cleanly and still be ready for the valid request that
// follows on the same socket. If the companion ever dies handling either
// of these, the *absence* of the final, valid response is how a reader
// notices - there is no separate crash log to look for, because a crashed
// companion just stops answering.
static void sendMalformedBadOpcode(int fd) {
    LOGI("client: sending negative control #1: unknown opcode");
    uint8_t hdr[kHeaderSize];
    encodeHeader({kProtocolVersion, 0xBAD0BAD0u, 0}, hdr);
    if (!writeExact(fd, hdr, kHeaderSize)) {
        LOGW("client: negative control #1: failed to send");
        return;
    }
    MsgHeader resp{};
    uint8_t respHdr[kHeaderSize];
    if (!readExact(fd, respHdr, kHeaderSize)) {
        LOGW("client: negative control #1: no response (companion may have died)");
        return;
    }
    decodeHeader(respHdr, &resp);
    // Validate the version before trusting anything else in the header. The
    // companion validates ours; we owe it the same courtesy, and a response
    // from a version we do not understand is not a response we can read.
    if (resp.version != kProtocolVersion) {
        LOGW("client: negative control #1: response version %u, expected %u - not parsing further",
             resp.version, kProtocolVersion);
        return;
    }
    uint8_t reason[kMaxPayload + 1];
    uint32_t n = resp.length > kMaxPayload ? kMaxPayload : resp.length;
    // Check the read. Ignoring it would leave `reason` holding whatever was
    // on the stack and then log it as though the companion had sent it -
    // inventing evidence in the one place this lab exists to produce it.
    if (n > 0 && !readExact(fd, reason, n)) {
        LOGW("client: negative control #1: short read on the error reason");
        return;
    }
    reason[n] = '\0';
    LOGI("client: negative control #1 result: opcode=%u (%s) reason=\"%s\" - companion alive",
         resp.opcode, resp.opcode == kOpError ? "rejected" : "UNEXPECTED", reason);
}

static void sendMalformedOversizedLength(int fd) {
    LOGI("client: sending negative control #2: length exceeds cap, no payload actually sent");
    uint8_t hdr[kHeaderSize];
    // A valid opcode, but a length that blows past kMaxPayload by a wide
    // margin. This is the request that would drive an unbounded
    // allocation or an unbounded read on a companion that trusted its
    // input - and specifically does NOT send that many payload bytes
    // afterward, because the companion is expected to reject it from the
    // header alone, before ever calling read() for a payload.
    encodeHeader({kProtocolVersion, kOpReadSecretReq, 10u * 1024u * 1024u}, hdr);
    if (!writeExact(fd, hdr, kHeaderSize)) {
        LOGW("client: negative control #2: failed to send");
        return;
    }
    MsgHeader resp{};
    uint8_t respHdr[kHeaderSize];
    if (!readExact(fd, respHdr, kHeaderSize)) {
        LOGW("client: negative control #2: no response (companion may have died)");
        return;
    }
    decodeHeader(respHdr, &resp);
    // Validate the version before trusting anything else in the header. The
    // companion validates ours; we owe it the same courtesy, and a response
    // from a version we do not understand is not a response we can read.
    if (resp.version != kProtocolVersion) {
        LOGW("client: negative control #2: response version %u, expected %u - not parsing further",
             resp.version, kProtocolVersion);
        return;
    }
    uint8_t reason[kMaxPayload + 1];
    uint32_t n = resp.length > kMaxPayload ? kMaxPayload : resp.length;
    // Check the read. Ignoring it would leave `reason` holding whatever was
    // on the stack and then log it as though the companion had sent it -
    // inventing evidence in the one place this lab exists to produce it.
    if (n > 0 && !readExact(fd, reason, n)) {
        LOGW("client: negative control #2: short read on the error reason");
        return;
    }
    reason[n] = '\0';
    LOGI("client: negative control #2 result: opcode=%u (%s) reason=\"%s\" - companion alive",
         resp.opcode, resp.opcode == kOpError ? "rejected" : "UNEXPECTED", reason);
}

// The real exchange: ask the companion for the file this process cannot
// open itself. Logs both the app's own failed attempt and the companion's
// answer, so the privilege asymmetry is visible in one trace rather than
// asserted in a comment.
static void runExchange(Api *api) {
    // Step 1: try it ourselves, as the app, first. This is expected to
    // fail - that failure IS the lesson - so it is logged at INFO, not
    // treated as an error path.
    int direct = open(kSecretPath, O_RDONLY);
    if (direct >= 0) {
        LOGW("client: unexpectedly able to open %s directly as the app (errno-less "
             "success) - this device's /data/adb permissions differ from what this "
             "lab assumes; see README.md", kSecretPath);
        close(direct);
    } else {
        LOGI("client: direct open(%s) failed as expected: %s (app has no root)",
             kSecretPath, strerror(errno));
    }

    // Step 2: ask the companion instead. connectCompanion() only works
    // from pre[XXX]Specialize, per the header - which is exactly where
    // this call sits.
    int fd = api->connectCompanion();
    if (fd < 0) {
        LOGW("client: connectCompanion() failed; is a companion registered and running?");
        return;
    }

    // A bounded timeout on both directions. If the companion never
    // answers - hung, killed, or simply never scheduled - this socket call
    // fails after the timeout instead of blocking preAppSpecialize (and so
    // the app's own launch) forever. 2 seconds is generous for a few
    // small local-socket round trips and short enough that a genuinely
    // stuck companion does not read as a stuck app.
    struct timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sendMalformedBadOpcode(fd);
    sendMalformedOversizedLength(fd);

    LOGI("client: sending the real request: read the root-only secret");
    if (!sendMsg(fd, kOpReadSecretReq, nullptr, 0)) {
        LOGW("client: failed to send the real request");
        close(fd);
        return;
    }
    MsgHeader resp{};
    uint8_t respHdr[kHeaderSize];
    if (!readExact(fd, respHdr, kHeaderSize)) {
        LOGW("client: timed out or lost the connection waiting for the real response - "
             "the companion may be hung or gone; continuing app startup regardless");
        close(fd);
        return;
    }
    decodeHeader(respHdr, &resp);
    // Same version-first rule as the negative controls. The companion checks
    // the version we send; a response carrying a version we do not understand
    // is one we must not parse, however plausible its other fields look.
    if (resp.version != kProtocolVersion) {
        LOGW("client: response version %u, expected %u - not parsing further",
             resp.version, kProtocolVersion);
        return;
    }
    uint8_t payload[kMaxPayload + 1];
    uint32_t n = resp.length > kMaxPayload ? kMaxPayload : resp.length;
    if (n > 0 && !readExact(fd, payload, n)) {
        LOGW("client: timed out reading the real response payload");
        close(fd);
        return;
    }
    payload[n] = '\0';
    if (resp.opcode == kOpReadSecretResp) {
        LOGI("client: companion returned %u byte(s) this process could not read itself: \"%s\"",
             n, payload);
    } else {
        LOGW("client: companion rejected the real request too: opcode=%u reason=\"%s\" "
             "(see README.md Configuration - the secret file probably does not exist yet)",
             resp.opcode, payload);
    }
    close(fd);
}

// Lab 6: the companion protocol. Prove the privilege asymmetry between the
// injected module (app privilege) and its companion (root), with a real
// request/response exchange and a rejected malformed request as the
// negative control.
class CompanionProtocol : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);

        char target[kConfigMax + 1];
        readTarget(api, target, sizeof(target));
        bool armed = target[0] != '\0' && strcmp(name, target) == 0;

        if (!armed) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            LOGI("preAppSpecialize: pid=%d nice_name=%s not armed (target=%s), no companion "
                 "exchange attempted", getpid(), name, target[0] ? target : "(unset)");
            env->ReleaseStringUTFChars(args->nice_name, name);
            return;
        }

        LOGI("preAppSpecialize: pid=%d proc=%s ARMED, starting companion exchange", getpid(), name);
        // connectCompanion() only works in pre[XXX]Specialize (SELinux
        // restricts it afterward - see the header), so the whole exchange
        // happens here, synchronously, before this call returns. It is
        // bounded by the socket timeouts set inside runExchange(), so it
        // cannot hang the app the way an unbounded read could.
        runExchange(api);

        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("postAppSpecialize: pid=%d nice_name=%s getuid=%d - companion exchange (if any) "
             "already finished in preAppSpecialize", getpid(), name, getuid());
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(CompanionProtocol)
