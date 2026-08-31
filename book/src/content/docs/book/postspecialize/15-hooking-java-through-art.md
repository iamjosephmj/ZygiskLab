---
title: "Hooking Java through ART"
description: "ART method hooking surveyed honestly: what it does, why it is fragile, and when to choose it over a native hook."
sidebar:
  order: 4
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- What ART method hooking actually does to a method entry point
- Why this is the most fragile technique in the book: it depends on ART internals that change between releases and are not API
- Survey of the approaches and existing implementations, with an honest assessment of each
- The artifact-is-mapped hazard again, in its Java form: force-stop the app before rewriting anything it has loaded
- When to reach for this and when a native hook or an app-side approach is the better engineering choice

