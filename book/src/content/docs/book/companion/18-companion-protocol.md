---
title: "Designing the companion protocol"
description: "Lab 6: designing a length-prefixed, versioned companion protocol with timeouts and validation of every field."
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

- The socket you are handed, and the fact that everything above it is yours
- Framing: length-prefixed messages, and why the naive `write`/`read` pair fails the first time a message is split
- Designing opcodes you will not regret — versioning from message one
- Partial reads and writes, and a read-exactly/write-exactly helper
- Timeouts, and what your injected side does when the companion never answers
- Not trusting your own client: the companion is root and the caller is not, so validate every field
- **Lab 6 deliverable:** a request/response exchange where the app process asks the companion for something only root can read, with a rejected malformed request as the negative control

:::note[Lab 6]
This chapter carries [Lab 6](/ZygiskLab/labs/lab-06-companion-protocol/).
:::

