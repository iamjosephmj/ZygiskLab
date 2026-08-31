---
title: "What Zygisk is, and what it is not"
description: "Zygote, specialization, where Zygisk inserts itself, and how it compares to Xposed, Frida, and LD_PRELOAD."
sidebar:
  order: 1
status: unverified
---

What you are standing on before you write a line of module code.

## In this chapter

- Zygote: the pre-warmed process every Android app is forked from, and why that fork is the highest-leverage moment in the system
- Specialization: how a generic zygote fork becomes a specific app process (uid, seinfo, nice_name, mount namespace, capability drop)
- Where Zygisk inserts itself, and who provides it (Magisk's built-in implementation vs. Zygisk Next as a standalone provider on KernelSU)
- The comparison table readers actually want: Zygisk vs. Xposed/LSPosed vs. Frida vs. `LD_PRELOAD` vs. repackaging — injection point, persistence, privilege, detectability, and what each cannot do
- What Zygisk is *not*: not a hooking framework. It gets you code execution in the right process at the right moment; the hooking is yours to bring.
- Honest limits up front: no kernel access, no SELinux bypass, arch-specific, breaks with provider updates

