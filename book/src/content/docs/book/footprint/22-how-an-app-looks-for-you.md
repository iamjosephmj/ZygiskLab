---
title: "How an app looks for you"
description: "How an app inspects its own maps, mount namespace, loaded libraries, tracer presence, and filesystem for signs of you."
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

- Self-inspection: reading its own `maps`, `status`, `fd`, and what a scan actually catches
- Mount namespace inspection: comparing against what the app expects
- Loaded-library enumeration, and integrity checks over its own hooks
- `ptrace` and tracer-presence checks
- Filesystem probes for known root and provider paths
- Property and package checks
- Platform attestation, and where it sits relative to all of the above
- For each: what it costs the app, what it catches, and its false-positive rate

