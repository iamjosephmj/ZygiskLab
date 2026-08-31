---
title: "A detection harness"
description: "Lab 7: an Android app that runs the Chapter 22 checks on itself and scores Labs 1-6's modules against the results."
sidebar:
  order: 4
status: unverified
---

The traces you left, stage by stage — and how apps look for them.

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
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

