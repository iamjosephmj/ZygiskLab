---
title: "Anatomy of a module"
description: "The ModuleBase interface, the Api handle, REGISTER_ZYGISK_MODULE, and the timeline of one app launch."
sidebar:
  order: 1
status: unverified
---

You are inside zygote. Nothing is an app yet.

## In this chapter

- The `zygisk::ModuleBase` interface in full: every callback, when each fires, and what is legal inside it
- `zygisk::Api`: the handle, its lifetime, and the operations it exposes
- `REGISTER_ZYGISK_MODULE` — what the macro actually expands to
- Module state: what persists between callbacks, what persists between processes, and what does not persist at all
- The order of events for one app launch, as a single annotated timeline diagram readers can return to

