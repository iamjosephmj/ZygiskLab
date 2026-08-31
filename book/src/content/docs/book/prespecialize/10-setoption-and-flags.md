---
title: "`setOption` and the flags"
description: "FORCE_DENYLIST_UNMOUNT and DLCLOSE_MODULE_LIBRARY: what each flag does, and its effect on your footprint."
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

- `FORCE_DENYLIST_UNMOUNT` — what it unmounts, when it takes effect, and what it does not hide
- `DLCLOSE_MODULE_LIBRARY` — unloading yourself, and the exact rules about what may still run afterwards
- Interaction between the flags and the provider's own denylist
- What each flag does to your footprint (forward reference to Part VI)

