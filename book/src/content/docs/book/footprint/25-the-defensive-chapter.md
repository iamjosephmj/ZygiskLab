---
title: "The defensive chapter"
description: "What app-side detection is worth doing, what is theatre, and why defence in depth beats blocking on the first hit."
sidebar:
  order: 5
status: unverified
---

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

This chapter is written for the other side of the table. Twenty-four chapters
have addressed the person writing the module; this one addresses the person
shipping the app that module lands in. If you have arrived here having skipped
the rest, the single most useful thing the book can tell you is this: on a
device whose owner has root, code that runs before your `Application` class
exists is not a hypothetical. It is a documented, tooled, reproducible
capability, and every detection you write runs *after* it, inside a process it
already occupies.

That is not a counsel of despair. It is a statement about where your effort
should go. Detection that assumes it gets first move is worth very little.
Detection that accepts it gets second move — and is designed to make the
attacker's second move expensive, noisy, or slow — is worth a great deal. The
rest of this chapter is about telling those two apart in your own codebase.

Chapter 22 assessed each check on its own terms: what it costs, what it
catches, how often it fires on someone innocent. Chapter 23 concluded that
process-level concealment and platform attestation are different kinds of
problem, and that no published concealment tool addresses the second. This
chapter takes both as given and asks the question they leave open: given all
that, what should you actually ship?

## Order your checks by cost-to-benefit, not by thoroughness

The instinct is to check everything. Resist it. Every check has three costs —
engineering time, runtime budget, and false positives — and one benefit, which
is the population of attackers it catches who would otherwise have got through.
That benefit is not evenly distributed. A small number of cheap checks catch
the large, careless majority. A long tail of expensive checks catches almost
nobody, because anyone sophisticated enough to survive the cheap ones is
sophisticated enough to survive the expensive ones too.

Here is a defensible priority order, with the reasoning attached so you can
disagree with it where your threat model differs.

**1. A server-side signal you already have.** Before you write any device-side
code at all, look at what your backend can already see: account age, device and
install identifiers as they change over time, the shape and timing of the
request sequence, whether this session's behaviour resembles this account's
history. This is first not because it is the most sensitive detector — it often
is not — but because it is the only category the device's owner does not
control. Everything below runs on hardware the attacker owns.

**2. Platform attestation, at the right granularity.** Hardware-backed
attestation moves part of the judgement off the device and into a
signing key the app process cannot produce for itself. Chapter 23's conclusion
matters here: this is a categorically different problem from hiding a mount or
a mapped library, which is precisely why it is worth having. It is also the
check most likely to fire on legitimate users — custom ROMs, older devices,
devices with a broken or missing keystore — so treat its output as a
*signal with a population attached*, not a gate. What each attestation
provider actually asserts, and how strong that assertion is, is
provider-specific and version-specific; read the provider's own documentation
rather than a summary, this book's included.

**3. Cheap self-inspection at a small number of well-chosen moments.** Reading
your own `/proc/self/maps` and `/proc/self/mountinfo` costs microseconds and
catches the attacker who has not thought about it at all — which, on a
consumer app, is most of them. Chapter 22 walked what these actually see.
Their value is not that they are hard to defeat. It is that they are nearly
free, so their cost-to-benefit ratio survives even a low catch rate.

**4. Integrity checks over the specific code paths that matter.** Not over
your whole binary — over the handful of functions where a modified return
value converts directly into money or data. If you can only afford to verify
three things, verify the three whose compromise you would have to report.
Chapter 14 and Chapter 15 describe how those functions get hooked; a defender
who understands the hooking mechanism can reason about which of their own
functions are attractive targets, which is a better guide than any generic
checklist.

**5. Everything else.** Tracer-presence checks, package enumeration,
filesystem probes for known paths, property reads. Do them if they are cheap
and you have already done the above. Do not build a programme around them.

Notice what determines the order: not how clever the check is, but how much of
the environment the attacker must control to defeat it. Item 1 requires them to
control your infrastructure. Item 2 requires them to control a key they cannot
extract. Items 3 to 5 require them to control a device they already own.

## What is theatre

Theatre is not a check that can be defeated. Every device-side check can be
defeated. Theatre is a check whose cost — in engineering time, in runtime, or
in legitimate users lost — exceeds what it returns. Three classes, and you can
look for each in your own codebase today.

**Checks whose evidence lives where the attacker already stands.** Anything
that reads a path, a property, a package list, or a file's existence is asking
a question the attacker's code can answer for you, because the code answering
it is inside the process the attacker occupies. A `File("/system/xbin/su")
.exists()` call and a fifty-entry list of known root binary paths are the same
check with different arithmetic: the fifty-entry version costs fifty times as
much and catches the same person. Scan your codebase for lists of hardcoded
paths and package names. The ones that have grown over time are the clearest
signal — each entry was added after someone got past the previous one, which
tells you the mechanism does not generalise.

**Obfuscation used as a security control rather than a delay.** Obfuscating a
check does not make it harder to defeat once found; it makes it harder to
find *the first time*, by one attacker, once. After that the knowledge is
transferable and your obfuscation is a permanent tax on your own debugging.
This is worth doing when your threat model is one-off opportunists at scale
and worthless when your threat model is a determined individual, which is the
same statement as saying its value depends entirely on which of those you
face. Be honest about which you face.

**Checks that fire on people who did nothing wrong.** This is the expensive
class, and it is expensive in a currency that does not appear in your security
budget. A check for an unlocked bootloader, or an unrecognised ROM build
fingerprint, or the presence of accessibility services, will fire on:
developers, enterprise-managed devices with unusual configurations, users of
accessibility tooling who depend on it, people on devices whose vendor
abandoned them, and people in regions where custom ROMs are how a phone stays
usable. You will not see these users complain. They will simply not use your
app, and you will record that as an absence rather than a cost.

Two more that deserve naming because they are commonly sold as security:

- **Emulator detection** as a standalone control. It catches automated
  tooling, which is real value if automation is your threat. It catches
  nothing at all if your attacker is a human with a physical rooted device,
  which is the threat model this whole book describes.
- **Client-side "tamper response" that the client decides.** If the app
  evaluates the check, decides it failed, and refuses to proceed, then the
  decision point is a branch in a process the attacker controls. You have not
  built a control; you have built a single instruction for someone to change.
  The check is only load-bearing if its *result* leaves the device and
  something else acts on it.

:::note
None of this means "delete all your device-side checks". It means each one
should have a stated purpose you could defend in a review: which population it
catches, what it costs, and what happens to the users it catches wrongly. A
check that cannot answer those three is a candidate for deletion regardless of
how sophisticated it looks.
:::

## Defence in depth, and why the device is not where depth comes from

Stacking ten device-side checks does not give you ten times the assurance of
one. It gives you one layer with ten entries in it, because all ten share the
same defeat condition: an attacker with code running in your process before
your code does. That is what Chapters 8 through 16 spend their length
establishing. When one precondition invalidates every check you have, you have
depth in appearance only.

Real depth comes from checks with *different* defeat conditions. That is the
whole argument for server-side signals, and it is worth stating precisely,
because the usual version of it overclaims.

Server-side signals are not unbeatable. An attacker who controls the device
controls what the device reports, and a determined one will make the reports
look ordinary. What server-side signals change is *what the attacker must
control to be consistent*. A device-side check can be defeated by changing one
return value. A behavioural signal computed across a session — timing,
sequence, the relationship between this request and the last thousand from
this account — cannot be defeated by changing a return value, because there is
no single value to change. The attacker must now produce a coherent
counterfeit of ordinary behaviour, sustained over time, without knowing which
features you are looking at. That is a materially harder job, and unlike a
device-side check, it does not get easier when they root a second device.

Attestation belongs in the same category for the same structural reason: the
assertion is produced by something the app process cannot impersonate, and
verified somewhere the attacker cannot reach. Chapter 23's conclusion — that
process-level concealment tools do not address attestation — is the same
observation from the module author's side.

The practical form of this: your device-side checks should feed a server-side
decision, not make one. Send evidence, not verdicts. "This is what
`/proc/self/maps` looked like" is data your backend can correlate across a
population and reason about later; "isRooted: true" is a boolean an attacker
flips in an afternoon, and once flipped you have lost not just the check but
any record that it ever fired.

## Failing well

This is the section most likely to change what you ship, so it gets the most
space.

When a check fires, the instinct is to block. It feels like the responsible
response and it is usually the wrong one, for two independent reasons.

**Blocking teaches the attacker.** A hard block is an oracle. The attacker
changes one thing, relaunches, and reads the result in under a minute. That is
a tight, reliable feedback loop, and it is the single most valuable thing you
can hand someone trying to get past you. They will use it to enumerate your
checks one at a time, and each block tells them precisely which change
mattered. You have converted a search problem into a guided one.

**Blocking is paid for by people who did nothing.** Your false-positive
population is not attackers who got lucky. It is real users: someone on a
custom ROM because their vendor stopped shipping updates, someone whose
accessibility tooling looks like automation, someone on an enterprise-managed
device with an MDM profile that rewrites things, a developer with a debug
build of your own app installed alongside. They see a screen that says
something went wrong and gives them no way forward. Almost none of them will
contact you. They will uninstall, and the cost will show up in a retention
metric that nobody attributes to the security team.

So consider the alternatives — each of which has real costs of its own, and
none of which is free.

**Flag and continue.** Record the evidence, let the session proceed, act on
the aggregate later. This removes the oracle entirely: the attacker gets no
signal, so no iteration loop. The cost is that the fraudulent session
completes. This is right when the action is reversible or the loss is bounded,
and wrong when it is neither.

**Degrade.** Allow the session but restrict what it can do: lower limits, no
new payees, no credential changes, no export. The attacker learns something
happened but not what fired, which is a much weaker oracle. The cost is a
worse experience for false positives — and they will report it as a bug, which
is actually useful, because it gives you a channel to measure your false
positive rate rather than guessing at it.

**Delay.** Introduce latency into the decision — hold the action for review,
or simply do not resolve it in the same session. This attacks the iteration
loop directly: a feedback loop measured in hours instead of seconds is a
different economic proposition for the attacker. The cost is that latency is
felt by everyone, so it only works on actions where users already expect a
wait.

**Raise the cost of the action rather than refusing it.** Step-up
authentication, an out-of-band confirmation, a re-verification. This is often
the best of the four, because it is legible: the legitimate user understands
what is being asked and can comply, while the attacker faces a control that
does not live inside the process they own. The cost is friction, and friction
applied at the wrong moment loses conversions as surely as a block does.

The common thread is that all four decouple *detection* from *response*.
Detection happens on the device, where it is cheap and unreliable. Response
happens somewhere the attacker cannot watch, on evidence aggregated across
more than one session. If your architecture cannot express that separation —
if the check and the branch are in the same function — that is the thing to fix
before you add another check.

:::caution
Whatever you choose, make it non-deterministic in the attacker's view. If
response follows detection immediately and identically every time, you have
rebuilt the oracle regardless of which response you chose. Varying when and
how you act is what makes the loop expensive; it is also what makes the
behaviour harder for you to debug, and that trade is real.
:::

## What actually raises the cost

Here is the honest assessment the book has been building to.

A determined attacker with root on their own device will get code into your
process. Not might — will. There is no configuration of app-side checks that
prevents it, because every one of those checks is code that runs inside a
process the attacker already occupies, evaluated by a runtime the attacker can
modify, on hardware the attacker owns. If your security programme's objective
is "prevent this", the objective is not achievable and the budget spent on it
is being spent on a goal that cannot be met.

The achievable objectives are three, and they are the ones worth writing down
in place of prevention:

- **Cost.** How much work, skill, and time does the attacker need before their
  first success?
- **Detection latency.** How long between their first success and your
  knowing?
- **Blast radius.** How much can one successful attacker take, and how many
  other users does their success reach?

Measured against those, here is what moves the needle:

**Moves it.** Server-side authority over anything that matters — if the amount,
the entitlement, or the permission is decided by your backend, an attacker who
owns the client has taken the client and not the decision. Rate and value
limits that bound a single account's damage. Anomaly detection over
behavioural signals aggregated across sessions, which shortens detection
latency without giving the attacker anything to iterate against. Attestation
used as one input among several. Reducing what the client is *trusted* to
decide, which is the only measure on this list that shrinks blast radius
rather than merely raising cost. And, unglamorously: shipping updates quickly,
because every measure here decays, and the ability to change your checks
faster than an attacker can re-characterise them is worth more than any
individual check.

**Does not move it, whatever it is sold as.** Longer lists of root paths and
package names — cost to the attacker: minutes, one time. Obfuscation as a
substitute for server-side authority, rather than as a delay on top of it.
Client-side kill switches, which are one branch. Detection that blocks
immediately, which spends your false-positive users to buy the attacker a fast
iteration loop. And any product claim that a device-side SDK makes an app
tamper-proof: whatever such an SDK does, it does inside the same process,
under the same constraints, with the same second-move disadvantage as your own
code. It may be well-built and worth buying for the engineering it saves you.
It is not a different category of thing.

The uncomfortable version of all this: the defensive value of Part VI is not
that it lets you win. It is that it lets you spend accurately. Knowing that
`/proc/self/maps` catches the careless and nobody else means you can ship that
check in an afternoon and stop thinking about it, instead of building a
programme on it. Knowing that a determined attacker gets in means you design
so that getting in is worth less — bounded, logged, and reversible — rather
than designing on the assumption that they do not.

That is the argument the whole book has been making from the other side.
Chapter 2 said the boundary is not in the code. This is the same sentence read
by a defender: the boundary is not in your code either, and the sooner your
architecture stops pretending otherwise, the better it will hold.
