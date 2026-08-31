---
title: "Choosing not to run"
description: "Lab 3: arming a module for one package only, and measuring the cost the unarmed path imposes on every other launch."
sidebar:
  order: 4
status: unverified
---

The window: forked from zygote, still root-ish, not yet the app.

## In this chapter

- The default posture: decide fast, return, and leave no trace in processes you do not care about
- The real cost of staying resident in every app on the device — launch latency, memory, and detection surface
- Structuring the decision so the not-interested path is the cheapest path
- Where the arming configuration lives, and why reading a file here is a design decision rather than a detail
- **Lab 3 deliverable:** a module armed for one package, with a measurement showing the unarmed path's cost on other app launches

:::note[Lab 3]
This chapter carries [Lab 3](/ZygiskLab/labs/lab-03-choosing-not-to-run/).
:::

