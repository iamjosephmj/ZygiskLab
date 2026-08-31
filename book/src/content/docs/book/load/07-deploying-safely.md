---
title: "Deploying without bricking zygote"
description: "Lab 2: why cp over a mapped .so corrupts zygote, the md5sum diagnostic, and the atomic mv-based safe deploy."
sidebar:
  order: 3
status: unverified
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
Apps your module is not enabled for keep working.

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

The third — verifying that the SELinux label actually took, rather than only
attempting to restore it — is worth its own explanation, because the failure
it guards against is the quietest one in this chapter. The next section covers
it.

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

## SELinux labels: the silent failure

There is one more thing `mv` does not do, and it produces the nastiest outcome
in this chapter because it produces no crash at all.

Every file carries an SELinux security context. A newly created file normally
inherits a label derived from the directory it is created in. A renamed file
does not: `rename` preserves the source file's label, so a `.so` staged through
`/data/local/tmp` arrives in the module directory still labelled as a temp
file. `chmod` will not help — mode bits and labels are independent mechanisms,
and the policy is checked separately from and in addition to the mode.

A provider running under a domain that is permitted to read module files but
not `/data/local/tmp`'s label simply cannot open yours. The result is a module
that is present, correctly named, correctly sized, hashes identical to your
build — and never loads. No error in your log, because your code never ran. No
crash. Possibly nothing in the provider's log either. This is precisely the
"listed but silent" failure from Chapter 4's catalogue, and if you do not know
about labels you will chase it through `module.prop`, ABI naming and scope
configuration first.

The fix is one command after the move:

```bash
restorecon /data/adb/modules/<id>/zygisk/arm64-v8a.so
```

`restorecon` re-derives the label from the system's file-context policy for
that path. Where it is unavailable, copying the neighbouring directory's label
explicitly is the fallback:

```bash
chcon --reference=/data/adb/modules/<id>/zygisk \
      /data/adb/modules/<id>/zygisk/arm64-v8a.so
```

This is exactly why `modules/01-hello-zygisk/deploy.sh` runs
`restorecon || chcon --reference=...` immediately after its `mv`. Read that
script's header comment — it is the short form of this chapter, kept next to
the code it explains.

Whether a mislabelled file actually fails is policy-dependent and therefore
device- and provider-dependent; on a permissive build or a policy that happens
to allow the transition, it may load fine. That variability is an argument for
always relabelling rather than for testing whether you need to — and, just as
important, for verifying that the relabel actually took rather than assuming
`restorecon` (or its `chcon` fallback) did its job. `restorecon || chcon` can
fall through silently: if both legs fail quietly, or the fallback references
the wrong path, you get no error and a label that is still wrong.

`deploy.sh` does not stop at running the relabel command. It reads the label
back with `ls -Z` and compares it against the label on `module.prop`, in the
same module directory. `module.prop` is the correct reference, not an
arbitrary stand-in: it is the one file in that directory the manager itself
wrote, in place, at install time, so its label is exactly what this provider's
policy assigns to files belonging to this module — the ground truth for what
your `.so` should carry, on this device, under this provider. Comparing
against a module you happened not to deploy by hand is a reasonable proxy;
comparing against `module.prop` in the *same* module is the actual answer,
because it cannot have drifted from whatever this install's policy expects.

A mismatch here is a direct hit on the "listed but silent" failure from
Chapter 4's catalogue: the module shows up in your manager, the file is
present, correctly named, correctly sized, and hash-identical to your build —
and never loads, with nothing in any log to tell you why. `deploy.sh` turns
that silent failure into a loud one at deploy time: it fails the run and
prints both labels, so you find out before you go looking for the bug in the
wrong place. You can still see what it checked with your own eyes:

```bash
adb shell su -M -c 'ls -Z /data/adb/modules/<id>/zygisk/'
```

but by the time the script has exited `0`, the `.so` and `module.prop` already
carry the same label — that comparison is no longer something you need to
perform to trust the deploy, only something worth doing once so you recognise
the label yourself.

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
5. `chmod 644` and `restorecon` (or `chcon --reference`), then read the label
   back and compare it against `module.prop`'s label — don't just trust that
   the relabel command succeeded.
6. `md5sum` both sides and fail on a mismatch, rather than printing the two
   hashes for a human to eyeball.
7. `adb reboot`. Then clear the log and reproduce.
8. If an armed app crashes, hash first, reboot second, debug third.

That last line is the one that pays. The crash signature described in this
chapter — SIGSEGV during app specialization, faulting inside the provider,
armed apps only — is indistinguishable by inspection from several real module
bugs, and so, by symptom, are a missing destination directory and a stale
SELinux label: all three produce a module that looks installed and never
runs. The hash tells the crash apart from a stale mapping in two seconds, and
only if you check it before you have already convinced yourself of a story.

This chapter is marked unverified: the mechanism is drawn from the behaviour of
`cp`, `rename` and `mmap`, and the crash signature from observation on the rig,
but nothing on this page has been re-run for this write-up. The claims worth
testing yourself are the selective ones — that unarmed apps survive, and that a
reboot clears a crash whose hashes matched. [Lab 2](/ZygiskLab/labs/lab-02-safe-deploy/)
has you reproduce both, deliberately, on a device you can afford to lose.
