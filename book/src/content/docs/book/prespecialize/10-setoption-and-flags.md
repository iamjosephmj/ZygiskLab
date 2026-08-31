---
title: "`setOption` and the flags"
description: "FORCE_DENYLIST_UNMOUNT and DLCLOSE_MODULE_LIBRARY: what each flag does, and its effect on your footprint."
sidebar:
  order: 3
status: unverified
---

The window: forked from zygote, still root-ish, not yet the app.

## In this chapter

- `FORCE_DENYLIST_UNMOUNT` — what it unmounts, when it takes effect, and what it does not hide
- `DLCLOSE_MODULE_LIBRARY` — unloading yourself, and the exact rules about what may still run afterwards
- Interaction between the flags and the provider's own denylist
- What each flag does to your footprint (forward reference to Part VI)

