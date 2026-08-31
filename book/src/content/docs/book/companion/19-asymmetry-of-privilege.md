---
title: "The asymmetry of privilege"
description: "A capability table for companion versus injected process, and the status-reporting problem after specialization."
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

- A capability table: what the companion can do, what the injected process can do, and the narrow overlap
- Designing around it: pushing privileged work to the companion and keeping the injected side thin
- The status-reporting problem — with no root channel after specialization, live status has to be assembled from two independent writers and joined on the root side
- A root-side control plane: what a module manager WebUI can and cannot reach

