# 06 — Companion Protocol

**Lab 6.** Designing the companion protocol. The injected module runs as
the app, with the app's own privileges - it cannot read anything root-only,
no matter how the earlier labs' hooks are used. The *companion* is a
separate process, spawned and kept alive by the root provider, that stays
root the whole time. This module proves the asymmetry directly: the armed
process tries to read a root-only file itself (and fails), then asks the
companion for the same file over a small request/response protocol (and
succeeds), and finally proves the companion survives bad input by sending
it a deliberately malformed request first and getting a clean rejection
back, not a dropped connection.

## What it proves

For the configured target, once `/data/adb/zygisklab_secret.txt` exists
(see Configuration):

```
preAppSpecialize: pid=5678 proc=com.android.chrome ARMED, starting companion exchange
client: direct open(/data/adb/zygisklab_secret.txt) failed as expected: Permission denied (app has no root)
companion: client connected
client: sending negative control #1: unknown opcode
companion: request header: version=1 opcode=3134983888 length=0
client: negative control #1 result: opcode=3 (rejected) reason="unknown opcode 3134983888" - companion alive
client: sending negative control #2: length exceeds cap, no payload actually sent
companion: request header: version=1 opcode=1 length=10485760
client: negative control #2 result: opcode=3 (rejected) reason="length 10485760 exceeds cap 4096; rejected before reading payload" - companion alive
client: sending the real request: read the root-only secret
companion: request header: version=1 opcode=1 length=0
companion: read 27 byte(s) from /data/adb/zygisklab_secret.txt (root-only path) for the connected client
client: companion returned 27 byte(s) this process could not read itself: "root can read this, apps can't"
postAppSpecialize: pid=5678 nice_name=com.android.chrome getuid=10123 - companion exchange (if any) already finished in preAppSpecialize
```

For every other launch:

```
preAppSpecialize: pid=1234 nice_name=com.other.app not armed (target=com.android.chrome), no companion exchange attempted
```

One line, nothing after it - `setOption(DLCLOSE_MODULE_LIBRARY)` unloads
the module before `postAppSpecialize`, same as Labs 3, 4, and 5, so
`connectCompanion()` is never even called on this path.

If `/data/adb/zygisklab_secret.txt` does not exist yet, the real request
still gets a clean answer - `companion: open(...) failed: No such file or
directory` followed by an `kOpError` response the client logs as `opcode=3
(UNEXPECTED)` with a reason pointing back at this README - not a hang and
not a crash. Create the file (see below) to see the success path.

Every log line above is prefixed `client:` or `companion:` so a reader can
tell which process produced it, even though both come from the same
`ZygiskLab` logcat tag and the companion is a separate OS process from the
app it served.

## Configuration

Same arming file, same contract as Labs 3, 4, and 5:

```bash
adb shell su -M -c "echo -n com.android.chrome > /data/adb/modules/zygisklab_companion/target.txt"
```

If `target.txt` is missing, empty, unreadable, or matches nothing, the
module never arms and never calls `connectCompanion()`. See
`03-armed-once/README.md` for the exact-match policy and why it isn't a
package-name prefix check.

The root-only secret is **not** shipped in the module package - a secret
baked into the zip would just be a file the app could read by unzipping
the module itself, which defeats the entire lesson. Create it once, as
root, before testing the success path:

```bash
adb shell su -M -c "echo -n \"root can read this, apps can't\" > /data/adb/zygisklab_secret.txt && chmod 600 /data/adb/zygisklab_secret.txt"
```

## The problem this lab is about

Every earlier lab's code - the hook in `04-plt-hook`, the main-thread action
in `05-main-thread` - runs with the target app's own privilege from
`postAppSpecialize` onward, and even `preAppSpecialize` (zygote's identity,
not the app's, but still not a device owner's) cannot read a file that only
`root` can open. `Api::connectCompanion()` is the header's answer: a
Unix-domain socket to a separate daemon process, registered with
`REGISTER_ZYGISK_COMPANION`, that the root provider keeps running as root
for the whole time the module is active - genuinely root, not "the app with
extra steps." Everything this module does with that socket is the actual
subject of the lab; the arming above is inherited plumbing, not new
material - see `03-armed-once/README.md` for why it works the way it does.

## The wire format

Every message, in either direction, starts with a fixed 12-byte header:

| offset | field   | type     | meaning                                   |
|-------:|---------|----------|--------------------------------------------|
| 0      | version | uint32   | protocol version; `1` today                |
| 4      | opcode  | uint32   | what this message is / is requesting       |
| 8      | length  | uint32   | payload byte count that follows the header |

followed by exactly `length` payload bytes and nothing else - no
terminator. The header fields are encoded with `htonl()`/decoded with
`ntohl()` into a flat byte buffer rather than copied in as a raw C struct;
both processes here run the same compiled code on the same CPU today, so a
raw copy would work, but nothing in the header's contract promises that
(it explicitly documents the companion connection as "ABI aware" - i.e.
capable of talking to a differently-built peer), and encoding explicitly
costs three function calls. Do it properly from message one, not "once it
matters."

**Opcodes:**

- `1` `OP_READ_SECRET_REQ` - client to companion, no payload. "Give me the
  secret file."
- `2` `OP_READ_SECRET_RESP` - companion to client, payload = file contents.
  Success.
- `3` `OP_ERROR` - companion to client, payload = a short ASCII reason
  string. Sent for every rejection: bad version, unknown opcode, oversized
  length, or a real I/O failure reading the secret file. One opcode for
  "no" keeps the client's response handling in one place instead of one
  branch per failure mode.

**Size cap:** `kMaxPayload = 4096` bytes, enforced on the companion side
against the `length` field *before* any payload byte is read and before
any buffer sized by that field is touched - the companion's payload buffer
is a fixed 4096-byte stack array (`uint8_t payload[kMaxPayload]`), never an
allocation sized by what the client claims. A client that sends
`length = 10 MiB` gets rejected by the header check alone; the companion
never calls `read()` for that payload at all. That is exactly what negative
control #2 (below) exercises.

## Framing: read-exactly, write-exactly

A plain `write()`/`read()` pair is the classic first bug in hand-rolled
socket code - it happens to work in casual testing and then fails the
first time a message is split across two reads, because nothing in POSIX
promises a stream socket delivers one `write()` as one `read()`.
`readExact()`/`writeExact()` in `jni/main.cpp` loop until the requested
byte count has fully moved or the peer is unambiguously gone:

- `EINTR` is retried, not treated as failure - a signal landing mid-syscall
  is not the peer hanging up.
- A `0`-byte `read()` (peer closed) and any other `errno` are logged and
  reported as failure to the caller.
- `EAGAIN`/`EWOULDBLOCK` (see Timeouts below) is logged distinctly as a
  timeout and reported as failure - the loop does not spin against it.

Every send and every receive in this protocol, on both sides, goes through
these two functions. There is no direct `read()`/`write()` call left
anywhere near a socket in this file.

## Validation, on the companion side, in order

The companion is root; the caller is not. Every field in an incoming
header is treated as untrusted input and checked, in this order, before
anything the request asked for is done:

1. **`version == kProtocolVersion`** - reject unsupported versions.
2. **`opcode == OP_READ_SECRET_REQ`** - reject anything else; this is the
   only opcode a client is ever allowed to send.
3. **`length <= kMaxPayload`** - reject before reading a single payload
   byte, and before touching any buffer sized by `length`.

Only after all three pass does the companion `read()` the payload (into
the fixed `kMaxPayload` stack buffer) and act on the request. See
`companion_handler()` and `handleReadSecret()` in `jni/main.cpp`.

## The negative control

The client sends two deliberately malformed requests, over the *same*
connection, before the real one:

- **#1 - unknown opcode.** `version = 1`, `opcode = 0xBAD0BAD0`,
  `length = 0`. Fails validation step 2.
- **#2 - oversized length.** `version = 1`, `opcode = OP_READ_SECRET_REQ`
  (a valid opcode), `length = 10 MiB`, and - critically - the client does
  **not** actually send 10 MiB of payload afterward. Fails validation step
  3, and does so before the companion ever tries to `read()` that many
  bytes, which is what keeps both ends of the stream in sync afterward:
  the companion never consumed payload bytes the client never sent.

Both get back `OP_ERROR` with a specific reason string, logged by the
client. Neither ends the connection - `companion_handler()`'s loop
`continue`s after each rejection rather than returning - and the same
socket then carries the real `OP_READ_SECRET_REQ` and gets a real
`OP_READ_SECRET_RESP` back. That final, successful exchange *is* the proof
the companion survived: a companion that had died handling either
malformed message would simply never answer the request that follows it,
and there is no separate crash log to check for that - the absence of the
final response line is the signal, which is why the trace above prints it
explicitly.

## Timeouts on the client side

`connectCompanion()` only works from `pre[XXX]Specialize` (the header
restricts it via SELinux afterward), so the entire exchange - both
negative-control probes and the real request - happens synchronously
inside `preAppSpecialize`, before that call returns. If the companion never
answered, this would block the callback that gates the app's own launch,
which is the mistake Chapters 8 and 11 already warn against. `runExchange()`
sets `SO_RCVTIMEO`/`SO_SNDTIMEO` to two seconds on the connected socket
right after `connectCompanion()` returns, before any message is sent. A
stalled `read()`/`write()` then fails with `EAGAIN`/`EWOULDBLOCK` after two
seconds rather than hanging indefinitely, `readExact()`/`writeExact()`
report that as a normal failure, and the client logs it plainly (`"timed
out or lost the connection waiting for the real response"`) and returns -
app startup continues either way.

## What only a device can confirm

Everything below is asserted from reading the Zygisk API header and from
how AF_UNIX `SOCK_STREAM` sockets and Magisk/KernelSU's `/data/adb`
permissions are documented to behave, not from running this module:

- That `/data/adb` is actually `0700 root:root` on this book's reference
  rig specifically, under Zygisk Next 1.4.5 on KernelSU-Next 3.3.0, such
  that the app process's direct `open()` attempt fails with `EACCES`
  rather than something else (or, on a misconfigured device, unexpectedly
  succeeding - `runExchange()` logs that case loudly rather than assuming
  it can't happen).
- That the companion daemon Zygisk Next spawns for this module actually
  runs as UID 0 with no further restriction that would make `open()` on
  `/data/adb/zygisklab_secret.txt` fail even for it.
- That `connectCompanion()` succeeds on the first call in
  `preAppSpecialize` on that rig, rather than returning `-1` for some
  provider-specific reason not documented in the header.
- That two seconds is actually enough headroom for three small local
  round trips under real device scheduling load, rather than either
  wastefully long or occasionally too short.
- That a message genuinely arrives split across multiple `read()`s often
  enough on this transport for `readExact()`'s looping to matter in
  practice, as opposed to every message happening to land in one `read()`
  on this specific socket type and payload size - the loop is there
  because the framing must be correct regardless, not because splitting
  was observed.
- That none of this trips whatever integrity/attestation checks a real
  target app runs - this module was written for the same reference rig as
  the rest of this book, not audited against any specific app.

## Build

```bash
export ANDROID_NDK_HOME=/home/joseph/Android/Sdk/ndk/29.0.14206865
./build.sh              # -> out/06-companion-protocol.zip
```

## Install

Flash `out/06-companion-protocol.zip` in your root manager, set
`target.txt` and create `/data/adb/zygisklab_secret.txt` (see
Configuration above), and reboot. For subsequent iterations use
`./deploy.sh` — see `03-armed-once/README.md` / Chapter 7 for why it
pushes and `mv`s instead of `cp`ing over the live `.so`.

## Watch, and run the control

```bash
adb logcat -s ZygiskLab
```

Launch the configured target and confirm the full trace: the failed direct
`open()`, both negative controls being rejected, and the real exchange
succeeding. Then launch a **different** app you did not configure as the
target — the control. It should produce exactly one `preAppSpecialize`
line and one `not armed` line, and nothing else: no companion connection,
no negative controls, no real request. If the control ever shows a
`client:` or `companion:` line, the arming check is broken, not the
protocol - re-check `target.txt` before assuming anything about the
companion exchange leaked scope.

## Reference rig

Written for Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next
1.4.5.

**Not yet run on that rig.** This module compiles and packages cleanly, and
nothing more than that has been established. Every statement here about what
it does at runtime is reasoning from the API header and from how AF_UNIX
sockets and `/data/adb` permissions are documented to behave, not an
observation. Treat the expected output as a prediction to be tested. As
stated above, this specific module has not yet been run on that device —
see "What only a device can confirm".
