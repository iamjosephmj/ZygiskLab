# ZygiskLab — Design

**Date:** 2026-08-31
**Status:** outline for review (no chapters written yet)

## What this is

A comprehensive book on writing Zygisk modules, from a hello-world that only
proves it loaded, through deep injection inside a live app process, to the
traces a module leaves and how apps look for them.

The centre of gravity is the **writing**. Modules exist to support the prose.

## Decisions taken

| Decision | Choice |
|---|---|
| Primary artifact | Docs site (Astro 6 + Starlight 0.38), code as examples |
| Organising spine | Execution stage — where your code runs in the process lifecycle |
| Reference rig | Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0 + Zygisk Next 1.4.5 |
| Magisk | Noted in call-outs where behaviour differs; not separately verified |
| Concealment depth | Mechanisms + measurable detection surface. No novel bypass tooling. |
| Example code | Fresh minimal modules, purpose-built per chapter. Standalone — no frametap. |
| Build system | NDK + `ndk-build`. No Gradle. |
| Publishing | GitHub Pages, `iamjosephmj.github.io/ZygiskLab` |
| First delivery | This outline only |

### Why the execution-stage spine

Zygisk's difficulty is that it is a narrow window into a process that has not
become an app yet. Almost every beginner failure is a lifecycle-timing
question. Making the lifecycle the spine puts the real failure modes on the
critical path, and it lets Part VI be *earned*: you can only discuss hiding a
footprint you spent five parts deliberately creating.

### Verification discipline

Every chapter carries a status marker:

- **proven** — the procedure was run on the reference rig
- **unverified** — written but not yet flashed and exercised

Nothing is promoted to *proven* on a clean compile. This is the same rule the
author applies to his other work, and it is the book's main claim to trust.

### Framing of Part VI

Rules of Engagement appear in Chapter 2 and are linked from every Part VI
chapter. Each concealment mechanism is paired with its detection counterpart,
and Part VI ends with a defensive chapter written for the app author. The book
explains how detection works and what published solutions do about it; it does
not ship novel evasion tooling.

## Repository layout

```
ZygiskLab/
├── README.md                       # what it is, ROE, link to the site
├── LICENSE
├── book/                           # Astro + Starlight
│   ├── astro.config.mjs            # site + base: '/ZygiskLab', sidebar
│   ├── package.json                # @astrojs/starlight ^0.38, astro ^6, sharp
│   └── src/
│       ├── assets/                 # logo, og-image
│       ├── styles/custom.css
│       └── content/docs/
│           ├── index.mdx           # landing
│           ├── book/
│           │   ├── foundations/    # Part I    ch 1–4
│           │   ├── load/           # Part II   ch 5–7
│           │   ├── prespecialize/  # Part III  ch 8–11
│           │   ├── postspecialize/ # Part IV   ch 12–16
│           │   ├── companion/      # Part V    ch 17–20
│           │   ├── footprint/      # Part VI   ch 21–25
│           │   └── appendices/
│           └── labs/               # lab 1–7
└── modules/                        # one buildable module per lab
    ├── NN-name/{jni/,module.prop,build.sh,README.md}
    └── 07-detection-harness/app/   # Lab 7's Android app (the one Gradle project)
```

Each module directory is self-contained: `jni/Android.mk`, `jni/Application.mk`,
sources, a `module.prop`, a `build.sh` that produces a flashable zip, and a
README stating what it proves and how to see the proof.

## Chapter outline

Status key: all chapters are **unverified** until run on the rig.

---

## Part I — Foundations

*What you are standing on before you write a line of module code.*

### 1. What Zygisk is, and what it is not

- Zygote: the pre-warmed process every Android app is forked from, and why
  that fork is the highest-leverage moment in the system
- Specialization: how a generic zygote fork becomes a specific app process
  (uid, seinfo, nice_name, mount namespace, capability drop)
- Where Zygisk inserts itself, and who provides it (Magisk's built-in
  implementation vs. Zygisk Next as a standalone provider on KernelSU)
- The comparison table readers actually want: Zygisk vs. Xposed/LSPosed vs.
  Frida vs. `LD_PRELOAD` vs. repackaging — injection point, persistence,
  privilege, detectability, and what each cannot do
- What Zygisk is *not*: not a hooking framework. It gets you code execution in
  the right process at the right moment; the hooking is yours to bring.
- Honest limits up front: no kernel access, no SELinux bypass, arch-specific,
  breaks with provider updates

### 2. Rules of engagement

- Authorized-use framing: your own devices, your own apps, or written
  permission
- Why this matters more here than in most Android writing — the same
  mechanism that instruments your own build instruments someone else's
- What this book deliberately does not contain, and why that is a choice
  rather than an omission
- Responsible disclosure: reporting an app-side weakness you find while
  testing
- Legal caveat and warranty disclaimer

### 3. The rig and the toolchain

- Reference rig stated once: Pixel 6 Pro, Android 16, arm64, KernelSU-Next
  3.3.0, Zygisk Next 1.4.5 — and how to verify each piece is actually active
- Why a *spare* device, not your daily driver
- NDK setup, the API level you target and why it is not the newest one
- `module.prop`: every field, which ones the manager displays, which ones the
  loader reads
- The module directory on device, and what each path in it means
- Reading logs that matter: `logcat` filtered to the Zygisk provider, and
  where a crash in your module actually surfaces
- Recovering a device that will not boot because of your module

### 4. Hello, Zygisk *(Lab 1)*

- The smallest module that proves it loaded — one file, one log line
- Line-by-line: the header, the class, `onLoad`, `REGISTER_ZYGISK_MODULE`
- `Android.mk` and `Application.mk` explained field by field
- Packaging: the zip layout the manager expects
- Installing, rebooting, and finding your log line
- **Lab 1 deliverable:** a log line from inside a named app's process, with the
  pid and uid printed, proving you ran inside *that* app and not zygote
- Failure catalogue: module not listed, listed but silent, log line appears for
  every process, device bootloops

---

## Part II — The load stage

*You are inside zygote. Nothing is an app yet.*

### 5. Anatomy of a module

- The `zygisk::ModuleBase` interface in full: every callback, when each fires,
  and what is legal inside it
- `zygisk::Api`: the handle, its lifetime, and the operations it exposes
- `REGISTER_ZYGISK_MODULE` — what the macro actually expands to
- Module state: what persists between callbacks, what persists between
  processes, and what does not persist at all
- The order of events for one app launch, as a single annotated timeline
  diagram readers can return to

### 6. How the loader finds you

- ABI selection: `arm64-v8a`, `armeabi-v7a`, `x86_64` — one `.so` per ABI and
  what happens when the one for the running ABI is missing
- Linker namespaces: why your module is not linked the way an app's JNI
  library is, and what that restricts
- What you may safely link against, what you must `dlopen` yourself, and what
  will simply not resolve
- The C++ runtime question: why static linking is the default here
- Keeping the module small, and why size matters at this stage
- Symbol visibility, and why a stray exported symbol is a footprint (forward
  reference to Chapter 21)

### 7. Deploying without bricking zygote *(Lab 2)*

- The hazard, stated plainly: `cp` over a mapped `.so` truncates and rewrites
  the same inode while zygote is executing those pages
- The symptom, and why it is so misleading — SIGSEGV inside app
  specialization, only for processes your module actually touches, so it looks
  like a bug in your new code or like app-side defences. It is neither.
- The one-step diagnostic: `md5sum` on-device against the local build.
  Matching hashes mean the file is fine and the mapping is stale.
- The correct deploy: push to `/data/local/tmp`, `mv` into place (atomic
  rename gives a new inode and leaves existing mappings intact), reboot
- Writing to the module directory needs mount-master (`su -M`); plain `su -c`
  is denied even as root under KernelSU
- **Lab 2 deliverable:** a `deploy.sh` that is safe by construction, plus a
  deliberate reproduction of the corruption so the reader has seen the crash
  signature once, on a spare device
- The general rule this is an instance of: never write an artifact that is
  currently mapped

---

## Part III — `preAppSpecialize`

*The window: forked from zygote, still root-ish, not yet the app.*

### 8. The specialization window

- What exists at this moment and what does not: no app classloader, no
  Application object, no app data directory, no app SELinux context
- What you still have that you are about to lose
- What you must not do here, and the consequences of each: heavy work delays
  every app launch; spawning threads does not survive the way you expect;
  touching the JVM early destabilises the fork
- The cost model: your code runs for *every* specialization, so the common
  path must be nearly free

### 9. Reading `AppSpecializeArgs`

- Every field, what it means, and which ones are safe to read
- Identifying your target: `nice_name` vs. uid vs. app data dir — the
  trade-offs, and why the obvious choice is the wrong one
- Reading a JNI string safely in this context
- Multi-process apps: `:remote` processes, and why a package match is not a
  process match
- **Worked example:** arm on exactly one process of one package, prove the
  match, and prove the non-match

### 10. `setOption` and the flags

- `FORCE_DENYLIST_UNMOUNT` — what it unmounts, when it takes effect, and what
  it does not hide
- `DLCLOSE_MODULE_LIBRARY` — unloading yourself, and the exact rules about
  what may still run afterwards
- Interaction between the flags and the provider's own denylist
- What each flag does to your footprint (forward reference to Part VI)

### 11. Choosing not to run *(Lab 3)*

- The default posture: decide fast, return, and leave no trace in processes
  you do not care about
- The real cost of staying resident in every app on the device — launch
  latency, memory, and detection surface
- Structuring the decision so the not-interested path is the cheapest path
- Where the arming configuration lives, and why reading a file here is a
  design decision rather than a detail
- **Lab 3 deliverable:** a module armed for one package, with a measurement
  showing the unarmed path's cost on other app launches

---

## Part IV — `postAppSpecialize`

*You are the app now. Everything you lost, you lost.*

### 12. What changed at the boundary

- A before/after table: uid, SELinux context, mount namespace, capabilities,
  filesystem reachability
- What an injected app process can actually write — its own
  `/data/user/0/<pkg>/cache` — and what is denied: `/data/adb/*` (root +
  SELinux), scoped-storage paths under `/storage/emulated/0`
- Why this table dictates your whole architecture, and the designs it rules out
- The JVM is now usable; the classloader is not yet what you want (Ch. 13)

### 13. JNI inside a live app

- Getting a usable `JNIEnv` and attaching from a thread that is not the one
  you were called on
- The classloader problem: the system classloader cannot see the app's classes,
  so `FindClass` fails for exactly the classes you care about
- Getting a real app classloader: the routes, and when each becomes available
- Reflection from native code without losing your mind — a small helper
  pattern developed once and reused for the rest of the book
- Local vs. global references, and the leak that will bite you
- Exception discipline: checking and clearing, and what an uncleared pending
  exception does to the host app
- Hidden-API restrictions and how they present from native code

### 14. Hooking native symbols *(Lab 4)*

- The PLT/GOT hook: what it is, in a diagram, before any code
- `Api::pltHookRegister` and `Api::pltHookCommit` — the register/commit split
  and why it exists
- Choosing what to hook: which library, which symbol, and confirming the
  symbol is actually reached before you hook it
- Writing a trampoline that is safe on the caller's thread
- What a PLT hook *cannot* reach: internal calls that never go through the PLT,
  inlined code, and calls made before your hook was committed
- The deferred-hook problem: the library you want is not loaded yet, and what
  to do about it
- **Lab 4 deliverable:** hook a libc call in one target app, log it, and show a
  correct non-hooked control process
- Debugging a hook that never fires — a decision tree

### 15. Hooking Java through ART

- What ART method hooking actually does to a method entry point
- Why this is the most fragile technique in the book: it depends on ART
  internals that change between releases and are not API
- Survey of the approaches and existing implementations, with an honest
  assessment of each
- The artifact-is-mapped hazard again, in its Java form: force-stop the app
  before rewriting anything it has loaded
- When to reach for this and when a native hook or an app-side approach is the
  better engineering choice

### 16. Threading and timing *(Lab 5)*

- The main-thread problem: your code runs early and off-thread, and almost
  everything interesting in an Android app must happen on the main thread
- Routes onto the main thread, with the trade-offs of each
- Waiting for the app to be ready without polling and without racing it
- Doing work off the main thread without ANRing the host
- **Lab 5 deliverable:** perform a main-thread-only action from an injected
  module, at a moment you chose, with proof of the thread you were on

---

## Part V — The root side

*The companion, and the asymmetry it exists to solve.*

### 17. The companion process

- What the companion is: a separate process that stays root, forked from the
  Zygisk daemon rather than from your app
- `REGISTER_ZYGISK_COMPANION` and its lifetime
- What it inherits, what it shares with your injected module (almost nothing),
  and the one channel between them
- When it forks, how many of them exist, and what that means for state

### 18. Designing the companion protocol *(Lab 6)*

- The socket you are handed, and the fact that everything above it is yours
- Framing: length-prefixed messages, and why the naive `write`/`read` pair
  fails the first time a message is split
- Designing opcodes you will not regret — versioning from message one
- Partial reads and writes, and a read-exactly/write-exactly helper
- Timeouts, and what your injected side does when the companion never answers
- Not trusting your own client: the companion is root and the caller is not,
  so validate every field
- **Lab 6 deliverable:** a request/response exchange where the app process asks
  the companion for something only root can read, with a rejected malformed
  request as the negative control

### 19. The asymmetry of privilege

- A capability table: what the companion can do, what the injected process can
  do, and the narrow overlap
- Designing around it: pushing privileged work to the companion and keeping the
  injected side thin
- The status-reporting problem — with no root channel after specialization,
  live status has to be assembled from two independent writers and joined on
  the root side
- A root-side control plane: what a module manager WebUI can and cannot reach

### 20. Where it breaks

- `Api::exemptFd` — returns false under some providers, so you cannot rely on
  carrying a descriptor across specialization
- `connectCompanion` after specialization: refused
- Provider differences that actually change your design (Magisk vs. Zygisk
  Next), and how to detect which you are running on
- Version drift: what breaks when the provider updates, and how to fail loudly
  instead of silently
- A short catalogue of designs that look obvious and do not work, each with the
  reason

---

## Part VI — Footprint and detection

*The traces you left, stage by stage — and how apps look for them.*
*Rules of Engagement (Ch. 2) apply throughout. Each mechanism is paired with
its detection counterpart.*

### 21. Your footprint, stage by stage

- Walking back through Parts II–V and naming what each stage left behind
- In-process: your `.so` in `/proc/self/maps`, exported symbols, open file
  descriptors, threads you created, hooks you installed (a PLT entry that no
  longer points where it should)
- Process-level: the mount namespace, differences between your app's namespace
  and a clean one, environment and system properties
- On-disk: the module directory, the provider's own directories, manager app
  packages
- Behavioural: launch-time deltas, and the trace that is not a file
- The point of the chapter: the footprint is a consequence of design decisions
  made five parts ago, not something added at the end

### 22. How an app looks for you

- Self-inspection: reading its own `maps`, `status`, `fd`, and what a scan
  actually catches
- Mount namespace inspection: comparing against what the app expects
- Loaded-library enumeration, and integrity checks over its own hooks
- `ptrace` and tracer-presence checks
- Filesystem probes for known root and provider paths
- Property and package checks
- Platform attestation, and where it sits relative to all of the above
- For each: what it costs the app, what it catches, and its false-positive rate

### 23. Existing answers, surveyed

- The provider denylist: what it is and what it actually does
- `FORCE_DENYLIST_UNMOUNT` revisited with Part VI eyes
- Shamiko, and the class of traces it addresses
- Zygisk Next's own design choices around footprint
- For each: what it removes, what it does not remove, and how a reader can
  check rather than take it on faith
- The honest conclusion — what none of them remove, and why attestation is a
  different kind of problem from process-level hiding

### 24. A detection harness *(Lab 7)*

- Building an Android app that inspects itself using the Chapter 22 checks and
  reports what it finds — a real app process, because a root shell sees a
  different world than an app does
- Harness structure: one check per module, each returning evidence rather than
  a boolean, so results are readable and arguable
- Establishing a clean baseline on an unmodified process first
- Measuring your own modules from Labs 1–6 against it
- Reading the results: which of your design decisions produced which finding
- **Lab 7 deliverable:** a table of your own modules against the check matrix,
  with the specific line of your code responsible for each hit
- The value of measuring rather than assuming

### 25. The defensive chapter

- Written for the app author, not the module author
- What is worth checking, ordered by cost-to-benefit
- What is theatre: checks that are trivially defeated, expensive, or that fire
  on legitimate users
- Defence in depth: why no single check is the answer, and where server-side
  signals belong
- Failing well: what to do when a check fires, and why blocking immediately is
  usually the wrong response
- What genuinely raises the cost for an attacker, honestly assessed

---

## Appendices

- **A. API reference** — every Zygisk API call in one table: signature, valid
  callback, provider notes, failure mode
- **B. Troubleshooting by symptom** — "module not listed", "loads but does not
  fire", "fires in every process", "SIGSEGV in specialization", "hook never
  called", "companion never answers", "bootloop"
- **C. Cheatsheet** — the commands, one page
- **D. Glossary** — zygote, specialization, denylist, PLT, mount namespace,
  companion, attestation
- **E. Further reading** — provider source, upstream docs, prior art

---

## Out of scope

- Kernel-level techniques, custom kernels, KernelSU internals
- Frida, Xposed/LSPosed module development (referenced for comparison only)
- APK repackaging and bytecode instrumentation
- Novel evasion tooling of any kind
- Defeating platform attestation
- Magisk-specific verification (call-outs only)

## Resolved on review (2026-08-31)

1. **Size:** 25 chapters and 7 labs stands. Parts III and IV stay separate —
   the `preAppSpecialize` / `postAppSpecialize` boundary is the book's central
   distinction and merging them would blur it.
2. **Chapter 15:** survey-only, as designed. A working ART hooking
   implementation is a book of its own; the chapter explains the mechanism,
   surveys existing implementations, and is honest about the fragility.
3. **Lab 7:** an **Android app**, not a shell script. A real app performing the
   Chapter 22 checks from inside its own process is the only version that
   measures what an app can actually see; a shell script running as root sees a
   different world and would teach the wrong lesson. It lives at
   `modules/07-detection-harness/app/` and is the one Gradle project in the
   repo.
