---
title: "Threading and timing"
description: "Lab 5: getting onto the main thread safely, waiting for app readiness, and proving which thread your code ran on."
sidebar:
  order: 5
status: unverified
---

You are the app now. Everything you lost, you lost.

## In this chapter

- The main-thread problem: your code runs early and off-thread, and almost everything interesting in an Android app must happen on the main thread
- Routes onto the main thread, with the trade-offs of each
- Waiting for the app to be ready without polling and without racing it
- Doing work off the main thread without ANRing the host
- **Lab 5 deliverable:** perform a main-thread-only action from an injected module, at a moment you chose, with proof of the thread you were on

:::note[Lab 5]
This chapter carries [Lab 5](/ZygiskLab/labs/lab-05-threading-and-timing/).
:::

