---
title: "The companion process"
description: "What the companion process is, REGISTER_ZYGISK_COMPANION, and the single channel it shares with your injected module."
sidebar:
  order: 1
status: unverified
---

The companion, and the asymmetry it exists to solve.

## In this chapter

- What the companion is: a separate process that stays root, forked from the Zygisk daemon rather than from your app
- `REGISTER_ZYGISK_COMPANION` and its lifetime
- What it inherits, what it shares with your injected module (almost nothing), and the one channel between them
- When it forks, how many of them exist, and what that means for state

