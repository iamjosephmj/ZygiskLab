---
title: "Reading `AppSpecializeArgs`"
description: "Reading AppSpecializeArgs fields safely, identifying your target process, and matching one process of one package."
sidebar:
  order: 2
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- Every field, what it means, and which ones are safe to read
- Identifying your target: `nice_name` vs. uid vs. app data dir — the trade-offs, and why the obvious choice is the wrong one
- Reading a JNI string safely in this context
- Multi-process apps: `:remote` processes, and why a package match is not a process match
- **Worked example:** arm on exactly one process of one package, prove the match, and prove the non-match

