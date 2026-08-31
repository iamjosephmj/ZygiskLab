---
title: "How the loader finds you"
description: "ABI selection, linker namespaces, what you may link against, and why a stray exported symbol is a footprint."
sidebar:
  order: 2
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- ABI selection: `arm64-v8a`, `armeabi-v7a`, `x86_64` — one `.so` per ABI and what happens when the one for the running ABI is missing
- Linker namespaces: why your module is not linked the way an app's JNI library is, and what that restricts
- What you may safely link against, what you must `dlopen` yourself, and what will simply not resolve
- The C++ runtime question: why static linking is the default here
- Keeping the module small, and why size matters at this stage
- Symbol visibility, and why a stray exported symbol is a footprint (forward reference to Chapter 21)

