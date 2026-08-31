# 01 — Hello, Zygisk

**Lab 1.** The smallest module that proves it loaded, and proves *where* it
loaded.

## What it proves

Three log lines, in this order, for each armed app launch:

1. `onLoad` — the module's library is in the process
2. `preAppSpecialize` — still the zygote fork; prints the target `nice_name`
3. `postAppSpecialize` — now the app; the uid has changed

Seeing all three, with the uid changing between lines 2 and 3, is the proof
that you ran inside a specific app process rather than in zygote.

## Build

```bash
./build.sh              # -> out/01-hello-zygisk.zip
```

## Install

Flash `out/01-hello-zygisk.zip` in your root manager and reboot.

For subsequent iterations use `./deploy.sh`, which installs safely — see
Chapter 7 for why `cp` over a live module bricks zygote.

## Known tradeoff: module size

`libzygisklab.so` is ~232KB (232312 bytes) for a module that only logs three
lines. That's the C++ runtime, not the log calls: `zygisk.hpp`'s
`REGISTER_ZYGISK_MODULE` macro expands into function-local statics, which
need `__cxa_guard_acquire`/`__cxa_guard_release` from the C++ runtime -
`APP_STL := none` can't link at all (see the comment in
`jni/Application.mk`), so this module statically links `c++_static`.
Static-archive linking pulls in libc++abi at `.o` granularity, leaving
`.eh_frame` / `.gcc_except_table` / `.text` sections that
`-Wl,--gc-sections` can't trim even with `-fno-exceptions -fno-rtti`.
Stripping the binary barely moves the number. Chapter 6 revisits module size
and what actually shrinks it.

## Watch

```bash
adb logcat -s ZygiskLab
```
