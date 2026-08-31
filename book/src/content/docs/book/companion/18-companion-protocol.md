---
title: "Designing the companion protocol"
description: "Lab 6: designing a length-prefixed, versioned companion protocol with timeouts and validation of every field."
sidebar:
  order: 2
status: unverified
---

[Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/) ended with a
connected socket and nothing else. `Api::connectCompanion()` returns a file
descriptor; `REGISTER_ZYGISK_COMPANION` hands your handler the other end of the
same connection. That is the entire contract. There is no message boundary, no
serialisation format, no request identifier, no version negotiation, no timeout,
and no schema. Zygisk gives you a byte pipe between an unprivileged process and a
root one, and stops.

That stopping point is the subject of this chapter. Everything below is design
work you have to do yourself, and almost all of it is work that appears
unnecessary the first time you write it, because a protocol with none of it will
pass your first test on your own device. It fails later, on someone else's
device, under load, or the first time you ship a second version. This chapter
builds the protocol that Lab 6's module actually implements —
`modules/06-companion-protocol/jni/main.cpp` — and argues each decision at the
point where the cheap version breaks.

One framing to carry through. The socket crosses a privilege boundary. The
process on the app side has the app's uid and runs inside a program you do not
control; the process on the root side is root. Those are not two halves of one
program that happen to be in different address spaces. They are a client and a
server, and the server is the one with power. Design accordingly.

## Framing: the bug you will not see in testing

Start with the naive version, because you have to know precisely why it is wrong:

```cpp
write(fd, request, sizeof(request));      // client
read(fd, buffer, sizeof(buffer));         // companion
```

This works. It will work on your rig, every time, for a fortnight, and the code
looks correct because it *is* correct on the evidence available.

It is wrong because nothing in POSIX says a stream socket delivers one `write()`
as one `read()`. `SOCK_STREAM` is a stream: the kernel is free to deliver the
bytes in any grouping it likes, and `read()` is documented to return *up to* the
requested count, not the requested count. Small messages on a quiet local socket
happen to arrive whole almost always, which is exactly what makes this a bad bug
— it is not reproducible on demand, it is not visible in code review, and when it
does happen the symptom is a desynchronised stream rather than a clean error. The
companion parses half a header as if it were a whole one, believes whatever
garbage the length field then contains, and behaves unpredictably from there. It
is a correctness bug that presents as flakiness.

There are two independent fixes and you need both.

The first is **length-prefixed framing**: every message announces how long it is
before it says anything else, so the receiver never has to guess where a message
ends. The module uses a fixed 12-byte header:

| offset | field | type | meaning |
|---:|---|---|---|
| 0 | version | `uint32` | protocol version |
| 4 | opcode | `uint32` | what this message is |
| 8 | length | `uint32` | payload bytes following the header |

followed by exactly `length` payload bytes, and nothing else — no terminator, no
delimiter, no trailing newline. Both sides read twelve bytes, learn how many more
to expect, then read exactly that many. There is never a moment where either side
has to infer a boundary.

The second fix is the helper that makes "read exactly twelve bytes" true rather
than aspirational.

## Read-exactly, write-exactly

A length prefix is useless if the code that reads the prefix can itself come up
short. `readExact` and `writeExact` loop until the full count has moved or the
peer is unambiguously gone:

```cpp
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
```

Four outcomes, deliberately distinguished. A short read is not an error and not a
special case — it is the normal case, and the loop simply continues. A zero
return means the peer closed: that is genuinely fatal, and the count in the log
line tells you how far the exchange got before it died. `EINTR` means a signal
landed during the syscall; the read did not fail, it was interrupted, and
retrying is the only correct response. Treating `EINTR` as failure is the second
classic bug in hand-rolled socket code and it is exactly as intermittent as the
first.

`EAGAIN`/`EWOULDBLOCK` is separated from the generic error case for one reason:
it is the timeout, and a reader debugging a stuck exchange needs to know that
"the companion did not answer in time" and "the companion sent us something
broken" are different diagnoses. Neither helper enforces a timeout itself. The
timeout is a socket option, set once, discussed below; the helpers only have to
avoid swallowing it.

`writeExact` mirrors this. Partial writes are rarer on a small local socket, but
"rarer" is not a property you can build on, and the loop costs nothing.

The discipline that follows is absolute: **there is no bare `read()` or `write()`
on a socket anywhere in the module.** Every message on both sides goes through
these two functions. A single unlooped call is enough to reintroduce the bug, and
it will be in the one path you did not think mattered.

Sending is wrapped once more, so a caller cannot send a header and forget the
payload that belongs with it:

```cpp
static bool sendMsg(int fd, uint32_t opcode, const void *payload, uint32_t len) {
    MsgHeader h{kProtocolVersion, opcode, len};
    uint8_t hdr[kHeaderSize];
    encodeHeader(h, hdr);
    if (!writeExact(fd, hdr, kHeaderSize)) return false;
    if (len > 0 && !writeExact(fd, payload, len)) return false;
    return true;
}
```

One logical message, one call. Header and payload cannot drift apart.

## Explicit bytes, not a struct

There is an obvious shortcut here, and the module deliberately does not take it:

```cpp
write(fd, &header, sizeof(header));   // not what the module does
```

Both processes in this lab run the same compiled library on the same CPU, so
copying the struct over the wire would work today. The module encodes each field
instead:

```cpp
static void encodeHeader(const MsgHeader &h, uint8_t out[kHeaderSize]) {
    uint32_t v = htonl(h.version), o = htonl(h.opcode), l = htonl(h.length);
    memcpy(out + 0, &v, 4);
    memcpy(out + 4, &o, 4);
    memcpy(out + 8, &l, 4);
}
```

The reason is that nothing in `REGISTER_ZYGISK_COMPANION`'s contract promises the
two ends share a struct layout. The header documents the companion connection as
ABI aware — that is, capable of connecting a client to a companion built for a
different ABI — and a 32-bit client talking to a 64-bit companion is precisely
the case where struct padding, alignment, and field size stop agreeing. The
failure mode is not a compile error. It is a header that parses into plausible
nonsense, and you debug it as a protocol bug for a day before suspecting the
layout.

## Versioning from message one

The version field is the cheapest thing in this protocol and the only one that
cannot be added later.

Consider a deployed protocol with no version field, whose header is opcode and
length. Where would a second version go? Every companion already in the field
reads the first four bytes as an opcode, and any value you put there is either
one it rejects or, worse, one it acts on. You cannot extend the header, because
the old parser reads a fixed count from a fixed offset. That leaves smuggling the
version inside a payload the old side never reads, burning an opcode as a
sentinel, or breaking every deployed pair at once. All three are worse than four
bytes.

The module states this plainly in `main.cpp`: a protocol that adds a version
field "later, once it turns out to matter" has already shipped every client and
companion that assumed there would never be a second version.

The same argument applies to opcode allocation, at lower stakes. The module has
three:

- `1` `OP_READ_SECRET_REQ` — client to companion, no payload.
- `2` `OP_READ_SECRET_RESP` — companion to client, payload is the file bytes.
- `3` `OP_ERROR` — companion to client, payload is a short reason string.

Two conventions worth copying. Request and response opcodes are distinct values
rather than the same number used in both directions, so a message is
self-describing regardless of who sent it, and a client that receives an echo of
its own request can say so instead of misreading it as success. And there is
exactly *one* failure opcode. Every rejection — bad version, unknown opcode,
oversized length, a real I/O error reading the file — comes back as `OP_ERROR`
with a human-readable reason string, which keeps the client's failure handling in
one branch instead of one branch per failure mode. The reason string is for the
log; the opcode is what the code switches on.

## Timeouts, and whose problem a hang is

`connectCompanion()` works only from `pre[XXX]Specialize` — the header says so,
and Chapter 17 covers why. That places the entire exchange inside
`preAppSpecialize`, synchronously, before the callback returns.

Now be concrete about what that means. `preAppSpecialize` runs on the app's own
launch path. The app is not on screen yet. Every millisecond you spend in that
callback is a millisecond of launch latency, and every *blocked* syscall in it is
a hang the user of the app experiences as the app failing to start. If the
companion is wedged, or was never spawned, or is busy behind another client, an
unbounded `read()` there does not degrade your module. It bricks the app.

Two lines prevent that, immediately after connecting:

```cpp
struct timeval tv{2, 0};
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
```

Both directions, before any message is sent. A stalled read or write now fails
with `EAGAIN`/`EWOULDBLOCK` after two seconds, `readExact`/`writeExact` report
that as an ordinary failure, and the client logs it and returns. App startup
continues either way. Two seconds is a judgement — generous for three small
round trips on a local socket, short enough that a genuinely stuck companion does
not read as a stuck app — and it is a judgement nobody has yet checked against
real scheduling load on the reference rig.

The general rule: **the injected side must be able to complete without the
companion.** Whatever your module does with the answer, there has to be a defined
behaviour for not getting one. Decide what that is deliberately, and log it, so
that "the companion did not answer" is a line in your trace and not a mystery
about why the app is slow.

## Not trusting your own client

This is the section that matters most, and it is the one that feels wrong,
because both sides ship in the same zip and you wrote both.

Write out where each side runs. The companion is root. The client runs inside an
app process — an app you did not write, whose code you do not control, into which
your library was injected alongside whatever else is in there. The client is a
piece of your code executing inside someone else's program. If that app is
compromised, or is a target that fights back, or your own client just has a bug
that puts a wrong number in a length field, the bytes arriving at the companion
are not the bytes you meant to send. The companion has no way to tell the
difference and should not try.

So: **validate at the privilege boundary, not before it.** Checks on the client
side are convenience. They document intent and catch mistakes early, and they are
worth having, but they are not security, because the attacker's client simply
does not run them. The only checks that count are the ones the root process
performs on bytes that have already crossed the socket.

The companion's loop validates in order, before doing anything the request asked
for:

```cpp
if (h.version != kProtocolVersion) { /* reject */ continue; }
if (h.opcode != kOpReadSecretReq)  { /* reject */ continue; }
if (h.length > kMaxPayload)        { /* reject */ continue; }

uint8_t payload[kMaxPayload];
if (h.length > 0 && !readExact(fd, payload, h.length)) { /* drop */ return; }
```

Version first: an unrecognised version means you do not know what the rest of the
header means, so nothing after it can be interpreted. Opcode second: reject
anything that is not the single opcode a client is permitted to send, rather than
accepting a set and hoping the handler copes. Allowlist, not denylist.

The length check is the one to internalise. `kMaxPayload` is 4096, and the check
happens *before* a single payload byte is read and before any buffer sized by
`length` is touched. The buffer is a fixed stack array:

```cpp
static constexpr uint32_t kMaxPayload = 4096;
```

Note what this is not. Not `malloc(h.length)`. Not a `std::vector` resized to
whatever the client claimed. A client-supplied length never sizes an allocation
and never drives a read, which closes the whole family of bugs where a hostile
number in a header becomes unbounded memory in a root process, or a read that
overruns a buffer the code believed was large enough.

The general form, worth stating separately from the example: **anything the
client sends is a claim, not a fact.** A length is a claim about how many bytes
follow. An opcode is a claim about what is wanted. A version is a claim about
what the sender believes. Every one of them gets checked against something the
companion decided at compile time before it influences anything the companion
does.

Two smaller points in the same spirit. When the companion rejects a message, it
`continue`s rather than returning — a bad request is not grounds to tear down the
connection, and closing on the first malformed byte would hand any buggy client a
denial of service against the exchange it needs. But when a payload *read* fails
mid-message, it does return: at that point the stream position is unknown, and
continuing to parse a stream you have lost your place in is worse than dropping
it.

## Concurrency, and the state you do not keep

The header's comment on `REGISTER_ZYGISK_COMPANION` warns that your handler "can
run concurrently on multiple threads" — one invocation per connected client,
potentially in parallel. That is a constraint on how you may hold state, and the
module answers it by holding none.

Every companion-side global is `constexpr`: the version, the opcodes, the payload
cap, the secret path. Everything else lives on the handler's own stack — the
header buffer, the payload buffer, the reason strings, the file descriptor. The
secret file is opened and read fresh on every request rather than cached into a
global. There is no mutable shared state, so there is nothing for concurrent
invocations to race on, and no lock to get wrong.

Adding a cache, a counter, or a connection registry to a companion takes on a
concurrency problem in a root process with no test harness. If you must, guard it
properly and assume every connection is independent —
[Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) sets out why
lifetime and connection count are assumptions rather than promises.

## Proving the validation, not asserting it

A rejection path that has never been exercised is not a rejection path. Lab 6's
module therefore sends two deliberately malformed requests over the same
connection, before the real one:

1. **Unknown opcode.** `version = 1`, `opcode = 0xBAD0BAD0`, `length = 0`. Fails
   the opcode check.
2. **Oversized length.** `version = 1`, a *valid* opcode, `length = 10 MiB` —
   and the client does not send those bytes. Fails the length check, from the
   header alone, before the companion attempts any payload read. That is what
   keeps the stream in sync: the companion never tries to consume bytes that were
   never sent, so the connection is still usable afterward.

Both get `OP_ERROR` back with a specific reason. Then the same socket carries the
real `OP_READ_SECRET_REQ`, and gets a real `OP_READ_SECRET_RESP`.

That third exchange is the actual evidence. A companion that had crashed handling
either malformed message would not announce it — a dead companion simply stops
answering, and there is no crash line in your logcat to look for, because it is a
different process with its own fate. The proof that the companion survived bad
input is that it answered the *next* request on the *same* connection. That is
why the negative controls come first and share a socket with the real request,
rather than being a separate test you run afterward and cannot correlate.

The other half of the lab's evidence is the same shape. Before asking the
companion for anything, the client tries the root-only path itself:

```cpp
int direct = open(kSecretPath, O_RDONLY);
```

and logs the expected `EACCES` at INFO, because that failure *is* the result.
Without it, "the companion returned some bytes" proves only that some bytes
arrived. With it, one trace shows the same path failing for the app and
succeeding for the companion, which is the asymmetry
[Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) is about,
demonstrated rather than asserted. The module logs loudly if that direct `open()`
unexpectedly *succeeds* — on a device whose `/data/adb` permissions differ from
what the lab assumes, the whole demonstration is void, and you want to know that
rather than read a false positive.

The resource itself is `/data/adb/zygisklab_secret.txt`, and it is deliberately
not shipped in the module package. A secret inside the zip is a secret the app
can read by unzipping the module, which would make it not root-only and quietly
invalidate the lab. You create it yourself, as root, with the deploy discipline
[Chapter 7](/ZygiskLab/book/load/07-deploying-safely/) sets out for anything you
put under `/data/adb`.

## A checklist for your own protocol

Nothing in this chapter has been run on the reference rig. It is design reasoning
plus a module that compiles; [Lab 6](/ZygiskLab/labs/lab-06-companion-protocol/)
is where it gets tested, and
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) catalogues the ways
the companion machinery underneath it can fail regardless of how good your
framing is.

What survives the move to a different protocol:

- Length-prefix every message. Never infer a boundary.
- Loop on every read and write. Retry `EINTR`. Distinguish timeout from error.
- Put a version field in the first message you ever send.
- Encode fields explicitly. Do not write structs to sockets.
- Set `SO_RCVTIMEO` and `SO_SNDTIMEO` before the first byte moves, and define
  what the injected side does when the answer never comes.
- Validate version, then opcode, then length, on the root side, before acting.
- Never let a client-supplied number size an allocation or drive a read.
- Reject bad messages without closing the connection; drop the connection only
  when you have lost your place in the stream.
- Keep the companion stateless, or be prepared to defend it against concurrent
  invocation.
- Exercise every rejection path, over the same connection as a request you
  expect to succeed.
