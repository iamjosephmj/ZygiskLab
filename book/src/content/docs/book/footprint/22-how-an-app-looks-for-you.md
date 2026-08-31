---
title: "How an app looks for you"
description: "How an app inspects its own maps, mount namespace, loaded libraries, tracer presence, and filesystem for signs of you."
sidebar:
  order: 2
status: unverified
---

The traces you left, stage by stage — and how apps look for them.

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
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

