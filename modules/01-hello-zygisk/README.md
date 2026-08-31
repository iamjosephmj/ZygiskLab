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

## Watch

```bash
adb logcat -s ZygiskLab
```
