---
title: "Your footprint, stage by stage"
description: "The in-process, on-disk, and behavioural traces each earlier part leaves behind, walked back stage by stage."
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

- Walking back through Parts II–V and naming what each stage left behind
- In-process: your `.so` in `/proc/self/maps`, exported symbols, open file descriptors, threads you created, hooks you installed (a PLT entry that no longer points where it should)
- Process-level: the mount namespace, differences between your app's namespace and a clean one, environment and system properties
- On-disk: the module directory, the provider's own directories, manager app packages
- Behavioural: launch-time deltas, and the trace that is not a file
- The point of the chapter: the footprint is a consequence of design decisions made five parts ago, not something added at the end

