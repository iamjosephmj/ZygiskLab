---
title: "JNI inside a live app"
description: "Attaching a JNIEnv, solving the classloader problem, reflection helpers, reference hygiene, and exception discipline."
sidebar:
  order: 2
status: unverified
---

You are the app now. Everything you lost, you lost.

## In this chapter

- Getting a usable `JNIEnv` and attaching from a thread that is not the one you were called on
- The classloader problem: the system classloader cannot see the app's classes, so `FindClass` fails for exactly the classes you care about
- Getting a real app classloader: the routes, and when each becomes available
- Reflection from native code without losing your mind — a small helper pattern developed once and reused for the rest of the book
- Local vs. global references, and the leak that will bite you
- Exception discipline: checking and clearing, and what an uncleared pending exception does to the host app
- Hidden-API restrictions and how they present from native code

