---
title: "Hello, Zygisk"
description: "Lab 1: a one-file module that logs its pid and uid from inside a named app, packaged, installed, and rebooted into place."
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

- The smallest module that proves it loaded — one file, one log line
- Line-by-line: the header, the class, `onLoad`, `REGISTER_ZYGISK_MODULE`
- `Android.mk` and `Application.mk` explained field by field
- Packaging: the zip layout the manager expects
- Installing, rebooting, and finding your log line
- **Lab 1 deliverable:** a log line from inside a named app's process, with the pid and uid printed, proving you ran inside *that* app and not zygote
- Failure catalogue: module not listed, listed but silent, log line appears for every process, device bootloops

:::note[Lab 1]
This chapter carries [Lab 1](/ZygiskLab/labs/lab-01-hello-zygisk/).
:::

