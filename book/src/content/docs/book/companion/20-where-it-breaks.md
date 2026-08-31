---
title: "Where it breaks"
description: "exemptFd and connectCompanion failure modes, provider differences, version drift, and designs that look obvious but fail."
sidebar:
  order: 4
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- `Api::exemptFd` — returns false under some providers, so you cannot rely on carrying a descriptor across specialization
- `connectCompanion` after specialization: refused
- Provider differences that actually change your design (Magisk vs. Zygisk Next), and how to detect which you are running on
- Version drift: what breaks when the provider updates, and how to fail loudly instead of silently
- A short catalogue of designs that look obvious and do not work, each with the reason

