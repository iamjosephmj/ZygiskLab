---
title: "Existing answers, surveyed"
description: "The provider denylist, Shamiko, and Zygisk Next's own footprint choices, each assessed for what it actually removes."
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

- The provider denylist: what it is and what it actually does
- `FORCE_DENYLIST_UNMOUNT` revisited with Part VI eyes
- Shamiko, and the class of traces it addresses
- Zygisk Next's own design choices around footprint
- For each: what it removes, what it does not remove, and how a reader can check rather than take it on faith
- The honest conclusion — what none of them remove, and why attestation is a different kind of problem from process-level hiding

