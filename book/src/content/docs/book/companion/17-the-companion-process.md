---
title: "The companion process"
description: "What the companion process is, REGISTER_ZYGISK_COMPANION, and the single channel it shares with your injected module."
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

- What the companion is: a separate process that stays root, forked from the Zygisk daemon rather than from your app
- `REGISTER_ZYGISK_COMPANION` and its lifetime
- What it inherits, what it shares with your injected module (almost nothing), and the one channel between them
- When it forks, how many of them exist, and what that means for state

