---
title: "JNI inside a live app"
description: "Attaching a JNIEnv, solving the classloader problem, reflection helpers, reference hygiene, and exception discipline."
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

- Getting a usable `JNIEnv` and attaching from a thread that is not the one you were called on
- The classloader problem: the system classloader cannot see the app's classes, so `FindClass` fails for exactly the classes you care about
- Getting a real app classloader: the routes, and when each becomes available
- Reflection from native code without losing your mind — a small helper pattern developed once and reused for the rest of the book
- Local vs. global references, and the leak that will bite you
- Exception discipline: checking and clearing, and what an uncleared pending exception does to the host app
- Hidden-API restrictions and how they present from native code

