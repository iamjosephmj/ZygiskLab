---
title: "Deploying without bricking zygote"
description: "Lab 2: why cp over a mapped .so corrupts zygote, the md5sum diagnostic, and the atomic mv-based safe deploy."
sidebar:
  order: 3
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

- The hazard, stated plainly: `cp` over a mapped `.so` truncates and rewrites the same inode while zygote is executing those pages
- The symptom, and why it is so misleading — SIGSEGV inside app specialization, only for processes your module actually touches, so it looks like a bug in your new code or like app-side defences. It is neither.
- The one-step diagnostic: `md5sum` on-device against the local build. Matching hashes mean the file is fine and the mapping is stale.
- The correct deploy: push to `/data/local/tmp`, `mv` into place (atomic rename gives a new inode and leaves existing mappings intact), reboot
- Writing to the module directory needs mount-master (`su -M`); plain `su -c` is denied even as root under KernelSU
- **Lab 2 deliverable:** a `deploy.sh` that is safe by construction, plus a deliberate reproduction of the corruption so the reader has seen the crash signature once, on a spare device
- The general rule this is an instance of: never write an artifact that is currently mapped

:::note[Lab 2]
This chapter carries [Lab 2](/ZygiskLab/labs/lab-02-safe-deploy/).
:::

