---
title: "Where it breaks"
description: "exemptFd and connectCompanion failure modes, provider differences, version drift, and designs that look obvious but fail."
sidebar:
  order: 4
status: unverified
---

The companion, and the asymmetry it exists to solve.

## In this chapter

- `Api::exemptFd` — returns false under some providers, so you cannot rely on carrying a descriptor across specialization
- `connectCompanion` after specialization: refused
- Provider differences that actually change your design (Magisk vs. Zygisk Next), and how to detect which you are running on
- Version drift: what breaks when the provider updates, and how to fail loudly instead of silently
- A short catalogue of designs that look obvious and do not work, each with the reason

