---
title: "The asymmetry of privilege"
description: "A capability table for companion versus injected process, and the status-reporting problem after specialization."
sidebar:
  order: 3
status: unverified
---

Your module is two programs. One of them is root and cannot see inside the app.
The other is inside the app and is not root. They share a socket and nothing
else. Every architectural decision in a Zygisk module is downstream of that
sentence, and this is the chapter where you stop treating it as a limitation to
work around and start treating it as the shape of the thing you are building.

[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)
established the facts: what specialization takes away, and how little an injected
process can write. [Chapter
17](/ZygiskLab/book/companion/17-the-companion-process/) introduced the other
half, a separate root process forked from the daemon rather than from your app.
This chapter puts the two side by side and reads off the consequences — which
side each piece of work belongs on, why live status cannot come from one
reporter, and what a root-side control plane can and cannot reach. It is a design
chapter. There is almost no API in it.

## The capability table

Both processes are yours, both were started because of your module, and they
overlap far less than that suggests.

| | Companion | Injected process |
|---|---|---|
| **Identity** | root, daemon's context | the app's uid, an app domain |
| **Filesystem** | root-side read/write, `/data/adb/` included | app sandbox and world-readable paths |
| **Other processes** | can inspect `/proc` broadly | itself, and little else |
| **JNI / ART** | none — no runtime | full `JNIEnv`, live runtime |
| **App classloader** | unreachable | reachable, with work |
| **Hooks in the app** | impossible | the only place they are possible |
| **Lifetime** | across app launches | one process, one launch |
| **Count** | one, shared | one per injected process |

:::caution[Two of those rows are design assumptions, not promises]
**Lifetime** and **Count** are the shape the API implies, not guarantees the
header gives. As
[Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/) sets out, the
header promises a separate root process and warns that your handler may be
called concurrently on several threads — it says nothing about how many
companion processes exist, when they are created, how long an idle one lives,
whether one is reused across connections, or whether a crashed one is
restarted. Design so that you would still be correct if the answer changed:
treat every connection as independent, keep no cross-connection state you
cannot rebuild, and guard anything shared. See
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/).
:::

Read the two columns as near-disjoint, because they are. The companion has
privilege and no visibility; the injected side has visibility and no privilege.
Neither column is a subset of the other, which is why "just do it in the
companion" and "just do it in the app" are both wrong as general answers.

The overlap is worth naming precisely, because it is where the split becomes
obvious rather than arbitrary. Both sides can:

- **Read world-readable files.** Anything readable by any uid on the device is
  readable by both. This is a smaller set than it sounds; most of what a module
  wants is not in it.
- **Compute.** Parsing, hashing, decoding, image work — anything that is pure
  arithmetic over bytes runs identically on either side. Where it runs is a cost
  decision, not a capability one, and Chapter 8's cost model says it should not
  run on the launch path.
- **Speak the socket.** Both ends of the channel from
  [Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/), and both can
  hold state for as long as their process lives.
- **Observe their own process.** `/proc/self` is readable to anyone. This is the
  reason [Part VI](/ZygiskLab/book/footprint/21-your-footprint/) is possible at
  all, and it cuts both ways.

That is the whole overlap: world-readable reads, computation, the socket, and
self-inspection. Everything else in the table belongs to exactly one side.

:::note
The lifetime and count rows are the ones people plan around last and regret
first. There is one companion and there are many injected processes, so any state
that must be shared between two apps you injected into lives in the companion or
nowhere. Chapter 12 made the same point from the other direction: two injected
processes share your `.so` on disk and nothing else.
:::

## Push work across the socket

Here is the heuristic, stated first and defended after:

> Work belongs in the injected process only if it *requires* the injected
> process. Everything else belongs in the companion.

That is a strong claim, and it deserves a real argument rather than an appeal to
tidiness. There are three independent reasons, and they happen to point the same
way.

**The injected side is on the critical path of app launches.** Chapter 8's cost
model is the operative one: your callback runs for every process the provider
injects into, hundreds of times a day, and is overwhelmingly running in processes
you do not care about. Time spent there is time added to somebody's cold start.
The companion runs once, outside anybody's launch, and can afford to be slow —
a fifty-millisecond parse in the companion is fifty milliseconds nobody waits
for, while a five-millisecond parse in `preAppSpecialize` is a device-wide tax.

**The injected side is the side an app can inspect.** Your library is mapped into
a process the app controls. Its code is enumerable, its threads are countable,
its open descriptors are listable, and the hooks it installs are exactly the
anomalies [Part VI](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)
teaches an app to look for. Code that lives in the companion is in another
process with another uid; the app cannot read it, cannot enumerate it, and
cannot fingerprint it. Every function you move across the socket is a function
that stops being evidence.

**The injected side has the least privilege.** It is the side where the
privileged operation you wanted will fail, and — worse — fail in a way that is
easy to misread. Chapter 12 was explicit that a denial can arrive as `ENOENT`
rather than `EACCES`, which means privileged work in the injected process does
not merely fail; it fails while telling you a plausible lie about why.

Latency, exposure, privilege. Any one of them would justify the split. Together
they make the injected side a place you visit rather than live.

In practice the heuristic sorts almost everything immediately. *Requires the
injected process*: installing a hook, calling into the app's classes, reading the
app's memory, anything touching `JNIEnv`, anything that must observe a specific
call at the moment it happens. *Does not*: reading configuration, parsing it,
writing files anywhere root can read, network work, decoding an asset, keeping
counters that outlive one launch, and — this is the one people keep in the wrong
place — deciding *policy*. The injected side should ask "what should I do here?"
and act; it should not be the thing that works out the answer from first
principles on every launch.

The residual case is the interesting one: work that requires data only the
companion can reach, at a moment only the injected process can observe. That is
what the socket is for, and it is why Chapter 18 spends its length on framing and
timeouts rather than on transport. Design that request to be answerable once and
cached for the life of the process, not asked per event.

:::caution
The socket is not free either. A round trip on a hot path — inside a hook that
fires per frame, per call, per allocation — is a latency and a footprint you have
moved rather than removed, and a blocking read inside a hook is a way to hang an
app on your own companion. Ask early, cache the answer, and keep the hook local.
:::

## The status-reporting problem

Now the consequence that has no clean solution, only an honest one.

You want a module manager UI to say: *active in `com.example.app`, armed at
09:12, 412 frames processed, last error none.* That is a reasonable thing to
want. It is also a request for live status from a process that is structurally
incapable of reporting it.

Restate the constraint exactly. The injected process knows every interesting
fact — it armed, it found the class, it installed the hook, the hook fired 412
times, one call threw. It also cannot write anywhere the UI can read. Chapter 12
walked the reachable paths and the honest answer was: its own app directory, and
essentially nothing else. The companion socket is not openable after
specialization, `/data/adb/` is root-owned and restricted, shared storage runs
through the framework's scoped-storage machinery and grants an injected module no
permission its host app lacks.
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) covers the API calls
that look like they would rescue you here and do not, `exemptFd` chief among
them — a descriptor you cannot rely on carrying across the boundary is not a
status channel you can build on.

So there is no single writer. There are two, and they see different things:

**The companion records what it knows at the time it acts.** It saw a connection
arrive from a process it can identify, it answered a request, it handed over a
configuration. It can write that to a root-side path immediately, with a
timestamp, because it is root. What it cannot tell you is anything about what
happened afterwards. It sees connections and requests, not behaviour.

**The injected process leaves its outcome where it is permitted to write.** A
small file in the app's own cache directory — the path Chapter 12 identified as
the one dependable place — holding what it did and how it ended. Written once at
a sensible point, not continuously; this is a deposit, not a stream.

**Something with root joins the two afterwards.** The companion, or the manager's
own root-side code, reads both halves and correlates them on whatever key you
chose — a package name plus a launch identifier the companion generated and
handed down over the socket at arming time is the version that actually works,
because it makes the join exact rather than heuristic.

The important framing is that this is not a workaround. It is the direct shape of
the platform's own decision. Android specialized the process precisely so that it
could not write outside its sandbox; a design that assembled status from two
writers is what "an app cannot write to root-owned paths" looks like when you
still want observability. Fighting it means looking for a hole in the sandbox,
and if you find one you have found a bug in one build rather than an architecture.

The costs are real and you should design knowing them.

**The join is after the fact.** Nothing about this is live. The injected half
appears when the injected process decides to write it, which is at least one
launch behind whatever the user is watching. A UI that promises real-time status
is promising something the platform does not offer; a UI that shows "as of the
last launch" is telling the truth.

**The two halves can disagree.** The companion says it armed a process; the app
directory holds nothing. That is not a contradiction to be resolved by picking a
side — it is information, and usually the most useful information the system
produces. It means the injected process either never got far enough to write, or
died first, or wrote somewhere you are not looking. Build the UI to *show* the
disagreement rather than reconcile it silently, and you have accidentally built a
diagnostic.

**A process that dies early leaves only one half.** Crashes, an app killed for
memory, a user swiping it away mid-launch. The companion half will exist because
it was written the moment the companion acted; the injected half will not.
Missing-injected-half is therefore the common case, not the exceptional one, and
any join that treats it as an error will spend its life reporting errors.

**Neither half is trustworthy in the way root-side data usually is.** The
injected half was written by a process running inside an app's sandbox, on a
device where the app has more control over that sandbox than you do. Treat it as
a report, validate its shape before parsing it, and do not let a malformed status
file become a bug in your root-side reader. Chapter 18 made the same argument
about the socket, and for the same reason.

## A root-side control plane

Which brings you to the surface where all of this is actually visible: a
root-side UI. Modern root managers can host a module-provided web interface, and
the pattern is now common enough to design against — but design against the
*shape*, not the API.

:::caution
Whether a WebUI is supported at all, where its files live, what a page is allowed
to call, and what privileges those calls run with are entirely
provider-and-version-specific. KernelSU-derived managers, Magisk, and their
various forks differ, and the details have changed across releases of each.
Nothing in this section is a stable interface, and none of it has been run on the
reference rig — Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next
1.4.5. Confirm your provider's current support before you build on it.
:::

What the shape gives you, stated in terms of privilege rather than API:

**It can read what root can read.** That is the whole point of it, and it is
exactly enough for the status problem above. Both halves — the companion's
root-side record and the app-directory deposit — are readable from a root-side
surface, which is what makes the join possible at all. A control plane that could
not read into app data directories could not do this job.

**It can write configuration the injected side reads at arming time.** This is
not a new mechanism; it is
[Chapter 11's](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) target file
seen from the other end. The module reads its arming configuration from its own
module directory in `preAppSpecialize` via `getModuleDir()`; a root-side surface
can write that file. Put those together and you have a UI that retargets the
module without a rebuild — the whole reason Chapter 11 argued for a file over a
compile-time constant.

Note the timing that falls out of it. Configuration is read at arming time, which
is process start. A change written now takes effect at the target app's *next*
launch, not immediately, and the honest UI says so rather than implying it
applied. If you want a running app to change behaviour, configuration is the
wrong channel entirely; the socket is the only channel, and only for a process
that is still holding it.

**It cannot reach into a running app process.** No amount of root on the manager
side gives it a `JNIEnv`, the app's classloader, or the ability to install a
hook. Those live in the one column of the capability table the control plane has
no access to. Root is not the missing ingredient for in-process work — being in
the process is, and only the injected module is.

So the honest description of a module's control plane is: it configures the next
run and reports on the previous ones. It does not drive the current one. Design
the UI to that promise and it will be accurate on every provider; design it to
"live control" and it will be a lie on all of them.

## Before you continue

The architecture in this chapter is solid because it follows from what
specialization *is*: a process that changed its uid, its SELinux domain and its
mount namespace cannot write to root-owned paths, and no module design recovers
that. The split, the heuristic and the two-writer status model are consequences,
not conventions.

The specifics underneath them are as trustworthy as your own device, and Chapter
12 said the same about the facts this chapter builds on. Three things are worth
checking yourself before you commit a design to them: that your injected process
can in fact write to its app cache directory and that root can read the result;
that your provider's companion can write to the root-side path you have chosen
for its half; and that your provider's manager supports a module WebUI at the
version your users are running, which is the claim in this chapter most likely to
be false for somebody. None of the three has been run on the reference rig, and
each is a few minutes of work to establish on yours.
