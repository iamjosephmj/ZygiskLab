---
title: "Hello, Zygisk"
description: "Lab 1: a one-file module that logs its pid and uid from inside a named app, packaged, installed, and rebooted into place."
sidebar:
  order: 4
status: unverified
---

What you are standing on before you write a line of module code.

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

