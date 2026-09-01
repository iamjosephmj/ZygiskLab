---
title: "Deploying without bricking zygote"
description: "Lab 2: why cp over a mapped .so corrupts zygote, the md5sum diagnostic, and the atomic mv-based safe deploy."
sidebar:
  order: 3
status: proven
---

The most expensive bug in this book is not in any module's source. It is in the
one-line command you use to put a rebuilt `.so` onto the device. Copy the new
library over the old path and you corrupt the zygote that is running right now,
and the symptom you get back is a segfault that points at your new code, in a
process you did not write, only for the apps your module is enabled in. Every
signal that failure sends is a lie. People lose days to it, rewriting perfectly
correct C++, because the crash arrives dressed as a bug in the thing they just
changed.

This chapter is short on new API and long on one mechanism. By the end you
should be unable to type `cp` at a module directory without flinching, and you
should have a deploy script that makes the mistake impossible rather than
merely discouraged.

## The hazard

Your module's `.so` lives at
`/data/adb/modules/<id>/zygisk/arm64-v8a.so`. Zygote loaded it at boot
(Chapter 6 covered how the loader found it) and it is still there — not as a
file that was read once, but as a **mapping**. `dlopen` maps the file's pages
into the process's address space, and the kernel backs those pages with the
file itself. Executable pages are demand-paged: the kernel does not necessarily
hold all of your `.text` in memory at once, and it is free to drop a clean page
and fetch it again from the file the next time it is executed. That backing
relationship persists for the life of the mapping.

Now consider what `cp src dst` actually does. It opens `dst` with `O_TRUNC`,
which sets its length to zero, and writes the new bytes. It does not create a
new file. It reuses the **same inode**, because that is what the path already
points at. So the bytes underneath a live mapping change while executing code
depends on them.

The consequences are unbounded and non-uniform. A page that zygote already had
resident may still hold the old code; a page it faults in after the write gets
whatever the new build put at that offset. Function boundaries in the new
`.so` will not line up with the old one — add a line to `onLoad` and every
subsequent symbol shifts. A relocation performed at load time against the old
layout now addresses the middle of a different function. And during the window
in which `dst` is truncated but not yet rewritten, the file is *shorter than
the mapping*, so faulting a page past the new end of file raises SIGBUS.

There is nothing here that "usually works". It is a process whose text segment
has been replaced by fragments of a different program.

:::danger
`cp`, `install`, `>` redirection, `dd of=`, `adb push` straight to the module
directory, and an editor's save-in-place are all the same operation for this
purpose: they write into an existing inode. All of them corrupt a running
zygote. The distinction that matters is not the tool, it is whether the path
gets a new inode.
:::

## The symptom, and why it points the wrong way

Here is what you observe on the rig after a `cp`-style deploy, without
rebooting.

The device stays up. The launcher is fine. Settings opens. Then you launch the
app your module is scoped to, and it dies instantly — SIGSEGV during app
specialization, with the fault surfacing inside the provider's library rather
than inside your own. Every subsequent launch of that app dies the same way.
Apps your module is not enabled for should keep working, because your code
returns before it touches the damaged pages.

That last sentence is reasoning, not a measurement. The captured instance below
used a module with no arming at all, so it says nothing about the unarmed case;
what was observed is that a corrupted mapping kills specialization, not that a
process your module ignores survives it. Verify the asymmetry yourself before
you rely on it as a diagnostic.

Read that asymmetry again, because it is the whole trap. The crash is
*selective*, and it selects exactly the set of apps you were working on. Three
false conclusions follow naturally, and all three are wrong:

**"I broke my module."** The obvious reading: the code you just changed crashes
in the process it runs in, and only there. So you revert the change, redeploy —
with `cp`, because that is what you have been doing — and it still crashes.
Now you are debugging a version you know was good, which is the point where an
afternoon becomes a day.

**"The app is detecting me."** The crash is app-specific and started when your
module started touching that app, and Part VI of this book is entirely about
apps that react to injection. This is a seductive story and it is completely
false. Nothing in the app is involved.

**"The provider is broken."** The faulting frames sit in the provider's
library, not in yours, so the natural move is to reinstall or downgrade Zygisk
Next. That changes nothing, because the provider is only the messenger: it is
the code that dlopens you, calls into you, and touches your mapping, so it is
where the damaged pages get executed from.

### A captured instance

This signature was reproduced deliberately on the reference rig — Pixel 6 Pro,
Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — by writing a second
build over the live `.so` with `cp` while zygote had it mapped. App spawns
started dying immediately, with `Zygote  : Process 8716 exited due to signal 11
(Segmentation fault)` in logcat and a tombstone for each. The tombstone header:

```text
Executable: /system/bin/app_process64
Cmdline: zygote64
pid: 8716, tid: 8716, name: main  >>> zygote64 <<<
uid: 0
esr: 0000000082000006 (Instruction Abort Exception 0x20)
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x000000000002f53c (read)
    x17 0000002b2b678fa8
    pc  000000000002f53c
```

Read what that header tells you. The dying process is a freshly forked
`zygote64` child, still `uid: 0` — it died during specialization, before it
became the app; the Java frames confirm it, running
`Zygote.specializeAppProcess` from `Zygote.childMain` from `Zygote.forkSimpleApps`.
The fault is an *Instruction Abort*, not a data access: the process jumped to
`0x2f53c` and there is no executable mapping there. That is what a stale
mapping into a rewritten inode looks like from the inside.

The native backtrace is the part that misleads:

```text
#00 pc 000000000002f53c  <unknown>
#01 pc 00000000000fb130  /data/adb/modules/zygisksu/lib64/libzygisk.so
#02 pc 00000000000fb5d0  /data/adb/modules/zygisksu/lib64/libzygisk.so
#03 pc 00000000000fad18  /data/adb/modules/zygisksu/lib64/libzygisk.so
#04 pc 0007fc98          /data/adb/modules/zygisksu/lib64/libzygisk.so
```

Frame #00 is an address in no mapping at all, and every named frame below it
belongs to the provider. The tombstone also reports
`Unreadable libraries: /data/adb/modules/zygisksu/lib64/libzygisk.so`. Nothing
in the trace points at the module that actually caused the crash. This is
exactly why the failure reads as a provider bug or as an app-side defence
rather than as your own deploy.

The mechanism behind the selectivity is simple once you see it. Your module's
callbacks run in every forked process, but a well-behaved module — the one
Chapter 8 teaches you to write — decides early that it is not interested and
returns. An unarmed app therefore executes a handful of your instructions and
stops, quite possibly never faulting a page that the write disturbed. An armed
app runs the rest: the hook installation, the string handling, the code paths
that live further into the library. That is where the corruption is. The
selection is not by app identity. It is by *how much of your text the process
executes* — which, on any device you are testing on, correlates almost exactly
with which apps you armed.

:::caution
The same reasoning applies to a crash that takes the whole device down. If your
module does work in `onLoad` or in every process, the corrupted mapping is
exercised by zygote itself and you get a boot loop instead of a per-app crash.
Same cause, louder symptom, and Chapter 3's recovery levers are what you need.
:::

## The one-step diagnostic

You cannot tell these two situations apart by staring at the crash, and you
should stop trying. There is a check that separates them in one command.

```bash
adb shell su -M -c 'md5sum /data/adb/modules/<id>/zygisk/arm64-v8a.so'
md5sum libs/arm64-v8a/libzygisklab.so
```

Compare the two hashes.

| Hashes | What it means | What to do |
|---|---|---|
| Differ | The deploy did not land — wrong path, wrong ABI file name, permission or label failure | Fix the deploy; your code is untested |
| Match | The file on disk is exactly what you built | The file is fine. The *mapping* is stale. Reboot before you conclude anything about your code |

On the rig, that second row is what came back. Immediately after the `cp`, the
on-disk hash was `fc02597d052fbe735731a292e510b9c7` — an exact match for the
freshly built local file — while app spawns were still crashing. A correct file
and a crashing device at the same time, told apart by one command.

That second row is the one worth internalising. A matching hash means the
question "is my code broken?" is not yet answerable, because the code on disk
is not the code the running zygote is executing. Reboot and re-test. If the
crash survives a reboot, you have a real bug and the tombstone means what it
appears to mean. If it does not, your deploy was the bug and your source was
never at fault.

Run this before any debugging session that starts with "it crashed after I
deployed". It costs two seconds and it is the difference between debugging your
module and debugging your imagination.

:::note
`md5sum` is used here as a cheap identity check against your own build
artifact, not as a security property. Nothing about this is adversarial — you
are asking "are these the same file", and any hash answers that.
:::

## The correct deploy

Three moves: stage, rename, reboot.

```bash
adb push libs/arm64-v8a/libzygisklab.so /data/local/tmp/arm64-v8a.so
adb shell su -M -c 'mv /data/local/tmp/arm64-v8a.so \
  /data/adb/modules/<id>/zygisk/arm64-v8a.so'
adb reboot
```

The middle line is the whole trick. `mv` within a filesystem is `rename(2)`,
and `rename` does not touch file contents at all — it rebinds a **name** to a
different inode. Afterwards the module path refers to the inode you pushed,
which is a file no process has ever mapped. The old inode is unlinked from the
directory but not destroyed: a mapping counts as a reference, so zygote's
mapping keeps the old inode alive and intact, backed by exactly the bytes it
was loaded from. Both the running process and the file on disk are correct.
They are just different files, which is precisely the property you wanted.

`rename` is also atomic with respect to other processes. There is no instant at
which the module path is missing, truncated, or half-written. A loader that
opens it either gets the old file or the new one, never a mixture — which
matters, because a provider you did not write may be reading that directory on
its own schedule.

`adb push` cannot write directly into `/data/adb/modules`, which is a mercy:
the shell user has no business there, and the restriction is what forces the
staging path. `/data/local/tmp` is the conventional world-writable staging
directory for exactly this.

The manual version above trusts three things that are easy to get wrong under
time pressure: that the destination directory is the one you think it is, that
the bytes arrived intact, and that the label survived. `deploy.sh` checks all
three instead of trusting them, and fails loudly the moment any one is false
rather than reporting success and leaving you to discover, on the rig, days
from now, that the module never ran.

The first is upstream of the `mv` itself. `deploy.sh` reads the module `id`
out of `module.prop` and derives the destination directory from it, then
confirms `[ -d "$DEST_DIR" ]` before it stages anything. Without that check, a
wrong `id` — a typo, or a zip you never actually flashed — means the
destination directory does not exist. `mv` does fail in that case: it will not
create missing directories, and it exits non-zero saying so. The problem is
where that failure happens. It happens on the far side of `adb shell`, whose
exit status does not reliably reach the script that invoked it, so `set -e`
never fires and the error scrolls past in a stream of output nobody reads
closely. The deploy "succeeds", the module simply never runs, and you are back
at the same outcome this whole chapter exists to prevent, arrived at from an
entirely different direction than a stale mapping.

There is one case where that check refuses and nothing is wrong. Straight after
a fresh manager install, before the first reboot, `/data/adb/modules/<id>/`
holds only an `update` marker — on KernelSU-Next 3.3.0 the installer stages the
extracted module in `/data/adb/modules_update/<id>/` and only swaps it into
place at boot, as
[Chapter 3](/ZygiskLab/book/foundations/03-rig-and-toolchain/) describes. So
`[ -d "$DEST_DIR" ]` is false, `deploy.sh` stops, and it is right to: there is
no live module directory to deploy into yet. Reboot first, then deploy. Recognise this one by the timing — you have just installed
and not yet rebooted — rather than going looking for a typo in your `id`.

That distinction is worth holding onto, because it recurs whenever you drive a
device from a host script: a remote command that fails loudly is still a silent
failure if nothing on your side is listening. Checking the precondition locally
turns it back into an error you cannot miss.

The second is the hash check this chapter teaches you to run by hand, above.
Printing two hashes for a human to compare is not a check — nobody compares
them at 2am, as the script's own comment puts it. `deploy.sh` captures both
and fails on a mismatch, so the diagnostic you just learned to run manually
after a crash also runs automatically at deploy time, before you have spent a
test cycle on a build that never landed.

The third — pinning the SELinux label to a known value and verifying it took,
rather than running a relabel command and assuming — is worth its own
explanation, both because the obvious tools produce the wrong answer and because
this book previously got the reason for the check wrong. The SELinux section
below covers what the rig actually showed.

### Why the reboot is not superstition

The rename gave you a correct file and a correct running process. It did not
give you the *new* code, and nothing on the device will pick it up on its own.
Zygote reads the module directory once, during its own startup, and dlopens
what it finds. There is no watcher, no reload, no signal you can send. The
mapping in the running zygote is a mapping of the old inode and it stays that
way until that process exits. Every app forked from that zygote inherits the
copy-on-write image of a zygote that never saw your new build.

So "install then reboot" is not a ritual borrowed from Windows. It is the only
mechanism that runs the loader again. If you deploy and then test without
rebooting, a *correct* deploy will simply show you the old behaviour — which is
its own species of wasted afternoon, though a much cheaper one than the crash.

`adb reboot` is enough. There is no supported way to restart zygote alone that
is meaningfully less disruptive than a reboot, and attempts to kill it by hand
tend to produce a soft reboot anyway with more uncertainty about what state you
left behind.

## Permissions: why plain `su -c` is not enough

Note the `-M` in every root command above. Without it, writing into
`/data/adb/modules` is denied — even though you are root, and even though the
mode bits would allow it.

Root grants you privilege. It does not grant you a particular *view* of the
filesystem. A mount namespace is a per-process table of what is mounted where,
and Linux lets different processes hold different tables: two processes can
resolve the same path to two different files, and each is correct in its own
namespace. Root modules exploit this heavily. Rather than modifying `/system`
on disk — which would break verified boot — a module's `system/` directory is
bind-mounted over the real one inside a namespace that only some processes see.
That is the entire trick behind systemless root.

The consequence is that "the filesystem" is not a single thing you can be root
over. When KernelSU spawns a root shell, it hands you a namespace that is
already the modified view, and within that view the module directory may be
covered, read-only, or otherwise not the raw storage you wanted to write to.
`su -M` — mount-master — asks instead for the **global** namespace: the
unmodified view, where `/data/adb/modules` is the real directory holding real
files. Magisk exposes the same idea under the same flag.

The rule for the rest of this book: **any command that modifies module storage
runs under `su -M`.** Commands that merely inspect the device's running state
generally do not need it. Part IV returns to namespaces when your module's code
needs to reason about which filesystem view it is standing in, and Part VI
returns to them again, because the namespace difference between an injected
process and a clean one is itself something an app can measure.

## SELinux labels: a claim this book got wrong

This section previously told you that a mislabelled `.so` would be silently
refused by the loader — that the file would sit in the module directory,
correctly named, hash-identical to your build, and never load. Lab 2 Part A was
run on the reference rig to check that, and it is not what happens. The
correction is below, because a book that only reports its confirmations is not
worth the rig time.

**What was tested.** The module's `.so` was left carrying
`u:object_r:adb_data_file:s0` — the label a staged file picks up, not the label
the provider's own modules carry — and the device was rebooted. The module
loaded and ran normally: 69 `ZygiskLab` lines across app launches, including
`onLoad`, `preAppSpecialize` and `postAppSpecialize`. On this rig, with this
provider, a `.so` labelled `adb_data_file` is opened and executed by the loader
without complaint.

That is one device, one provider, one version — Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and it does not prove that no label can
ever cause a refusal. It does disprove the book's own claim, which was stated
generally, and a general claim falls to a single counterexample.

**Where the wrong claim came from.** The Zygisk API header documents that the
*module directory* must be readable by zygote, and names `system_file` context
in doing so, because of SELinux restrictions on the descriptor
`Api::getModuleDir()` hands over a socket. That is a statement about **access to
the module directory through that descriptor**, not about whether the library
loads. This book over-generalised it into a claim about loading. The narrower
statement is the accurate one.

:::note
Module 01 never calls `getModuleDir()`, so this run says nothing about the
`getModuleDir` case. Whether a mislabelled module directory breaks that call
remains **untested**. Modules 03 and 06 do call it, so a later lab can settle
it. Until then, treat the header's directory-context requirement as documented
but unverified here, not as disproven.
:::

### Three labels, all different

The label question is still real; it is just not a loading question. What the
run measured is that there is no single "correct" label you can derive by
convention, because three plausible sources give three different answers.

| Where it comes from | Label observed |
|---|---|
| `restorecon` on a file under `/data/adb/modules` | `u:object_r:adb_data_file:s0` |
| The provider's own working modules' `zygisk/*.so` | `u:object_r:system_lib_file:s0` |
| `ksud module install` staging a fresh `.so` | `u:object_r:system_file:s0` |

The middle row is the one to match: every working module's `zygisk/*.so` on the
device carried `u:object_r:system_lib_file:s0`, checked across `zygiskcamera`,
`zygisk_vector` and `hma_oss_zygisk`. Three modules is three samples, but they
agree with each other and they disagree with both of the labels you would get by
automated means.

So two pieces of earlier advice in this book were wrong, and both are now
withdrawn:

- **`restorecon` is the wrong tool here.** It re-derives the label from the
  file-context policy for the path, and for a path under `/data/adb/modules`
  that policy says `adb_data_file`. It will run, exit `0`, and leave you with a
  label no working module carries.
- **`chcon --reference=module.prop` is the wrong fallback.** `module.prop` in
  the live module directory is `adb_data_file` too, so referencing it reproduces
  exactly the same wrong answer with more ceremony. This book previously argued
  that `module.prop` was the ground truth because the manager wrote it in place;
  the manager did write it in place, and it is still not the label the libraries
  carry.

The rule that survives:

**Set the label explicitly, then read it back and fail if it does not match.**

```bash
adb shell su -M -c 'chcon u:object_r:system_lib_file:s0 \
  /data/adb/modules/<id>/zygisk/arm64-v8a.so'
adb shell su -M -c 'ls -Z /data/adb/modules/<id>/zygisk/arm64-v8a.so'
```

`deploy.sh` in every module now does this: it sets
`WANT_LABEL="u:object_r:system_lib_file:s0"` with `chcon`, reads the label back,
and fails the run if it does not match. Verification is the load-bearing half —
a `chcon` that quietly did nothing is indistinguishable from one that worked
until you look.

Note what this check is now for. It is not "otherwise the module will not load",
because on this rig it loads. It is that a file whose label differs from every
other module's library on the device is a difference you did not intend, in a
mechanism you do not fully control, and the cost of pinning it is one command.
Deploy hygiene, not a fix for a failure the rig actually reproduced.

### One `su -M -c` per step

A practical note from the same run, cause not established. Chaining the move and
the mode change behind a single invocation failed:

```bash
# fails: "chmod: Permission denied", immediately after the mv
adb shell su -M -c 'mv /data/local/tmp/arm64-v8a.so \
  /data/adb/modules/<id>/zygisk/arm64-v8a.so && chmod 644 \
  /data/adb/modules/<id>/zygisk/arm64-v8a.so'
```

The identical `chmod`, issued seconds later as its own `su -M -c` invocation,
succeeded. Nothing about the file changed in between. Whether this is a
namespace-timing effect inside the root shell, something the provider does to
the directory after a write, or something else entirely was not determined.

The working rule is mechanical: **run each root step as its own invocation.**
`deploy.sh` does. It costs a few `adb shell` round-trips and removes a failure
whose cause nobody on this rig can currently explain.

## The general rule

Everything above is one instance of a principle worth naming, because you will
meet it again in unrelated clothing:

**Never write to an artifact that is currently mapped. Write a new file and
rename it into place.**

The reason is not about `.so` files or about Android. It is that a mapping is a
*live reference to an inode*, and file contents can change under a live
reference in a way that in-memory state cannot defend against. Anything that
mmaps a file inherits this hazard: a shared library, a database, a font cache,
an app's own resource archive. When Part IV has you replacing data an app has
mapped, the hazard is identical and so is the fix — new inode, atomic rename,
and something must re-read before the change takes effect.

Two corollaries follow, and they are what actually change your habits:

- **Atomic replacement needs a same-filesystem staging path.** `rename` cannot
  cross filesystems. Staging in `/data/local/tmp` works for `/data/adb/modules`
  because both are on `/data`; it would not work for a destination elsewhere.
- **A new inode does not mean new behaviour.** Something must load the file
  again. Identify what that is — here, zygote at boot — and trigger it
  deliberately, or you will test the old code and believe the new.

## The deploy checklist

Every deploy, in order. `deploy.sh` does steps 2 through 6 for you, checking
each rather than assuming it; the point of listing them is that you can
diagnose a deploy that goes wrong, whether or not the script is what ran it.

1. Build, and confirm the `.so` timestamp is from this build, not the last one.
2. Confirm the destination module directory exists before staging anything —
   a wrong `id` in `module.prop` is a silent failure otherwise.
3. `adb push` to `/data/local/tmp`.
4. `su -M -c mv` into the module directory. Never `cp`.
5. `chmod 644`, then `chcon u:object_r:system_lib_file:s0` — not `restorecon`,
   not `chcon --reference=module.prop`, both of which yield `adb_data_file` —
   and read the label back, failing if it does not match. Each of these as its
   own `su -M -c` invocation.
6. `md5sum` both sides and fail on a mismatch, rather than printing the two
   hashes for a human to eyeball.
7. `adb reboot`. Then clear the log and reproduce.
8. If an armed app crashes, hash first, reboot second, debug third.

That last line is the one that pays. The crash signature described in this
chapter — SIGSEGV during app specialization, faulting inside the provider,
armed apps only — is indistinguishable by inspection from several real module
bugs, and so, by symptom, is a missing destination directory: both produce a
module that looks installed and never runs. The hash tells the crash apart from a stale mapping in two seconds, and
only if you check it before you have already convinced yourself of a story.

## What has been run, and what has not

Lab 2 Part A has now been run on the reference rig, and it settles the central
claim of this chapter directly. A second build of Module 01, its `onLoad` string
changed to `BUILD-2`, was deployed by the stage-then-rename path and the device
rebooted; apps logged `BUILD-2`. A third build, `BUILD-3`, was then deployed the
same way — push, `mv`, `chmod`, `chcon` — with the hash and label verified on
disk and **no reboot**. Newly launched apps kept logging `BUILD-2`. After a
reboot, they logged `BUILD-3`.

That is the mapped-library claim, observed rather than argued: zygote holds the
old mapping, a correct deploy has no effect until zygote restarts, and a newly
forked app inherits the old code no matter what the file on disk says. It is
also why the hash check cannot answer "is my code broken?" on its own — a
matching hash was exactly the state in which the device was still running the
previous build.

Part B has now been run too, once, on the same rig. The `cp` over a mapped
`.so` crashed app spawns immediately with the signature quoted above; the
on-disk hash matched the local build throughout; and a plain reboot cleared the
crash completely, after which the module loaded and logged its new string. So
the file was valid the whole time and only the mapping was damaged, which is
this chapter's claim end to end. What that run did **not** settle is the
selectivity: whether an unarmed app survives while an armed one dies was not
measured, so the asymmetry above remains reasoning, not observation. One
device, one provider version, one module.
[Lab 2](/ZygiskLab/labs/lab-02-safe-deploy/) Part B has you reproduce this
deliberately, on a device you can afford to lose.
