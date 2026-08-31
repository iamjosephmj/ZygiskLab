---
title: "Hooking native symbols"
description: "Lab 4: PLT/GOT hooking with pltHookRegister and pltHookCommit, what a PLT hook cannot reach, and debugging a silent hook."
sidebar:
  order: 3
status: unverified
---

You are the app now. Everything you lost, you lost.

## In this chapter

- The PLT/GOT hook: what it is, in a diagram, before any code
- `Api::pltHookRegister` and `Api::pltHookCommit` — the register/commit split and why it exists
- Choosing what to hook: which library, which symbol, and confirming the symbol is actually reached before you hook it
- Writing a trampoline that is safe on the caller's thread
- What a PLT hook *cannot* reach: internal calls that never go through the PLT, inlined code, and calls made before your hook was committed
- The deferred-hook problem: the library you want is not loaded yet, and what to do about it
- **Lab 4 deliverable:** hook a libc call in one target app, log it, and show a correct non-hooked control process
- Debugging a hook that never fires — a decision tree

:::note[Lab 4]
This chapter carries [Lab 4](/ZygiskLab/labs/lab-04-hooking-native-symbols/).
:::

