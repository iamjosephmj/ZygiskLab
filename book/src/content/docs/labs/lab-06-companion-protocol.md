---
title: "Lab 6: Designing the companion protocol"
description: "A request/response exchange where the app process asks the companion for something only root can read, with a rejected malformed request as the negative control."
sidebar:
  order: 6
status: unverified
---

**Chapter:** 18
**Module:** `modules/06-companion-protocol/`

## Deliverable

A request/response exchange where the app process asks the companion for something only root can read, with a rejected malformed request as the negative control.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Labs 1-3 complete: a working toolchain, `deploy.sh` as the only way you
  install a `.so`, and `target.txt` arming you already trust.
- Root over `adb`, with `su -M` available.
- `ANDROID_NDK_HOME` exported.
- **Two apps.** One you arm for, one you do not. The unarmed one is the control
  that shows the exchange never happens outside the target.

## Steps

### Part A - read, build, arm

1. **Read `jni/main.cpp` before you build it.** Everything above the
   `--- The wire protocol ---` banner is arming plumbing inherited from Labs
   3, 4 and 5, and is not the subject here. Below it, find six things and be
   able to say why each is where it is: `encodeHeader()` using `htonl` rather
   than writing a struct; the `kProtocolVersion` check running before the
   opcode check; the `h.length > kMaxPayload` test sitting *above* the
   `readExact()` that fetches the payload; `uint8_t payload[kMaxPayload]`
   being a fixed stack array; the `continue` after each rejection in
   `companion_handler()`; and the two `setsockopt()` calls in `runExchange()`
   happening immediately after `connectCompanion()` and before the first
   message. [Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/)
   argues each one.

2. **Build.**

   ```bash
   cd modules/06-companion-protocol
   export ANDROID_NDK_HOME=/path/to/ndk
   ./build.sh              # -> out/06-companion-protocol.zip
   ```

3. **Install the zip once, through your root manager.** This creates
   `/data/adb/modules/zygisklab_companion/` with the labels this provider
   assigns. `deploy.sh` refuses to run before that directory exists.

4. **Write `target.txt`.** Same contract as Labs 3, 4 and 5: one line, no
   trailing newline, matched exactly against `args->nice_name`.

   ```bash
   adb shell su -M -c "echo -n <target.package> > \
     /data/adb/modules/zygisklab_companion/target.txt"
   ```

   Missing, empty, unreadable or matching nothing means the module never arms
   and `connectCompanion()` is never called at all.

5. **Create the root-only secret.** The module does not ship one, on purpose:
   a secret inside the zip is a secret the app can read by unzipping the
   module, which would make it not root-only and quietly void the whole lab.

   ```bash
   adb shell su -M -c "echo -n \"root can read this, apps can't\" > \
     /data/adb/zygisklab_secret.txt && chmod 600 /data/adb/zygisklab_secret.txt"
   ```

6. **Check ownership and label on what you just created.** Chapter 7's deploy
   discipline applies to anything you put under `/data/adb`, not only to
   `.so` files. A file written through `su -M` inherits root ownership, but
   check rather than assume, and restore the label if your provider labels
   this directory:

   ```bash
   adb shell su -M -c "ls -lZ /data/adb/zygisklab_secret.txt"
   adb shell su -M -c "restorecon /data/adb/zygisklab_secret.txt"
   ```

   You are looking for `root root`, mode `600`, and a label consistent with
   its neighbours in `/data/adb`. A world-readable secret makes the negative
   result in step 9 impossible and every conclusion after it false.

7. **Deploy and reboot.**

   ```bash
   ./deploy.sh             # or ./deploy.sh <serial>
   adb reboot
   ```

   `deploy.sh` stages and `mv`s rather than `cp`ing over the live `.so`,
   restores and verifies the SELinux label, and hashes both sides. It does not
   reboot for you: zygote still has the old library mapped until it does. See
   [Chapter 7](/ZygiskLab/book/load/07-deploying-safely/).

### Part B - the exchange

8. **Watch the log, then cold-launch the target.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   ```bash
   adb shell am force-stop <target.package>
   adb shell monkey -p <target.package> -c android.intent.category.LAUNCHER 1
   ```

   Force-stop matters. An app resumed from recents is not forked again, so
   `preAppSpecialize` never runs, `connectCompanion()` is never called, and
   you will see nothing.

9. **Read the app's own failed attempt first.** Before anything involving the
   companion, expect the client to have tried the path itself and failed:

   ```text
   client: direct open(/data/adb/zygisklab_secret.txt) failed as expected: Permission denied (app has no root)
   ```

   This line is not preamble. It is half the deliverable, and it comes first
   so that the success further down means something. If instead you see the
   `unexpectedly able to open ... directly as the app` warning, stop: this
   device's `/data/adb` permissions are not what the lab assumes, and nothing
   below proves privilege asymmetry. Fix step 6 before continuing.

10. **Read the two negative controls.** Both are sent over the same connection,
    before the real request. Expect a rejection for each, with a reason string,
    and expect the client to note the companion is still alive:

    ```text
    client: sending negative control #1: unknown opcode
    client: negative control #1 result: opcode=3 (rejected) reason="unknown opcode 3134983888" - companion alive
    client: sending negative control #2: length exceeds cap, no payload actually sent
    client: negative control #2 result: opcode=3 (rejected) reason="length 10485760 exceeds cap 4096; rejected before reading payload" - companion alive
    ```

    `(rejected)` means the response opcode was `OP_ERROR` (`3`). If either line
    says `(UNEXPECTED)` instead, the companion answered something other than an
    error and validation is not doing what the code claims.

11. **Confirm the companion saw what the client sent.** The `companion:` lines
    come from a different OS process on the same logcat tag, and they are the
    only direct evidence of what arrived on the root side:

    ```text
    companion: request header: version=1 opcode=3134983888 length=0
    companion: request header: version=1 opcode=1 length=10485760
    ```

    The second one is the important one. The companion logged a 10 MiB length
    and then rejected it — and never read those bytes, which is why the stream
    stayed usable.

12. **Read the real exchange.** Same socket, immediately after the two
    rejections:

    ```text
    client: sending the real request: read the root-only secret
    companion: request header: version=1 opcode=1 length=0
    companion: read 27 byte(s) from /data/adb/zygisklab_secret.txt (root-only path) for the connected client
    client: companion returned 27 byte(s) this process could not read itself: "root can read this, apps can't"
    ```

    The byte count depends on what you wrote in step 5. What matters is that
    the content matches the file, and that this exchange arrived after the two
    rejections on the same connection.

13. **Note where in the lifecycle it all happened.** The `postAppSpecialize`
    line should report the exchange as already finished:

    ```text
    postAppSpecialize: pid=... nice_name=... getuid=... - companion exchange (if any) already finished in preAppSpecialize
    ```

    `connectCompanion()` only works from `pre[XXX]Specialize`, which is why the
    whole exchange is synchronous inside that callback. Chapters 17 and 20 have
    the reasoning and the failure modes.

14. **Confirm the app is undamaged and launched at normal speed.** The exchange
    sits on the app's launch path. A target that visibly stalls for about two
    seconds before appearing is the timeout firing — check the log for a
    `timed out` line rather than assuming the module is merely slow.

### Part C - the control

15. **Cold-launch an app you did not arm for**, with the same logcat running.

    ```bash
    adb shell am force-stop <control.package>
    adb shell monkey -p <control.package> -c android.intent.category.LAUNCHER 1
    ```

16. **Expect exactly one line for that process:**

    ```text
    preAppSpecialize: pid=... nice_name=<control> not armed (target=<target>), no companion exchange attempted
    ```

    No `client:` lines, no `companion:` lines, and no `postAppSpecialize` line —
    `setOption(DLCLOSE_MODULE_LIBRARY)` unloads the library first, as in Labs
    3, 4 and 5.

17. **Prove it by pid, not by eye.**

    ```bash
    adb logcat -d -s ZygiskLab | grep 'client:' | grep -o 'pid=[0-9]*' | sort -u
    ```

    The `client:` lines carry no pid of their own, so instead correlate: every
    `ARMED, starting companion exchange` line in the buffer must carry the
    target's pid, and there must be exactly one of them per target launch.
    Any second armed pid means the arming check leaked, not the protocol.

### Part D - break it on purpose

18. **Delete the secret and relaunch.** This exercises the companion's I/O
    failure path, which is a different branch from the two negative controls.

    ```bash
    adb shell su -M -c "rm /data/adb/zygisklab_secret.txt"
    ```

    Expect a clean `OP_ERROR` carrying `open(...) failed: No such file or
    directory`, logged by the client as `opcode=3` with the reason — not a
    hang, not a crash, and not a companion that stops answering. Recreate the
    file afterwards.

19. **Optional, and the only way to test the timeout honestly:** there is no
    supported way to stall the companion from outside it, so if you want to
    see `SO_RCVTIMEO` fire you have to edit `handleReadSecret()` to sleep
    longer than two seconds, rebuild, and deploy. Expect the client to log
    `timed out or lost the connection waiting for the real response` and the
    app to start anyway, roughly two seconds late. Revert afterwards. Do not
    report the timeout as verified if you did not do this.

## Self-check

Two groups, and the split is the point of the lab. The first says bytes came
back. The second says those bytes could not have come from the app process, and
that the root process survived being handed rubbish.

**Did the exchange work?**

1. **Did you see `ARMED, starting companion exchange` for the target's pid?**
   Without it, nothing below applies — you are looking at an arming problem,
   not a protocol one.

2. **Did `connectCompanion()` succeed?** A `connectCompanion() failed; is a
   companion registered and running?` line means the socket was never opened
   and no part of the protocol ran. That is a provider-level failure; see
   [Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/).

3. **Did the client log a real response with `opcode=2` semantics** — the
   `companion returned N byte(s) this process could not read itself` line —
   with content matching the file you wrote in step 5? Matching content, not
   just a non-zero byte count.

4. **Did the app launch normally?** No visible stall, no `timed out` line.

**Did you prove privilege asymmetry?**

5. **Did the app process's own `open()` fail, in the same trace, on the same
   path?** This is the load-bearing line. Without it you have proved that a
   socket delivered some bytes, which is not the deliverable. Say out loud
   which errno it failed with.

6. **Is the secret file actually root-only on this device?** You checked
   ownership, mode and label in step 6. If it is `644`, or in a directory the
   app can traverse, the app could have read it and the whole result is
   circular.

7. **Did the file the companion read come from outside the module package?**
   If you shipped a secret in the zip instead of creating one under
   `/data/adb`, the app could obtain it by reading its own module files, and
   the demonstration is void.

**Did you prove the companion survived bad input?**

8. **Were both negative controls answered with `opcode=3 (rejected)`, each
   with the specific reason string for its own check?** Two rejections with
   the same reason means one check is doing both jobs and the other is
   untested.

9. **Did the oversized-length control get rejected from the header alone?**
   The companion's log line should show `length=10485760` followed by the
   rejection, with no payload read between them. A companion that tried to
   read 10 MiB that were never sent would instead hang until the client's
   two-second timeout.

10. **Did the real request succeed on the *same* connection, after both
    rejections?** This is the survival proof and there is no substitute for
    it. A crashed companion produces no error line anywhere — it is a separate
    process with its own fate — so the only signal is the presence of the
    answer that came afterwards.

11. **Did the control app produce exactly one line, the `not armed` one?** Any
    `client:` or `companion:` line attributable to the control launch means
    the exchange is not scoped to your target.

**What you have not proved.** You have proved this protocol rejects the two
malformed messages it was written to reject, on this device, in this provider.
You have not proved the framing is robust: `readExact()`'s loop exists because
stream sockets are permitted to split messages, not because you observed a split
here, and a lab where every message happened to arrive in one `read()` tests the
loop's correctness not at all. You have not proved the two-second timeout is
well chosen unless you ran step 19. And you have not proved anything about a
client with a different ABI from its companion — the `htonl` encoding is there
for a case this lab cannot reach on a single arm64 device. Do not report this
lab as "the protocol is correct". Report it as "the validation rejects what it
was written to reject, and the privilege asymmetry is real on this rig".
