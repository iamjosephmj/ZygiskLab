---
title: "Anatomy of a module"
description: "The ModuleBase interface, the Api handle, REGISTER_ZYGISK_MODULE, and the timeline of one app launch."
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

- The `zygisk::ModuleBase` interface in full: every callback, when each fires, and what is legal inside it
- `zygisk::Api`: the handle, its lifetime, and the operations it exposes
- `REGISTER_ZYGISK_MODULE` — what the macro actually expands to
- Module state: what persists between callbacks, what persists between processes, and what does not persist at all
- The order of events for one app launch, as a single annotated timeline diagram readers can return to

