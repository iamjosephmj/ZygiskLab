---
title: "What changed at the boundary"
description: "The uid, SELinux, mount namespace, and filesystem changes at the postAppSpecialize boundary, and what they rule out."
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

- A before/after table: uid, SELinux context, mount namespace, capabilities, filesystem reachability
- What an injected app process can actually write — its own `/data/user/0/<pkg>/cache` — and what is denied: `/data/adb/*` (root + SELinux), scoped-storage paths under `/storage/emulated/0`
- Why this table dictates your whole architecture, and the designs it rules out
- The JVM is now usable; the classloader is not yet what you want (Ch. 13)

