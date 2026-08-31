---
title: "The specialization window"
description: "What exists and does not exist in the preAppSpecialize window, and why your code must be nearly free to run."
sidebar:
  order: 1
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- What exists at this moment and what does not: no app classloader, no Application object, no app data directory, no app SELinux context
- What you still have that you are about to lose
- What you must not do here, and the consequences of each: heavy work delays every app launch; spawning threads does not survive the way you expect; touching the JVM early destabilises the fork
- The cost model: your code runs for *every* specialization, so the common path must be nearly free

