---
title: "The defensive chapter"
description: "What app-side detection is worth doing, what is theatre, and why defence in depth beats blocking on the first hit."
sidebar:
  order: 5
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- Written for the app author, not the module author
- What is worth checking, ordered by cost-to-benefit
- What is theatre: checks that are trivially defeated, expensive, or that fire on legitimate users
- Defence in depth: why no single check is the answer, and where server-side signals belong
- Failing well: what to do when a check fires, and why blocking immediately is usually the wrong response
- What genuinely raises the cost for an attacker, honestly assessed

