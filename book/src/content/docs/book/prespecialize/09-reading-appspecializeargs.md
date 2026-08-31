---
title: "Reading `AppSpecializeArgs`"
description: "Reading AppSpecializeArgs fields safely, identifying your target process, and matching one process of one package."
sidebar:
  order: 2
status: unverified
---

The window: forked from zygote, still root-ish, not yet the app.

## In this chapter

- Every field, what it means, and which ones are safe to read
- Identifying your target: `nice_name` vs. uid vs. app data dir — the trade-offs, and why the obvious choice is the wrong one
- Reading a JNI string safely in this context
- Multi-process apps: `:remote` processes, and why a package match is not a process match
- **Worked example:** arm on exactly one process of one package, prove the match, and prove the non-match

