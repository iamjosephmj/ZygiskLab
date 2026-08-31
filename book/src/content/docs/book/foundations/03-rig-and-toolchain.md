---
title: "The rig and the toolchain"
description: "The Pixel 6 Pro reference rig, NDK setup, module.prop fields, on-device paths, and recovering from a bootloop."
sidebar:
  order: 3
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- Reference rig stated once: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and how to verify each piece is actually active
- Why a *spare* device, not your daily driver
- NDK setup, the API level you target and why it is not the newest one
- `module.prop`: every field, which ones the manager displays, which ones the loader reads
- The module directory on device, and what each path in it means
- Reading logs that matter: `logcat` filtered to the Zygisk provider, and where a crash in your module actually surfaces
- Recovering a device that will not boot because of your module

