---
title: "Choosing not to run"
description: "Lab 3: arming a module for one package only, and measuring the cost the unarmed path imposes on every other launch."
sidebar:
  order: 4
status: unverified
---

Your module runs in every app on the device. Not the app you care about — every
app, every launch, for the entire uptime of the phone. That is not a
configuration you chose; it is what "loaded into zygote" means. The provider
forks a process, your `.so` is in the child's address space, `onLoad` fires,
`preAppSpecialize` fires, and only then does anything in your code get to form an
opinion about whether this process was ever interesting. The dialer, the keyboard
that respawns constantly, the launcher, a dozen background services waking on a
sync — each one pays for your presence before you have decided you do not want to
be there.

The discipline this chapter teaches is the response: decide as early and as
cheaply as you can, and then leave. Chapters 8 through 10 built the three pieces
you need — [the window and its
constraints](/ZygiskLab/book/prespecialize/08-specialization-window/), [how to
read what process you are in](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/),
and [what `setOption` can do about
it](/ZygiskLab/book/prespecialize/10-setoption-and-flags/). This chapter is about
assembling them into a default posture, and it argues that the posture is not a
nicety. It is the difference between a module and a tax.

The worked example throughout is `modules/03-armed-once/`, the Lab 3 module. Read
its `jni/main.cpp` beside this chapter.

## What staying resident actually costs

There are three costs, and they are paid by different parties, which is why the
first two are easy to dismiss and the third is easy to forget entirely.

**Latency, paid by the user, on every launch.** Whatever your module does in
`onLoad` and `preAppSpecialize` sits on the critical path of app startup. Not
your app's startup — *every* app's startup within the provider's injection scope.
A module that spends a millisecond deciding it is uninterested has added a
millisecond to the cold start of every process on the device. That number is
small enough to sound harmless and large enough to be measurable, and it
compounds with every module the user has installed. Chapter 8 made the cost model
explicit; the operative consequence here is that the uninteresting case is the
overwhelmingly common case, so the uninteresting case is the one whose cost
determines your module's real footprint.

**Memory, paid by every process.** [Chapter
6](/ZygiskLab/book/load/06-how-the-loader-finds-you/) measured Lab 1's library at
around 232KB, most of which is statically linked C++ runtime rather than anything
you wrote. That is a per-process mapping. Much of it is file-backed and shared
across processes, so the resident cost is not 232KB multiplied by the process
count — but the mappings are real, the relocations are per-process dirty pages,
and the accounting is not free. More to the point, it is a cost you are imposing
on processes that will never receive any benefit from it.

**Detection surface, paid by you.** This is the one that matters most and shows
up last. A library mapped into a process is visible from inside that process:
`/proc/self/maps` lists it, the dynamic linker's bookkeeping knows about it, and
anything walking loaded objects can find it. A module present in *one* app is
observable in one app. A module present in *every* app is observable in every
app, which means every app on the device is a place where your module can be
found, fingerprinted, and counted — including apps you have no interest in and
whose developers have no interest in you, but whose SDKs report anomalies
upstream anyway. Part VI's [Your
footprint](/ZygiskLab/book/footprint/21-your-footprint/) treats the mechanisms in
detail. The design rule falls out of it here: your footprint should be
proportional to your purpose, and in almost every process your purpose is
nothing.

## The default posture

State it as a rule and then defend it:

> In any process you have not deliberately armed for, do the minimum work needed
> to reach that conclusion, unload your library, and return.

The defence is that the alternative has no natural stopping point. If your module
stays mapped in uninteresting processes, there is never a moment at which
something forces you to notice the cost, because nothing breaks. The phone works.
The apps launch. The only symptoms are a slightly slower device, a larger
detection surface, and a class of bug — code running in a process you never
thought about — that surfaces months later as an inexplicable crash in an
unrelated app. Modules that misbehave in this way are not badly written so much as
never asked to justify their presence.

The posture also buys you something concrete for debugging. When your module runs
in exactly one process, a `logcat` full of your tag is a log of one thing. When it
runs everywhere, you are filtering your own noise out of your own evidence before
you can read it, and every measurement you take is contaminated by processes you
did not intend to measure.

## Structuring the decision

Order of operations is the whole technique. Work backwards from what the unarmed
path must do, and put nothing in front of it.

`ArmedOnce::preAppSpecialize` reads, in order: a timestamp, the process name, the
configured target, one comparison, and then — on the unarmed path — `setOption`,
a second timestamp, one log line, and `return`. Nothing else exists in that
function before the branch. That is deliberate.

```cpp
void preAppSpecialize(AppSpecializeArgs *args) override {
    // Timestamp first, before touching JNI or the filesystem, so the
    // measurement below covers the module's full cost for this
    // callback - not just the part after some other setup work.
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    const char *name = env->GetStringUTFChars(args->nice_name, nullptr);

    char target[kConfigMax + 1];
    readTarget(api, target, sizeof(target));
```

Three things are worth naming about that opening.

The timestamp is the *first statement*, not the first statement after setup.
A measurement that starts after the expensive part has already run tells you
nothing you wanted to know. Put `t0` anywhere else and the number this module
reports becomes a smaller, flattering number that omits exactly the work you were
trying to account for.

The `nice_name` read comes through `GetStringUTFChars`, which allocates, and is
paired with a `ReleaseStringUTFChars` on *both* exits from the function — the
unarmed return and the armed fall-through. Chapter 9 covers why that pairing is
not optional and what leaks if you forget it on the path you exercise a thousand
times a day.

The target buffer is a fixed 256-byte stack array. No allocation, no `std::string`,
no ownership question. In a function that runs on every app launch and whose entire
job is to be cheap, the cheapest storage that fits is the correct storage.

Then the decision, and the exit:

```cpp
bool armed = target[0] != '\0' && strcmp(name, target) == 0;

if (!armed) {
    api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    ...
    env->ReleaseStringUTFChars(args->nice_name, name);
    return;
}
```

Note the short-circuit. If `target` is empty — the file is missing, unreadable, or
blank — `armed` is false without a `strcmp` ever running. The module fails
*closed*: a configuration problem produces a module that arms for nothing, never
one that arms for everything. That is the correct direction for the failure to
point, and it is a one-token decision in the source. Getting it backwards is how
a module ends up hooking the whole device because someone typo'd a filename.

The comparison itself is `strcmp`, not a prefix test, and that is a policy
choice rather than a shortcut. `args->nice_name` is a process name, so an app's
`:remote` or `:sync` sub-processes have names the exact match will reject — this
module arms for the main process only and unloads out of the app's own other
processes. Sometimes you want the opposite, and Chapter 9 works through both
policies; the reason `ArmedOnce` embodies this one is that it cannot over-arm. A
naive prefix match written as `strncmp(name, target, strlen(target))` matches
`com.example.app2` against a target of `com.example.app`, which is a stranger's
package. If you do want every process of an app, the check has to respect the
token boundary — equal, or the target followed by `:` — not merely share a
prefix.

There is a second, subtler ordering rule underneath all of this: **what you avoid
touching at all**. The unarmed path never calls a JNI method beyond the string
accessor, never installs a hook, never spawns a thread, never opens a companion
connection, never writes a file. Everything the later parts of this book teach you
to do belongs *after* the arm check, not before it. The temptation to hoist "just
one cheap thing" above the branch — a version check, a build fingerprint read, one
lazily-initialised singleton — is how the cheap path stops being cheap, one
reasonable-looking commit at a time.

:::caution
`onLoad` runs before `preAppSpecialize` and has no idea what process it is in.
Chapter 5 is explicit about this: `onLoad` fires identically for an app process
and for `system_server`, and nothing in its signature distinguishes them. So
`onLoad` cannot participate in the arming decision at all, and any work you put
there is unconditional work in every process. Stash the two pointers and stop.
:::

## Where the arming configuration lives

`ArmedOnce` does not have its target compiled in. It reads it, at runtime, from a
file:

```cpp
static void readTarget(Api *api, char *out, size_t outSize) {
    out[0] = '\0';
    int dirFd = api->getModuleDir();
    if (dirFd < 0) {
        LOGW("getModuleDir failed; module will not arm");
        return;
    }
    int fd = openat(dirFd, kConfigFile, O_RDONLY);
    // Close the module-dir fd as soon as openat has used it. The header does
    // not state who owns this descriptor, so we take ownership: leaking one
    // per app launch would be a descriptor pointing at the module directory,
    // open in every process the provider injects into - which is exactly the
    // kind of trace Chapter 21 teaches you to look for.
    close(dirFd);
```

That `close` is worth pausing on. The header documents what `getModuleDir()`
returns and when it is usable, but it does not say who owns the descriptor
afterwards. Faced with that silence the safe reading is that you own it, and
the cost of guessing wrong in the other direction is not a leak you will notice
in testing — it is one open descriptor per app launch, pointing at your own
module directory, in every process on the device the provider injects into.
That is a footprint, not just untidiness, and
[Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/) is where you meet it
again from the other side.

`Api::getModuleDir()` returns a file descriptor for the module's own directory on
the device — the one holding `module.prop` — and `openat()` resolves a name
relative to it. This matters because in `preAppSpecialize` you are still in
zygote's mount namespace and zygote's SELinux domain, and an absolute path to
`/data/adb/modules/...` is not a path you can rely on resolving the way you
expect. The directory fd is the sanctioned route, it is only valid from the `pre`
callbacks, and it exists precisely so that a module can read its own
configuration in this window. The API offering it at all is a statement that
per-device configuration is an expected thing to want here.

The cost is honest and worth stating plainly: **this is a file open and a read on
every app launch on the device**, armed or not. Two syscalls, a `close`, and a
`strcmp`, multiplied by every process the provider injects into for as long as the
phone is up. That is a real design decision and there are real alternatives.

| Approach | Runtime cost | Retarget |
|---|---|---|
| Compile-time constant | Zero | Rebuild, redeploy, reboot |
| `getModuleDir()` + `openat()` | Two syscalls per launch | Edit a file, reboot |
| Cache across launches | — | Does not work; see below |

A compile-time constant is genuinely free and genuinely defensible. If your module
targets one package forever and you are optimising the unarmed path to its floor,
bake the name in and skip the file. The price is that changing your target means
rebuilding the `.so`, redeploying it under [Chapter 7's
discipline](/ZygiskLab/book/load/07-deploying-safely/), and rebooting — which,
during the kind of iteration a lab involves, is the slowest loop in this book.

The third row is the interesting one, because it is the option a systems
programmer reaches for first and it does not exist. You cannot read the config
once and cache it, because there is no "once" to read it in. Your module is loaded
into a fresh forked child every time; the static you wrote it into was
initialised in *that* child and dies with it. Chapter 5 covers what does and does
not survive a fork: nothing your module writes in one app process is visible in
the next. There is no cross-process cache in a per-process module, and any design
that assumes one is wrong in a way that will appear to work on the first process
and fail silently after.

You could still cache within a single process — but the decision is made once per
process, so caching it saves nothing.

`ArmedOnce` chose the file because Lab 3 is a lab: the reader retargets the module
at a different package by writing one line and rebooting, without a rebuild, and
the syscall cost is not hidden from them — it is inside the very measurement the
lab collects. That is the right tradeoff for teaching and for any module you are
still iterating on. It is not the only defensible one, and a shipping module with
a fixed target has a good argument for the constant.

## Unloading as discipline

The unarmed path's last act before returning is:

```cpp
api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
```

Chapter 10 covers the mechanics: what the flag instructs the provider to do, when
it takes effect, and what the loader does with your library afterwards. What
belongs here is why it is part of the posture rather than a micro-optimisation.

Returning early stops you *doing* anything. It does not stop you *being* there.
Without the `dlclose`, an uninterested module remains a mapped, enumerable object
in a process it has already decided it has no business in — the detection surface
from the first section, retained for no purpose, in every app on the device. The
early return addresses the latency cost. The unload addresses the other two. They
are two halves of the same decision, and doing only the first is a module that
politely declines to work while continuing to be evidence.

There is a precondition, and the module's comment states it: `DLCLOSE_MODULE_LIBRARY`
must be set only when nothing depends on your library staying mapped. If you have
installed a PLT hook, registered a native method, left a function pointer anywhere
the runtime will later call, or started a thread whose entry point lives in your
`.so`, unloading pulls the code out from under a caller who has not run yet. The
result is a crash in a process that has no visible connection to your module. The
reason `ArmedOnce` can set the flag safely is stated in its source: on this path it
has not done any of those things. That is exactly the invariant the ordering rules
in the previous section were protecting — the unarmed path is safe to unload
*because* nothing was hoisted above the branch.

:::note
Consequence worth predicting before you see it: an unarmed process logs its
`preAppSpecialize` line and then nothing. No `postAppSpecialize` line follows,
because there is no longer a module in that process to call. If you go looking
for a matching pair of lines per launch, the absence is the proof the flag took
effect, not a missing log.
:::

## What you do not know until you run it

The delta this module prints is its own callback cost and nothing more: one JNI
string accessor, one `getModuleDir`, one `openat`/`read`/`close`, one `strcmp`,
one `setOption`. It is not the provider's total per-app injection overhead —
finding the module, mapping the `.so`, running the C++ runtime's static
initialisers, `onLoad` — and it cannot see any of that from inside itself. Nor is
one sample a number. Scheduling, page cache state, and whatever the device was
doing at that moment move it around freely, and the first launch after a reboot is
not like the tenth.

So the honest position at the end of this chapter is that the argument is sound
and the magnitude is unmeasured. [Lab
3](/ZygiskLab/labs/lab-03-choosing-not-to-run/) is where you get the magnitude,
on your own rig, across enough launches to see a spread rather than a value — and
where you prove the negative half of the claim, which is that the module genuinely
did not arm anywhere else.

:::note[Lab 3]
This chapter carries [Lab 3](/ZygiskLab/labs/lab-03-choosing-not-to-run/).
:::
