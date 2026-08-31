---
title: "Rules of engagement"
description: "Authorized-use framing, responsible disclosure, and the legal and ethical boundaries this book holds throughout."
sidebar:
  order: 2
status: unverified
---

A Zygisk module is code you write that runs inside applications you did not write. That is the whole point of the technique, and it is also the entire ethical problem with it. Every other chapter in this book assumes you have already settled the question this chapter asks: whose process are you about to be inside, and on what basis?

The technical answer is uncomfortably short. Nothing in Zygisk distinguishes your own debug build from a banking app; `preAppSpecialize` fires for both, and the `nice_name` you match on is a string. If your module's filter says `com.example.myapp`, that is a decision you made in source, not a boundary the platform enforces for you. Change the string and the same code runs somewhere else, with the same privileges, at the same moment in the lifecycle.

The boundary is not in the code, then. It is in what you are authorized to do — which has to be stated rather than assumed, and this chapter states it once for the whole book.

## What authorization actually looks like

Not a feeling that what you are doing is fine. A specific, nameable basis you could point to if someone asked.

**Your own device and your own app.** The default case, and the one this book is written around. You built the APK, you signed it, you flashed the ROM, you own the hardware. The reference rig in this book — Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — is a device with nothing on it that belongs to anyone else. Every lab in this book targets a sample app whose source ships alongside it, precisely so that the default path through the material never requires you to touch someone else's software.

**An engagement with written scope.** A client hired you, and a document says which applications, versions, environments and time window. That document is the authorization; a verbal "go ahead" from an engineer on a call is not, because that person may not be the one who can grant it. If the scope says staging, staging is what you get. If it does not mention the mobile app, the mobile app is out of scope however interesting it looks.

**A bug bounty programme whose terms you have actually read.** Read them, not skimmed. Programmes vary on exactly the questions that matter here: whether reverse engineering the client is permitted at all, whether rooted or modified devices are in scope, whether you may test against production, whether you must use a test account they issue. Some explicitly exclude findings that require a rooted device or a hooking framework — worth knowing before you spend a weekend on one.

**A CTF, a lab, or a deliberately vulnerable target.** Built to be attacked, by someone who wanted it attacked.

And what it is not:

- Someone else's account, even with their password, even if they said it was fine. Their say-so binds them, not the service.
- A production service you do not own, on the theory that you are only looking.
- An app whose terms you have not read. "I did not know" describes your state of mind, not a defence.
- "Just testing" against a live third party. The word "just" is doing no work in that sentence.

When the formal answer is unclear, the working heuristic is the report: if you would not be comfortable writing down what you did and sending it to the vendor, do not do it.

## Why this weighs more here than in most Android writing

Most Android security writing studies a file. You decompile an APK, read it, reason about it, write up what you found. Nothing runs.

Zygisk is not that. Three properties stack up, and the combination is what matters:

**Your code runs inside the target process.** Not alongside it, not watching from a debugger with its own PID. Inside — sharing the address space, the file descriptors, and, after specialization, the app's uid, SELinux context and data directory. Anything the app can reach, you can reach, because as far as the kernel is concerned you *are* the app.

**It runs before the app's code does.** `preAppSpecialize` is called while the process is still zygote-privileged, before the app's `Application` class exists and before any of its own integrity checks can observe anything. Chapter 3 walks the lifecycle in detail; the ethically relevant part is that the app never gets first move.

**It is indiscriminate by construction.** Zygote forks every app, and your module is loaded into every one of those forks. The only thing keeping it out of processes you did not intend is your own filtering logic. A typo in a package name is not a compile error; it is a module attached to something it should not have been.

Together: the mechanism that instruments your own build is byte-for-byte the mechanism that instruments someone else's. The line between them is authorization and intent, and nothing in the platform will draw it for you. A reader who works through Part III will be able to hook JNI entry points in an arbitrary process. That capability arrives whether or not anyone framed it first — so it gets framed first.

## What this book does not contain

Part VI is about detection: how apps notice that something like Zygisk is present, what the published solutions do about it, and how to measure your own module's footprint. It goes into real depth on mechanism — maps and mounts, the properties an app can read, timing, the artefacts a module leaves in a process it has entered. It surveys what denylists, Shamiko, and Zygisk Next's own hiding design remove, and — more usefully — what they demonstrably cannot. It gives you a harness so you can measure your own traces rather than trusting a checklist.

It ships no novel evasion tooling, and no recipe aimed at defeating a particular product's checks. That is a choice, and the reasoning is worth stating plainly rather than leaving as an implied apology.

Mechanisms generalise; bypasses do not. Understanding *why* a mounted overlay is visible to a process reading its own `/proc/self/mountinfo` survives the next SDK release, the next attestation change and the next detection library. A specific sequence that gets past one version of one app teaches you one version of one app: stale within a release cycle, non-transferable, and no help at all with the case actually in front of you. Mechanism is also what a defender needs — you cannot harden an app against something you do not understand — while a targeted bypass has a much narrower set of uses, most of them the thing this book is not for.

So if you came here for a working bypass of a named app's root or integrity checks, it is not in a later chapter either. What is here is enough understanding that you could reason about the problem yourself, which is deliberately a different thing.

:::caution
This applies to the labs as well as the prose. Every lab targets the sample app shipped with the book, or your own build. If you retarget a lab at third-party software, the authorization for that is yours to establish, and the framing above is the standard the book holds you to.
:::

## When you find something real

Sooner or later, on legitimately scoped work, you will find a genuine weakness in software someone else maintains. Here is how to handle it without turning a good finding into a bad situation.

**Stop where the proof is.** You need enough to show the issue exists and what it costs. You do not need to see how far it goes. If you can read one record you were not entitled to, that is the finding — do not enumerate the table to prove it scales. If you got a token, do not spend it. Every step past the minimum demonstration adds risk for you, harm for them, and nothing to the report. Do not touch other users' data; if you have already seen some, say so and do not retain it.

**Find the real contact before you publish anything.** In rough order of reliability:

- `/.well-known/security.txt` on the vendor's primary domain (RFC 9116) — the closest thing to a standard, and it names the intended channel directly.
- An existing bug bounty or VDP page, on their site or on a platform. Their terms usually specify format, channel, and expected timelines.
- A security page or `security@` address on the vendor's domain.
- If none of those exist: a named contact through a support channel, asking to be routed to whoever handles security reports. Do not put the details in the first message to a general support queue.

**Write a report the engineer on the other end can act on:**

- A reproduction that actually reproduces, step by step, from a stated starting state.
- The environment: device, OS version, app version and build, root solution and version, module versions. For anything involving Zygisk this is not optional — behaviour differs between root solutions, and a report that omits it gets filed as unreproducible.
- Impact, concrete and uninflated. What can an attacker do, and what do they need to do it? If exploitation requires a rooted device with a hooking framework installed, say so — that is part of the finding, and hiding it costs you credibility on every later report.
- Affected versions, and whether you checked older or newer ones.
- What you did *not* test, so nobody assumes coverage you did not provide.

Honest severity beats dramatic severity. Vendors remember which reporters oversold.

**Give a remediation window.** Ninety days from first contact is the widely used coordinated-disclosure default and a reasonable starting point; where a programme specifies its own, use theirs. Mobile is often slower than server-side, because a fix has to clear app-store review and then reach installed devices — so extend for a vendor who is visibly working. Extend for progress, not for silence. If the terms you submitted under say the finding stays confidential, that is a term you agreed to.

**Keep the record.** Dates, what you sent, what came back. If a disclosure goes wrong, the timeline is what you have.

## The legal part

This is not legal advice. I am an engineer, not a lawyer.

Computer misuse and unauthorized-access law varies substantially by jurisdiction, and the operative questions are usually about authorization and intent — precisely what this chapter has spent its length on. Terms of service, licence agreements and programme rules add another layer, and can restrict things the law alone would not. If you are working commercially, on someone else's systems, or anywhere near a grey area, get advice from someone qualified where you are, before rather than after.

The techniques here are for education and authorized testing, and are provided as-is, without warranty of any kind. Zygisk modules run with elevated privileges inside a system-critical process: a bad module bootloops the device, and recovery can mean a full wipe. Test on hardware you can afford to reflash. Nothing here is a promise that a technique is safe, current, or lawful in your situation — that determination is yours.

## Before you continue

A short check against whatever you are about to do:

- Can you name your authorization specifically — a device you own, an app you built, a scope document, a programme's terms?
- If it is a programme or an engagement, have you read the terms rather than assumed them?
- Does your module's process filter match only what you intend, verified rather than trusted?
- If you find something real, do you know where you would send it?
- Would you be comfortable describing what you are about to do, in plain words, to the vendor?

If all five have answers, the rest of the book is yours. Chapter 3 starts where the interesting part does: inside zygote, before anything is an app yet.
