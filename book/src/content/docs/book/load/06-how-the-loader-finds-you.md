---
title: "How the loader finds you"
description: "ABI selection, linker namespaces, what you may link against, and why a stray exported symbol is a footprint."
sidebar:
  order: 2
status: unverified
---

You are inside zygote. Nothing is an app yet.

## In this chapter

- ABI selection: `arm64-v8a`, `armeabi-v7a`, `x86_64` — one `.so` per ABI and what happens when the one for the running ABI is missing
- Linker namespaces: why your module is not linked the way an app's JNI library is, and what that restricts
- What you may safely link against, what you must `dlopen` yourself, and what will simply not resolve
- The C++ runtime question: why static linking is the default here
- Keeping the module small, and why size matters at this stage
- Symbol visibility, and why a stray exported symbol is a footprint (forward reference to Chapter 21)

