---
title: "A detection harness"
description: "Lab 7: an Android app that runs the Chapter 22 checks on itself and scores Labs 1-6's modules against the results."
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

- Building an Android app that inspects itself using the Chapter 22 checks and reports what it finds — a real app process, because a root shell sees a different world than an app does
- Harness structure: one check per module, each returning evidence rather than a boolean, so results are readable and arguable
- Establishing a clean baseline on an unmodified process first
- Measuring your own modules from Labs 1–6 against it
- Reading the results: which of your design decisions produced which finding
- **Lab 7 deliverable:** a table of your own modules against the check matrix, with the specific line of your code responsible for each hit
- The value of measuring rather than assuming

:::note[Lab 7]
This chapter carries [Lab 7](/ZygiskLab/labs/lab-07-detection-harness/).
:::

