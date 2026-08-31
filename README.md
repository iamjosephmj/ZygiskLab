# ZygiskLab

[![Deploy to GitHub Pages](https://github.com/iamjosephmj/ZygiskLab/actions/workflows/deploy.yml/badge.svg)](https://github.com/iamjosephmj/ZygiskLab/actions/workflows/deploy.yml)

A book on writing Zygisk modules — from a hello-world that only proves it
loaded, through deep injection inside a live app process, to the traces a
module leaves behind and how apps look for them.

**[Read the book →](https://iamjosephmj.github.io/ZygiskLab)**

Six parts, 25 chapters, 7 labs. The spine is the process lifecycle: module
load, `preAppSpecialize`, `postAppSpecialize`, the root-side companion, and
finally your own footprint.

> **Authorized use only.** This material is for education and for security
> work on devices and applications you own or have written permission to
> assess. See Chapter 2, Rules of Engagement.
>
> **Not legal advice.** Provided as-is, without warranty of any kind. You are
> responsible for complying with the law in your jurisdiction.

## What is in here

| | |
|---|---|
| `book/` | The book itself — Astro + Starlight, published to GitHub Pages |
| `modules/` | One buildable example module per lab |

## Reference rig

Every procedure in this book is written against, and verified on, one rig:

- Pixel 6 Pro, Android 16, arm64
- KernelSU-Next 3.3.0
- Zygisk Next 1.4.5

Magisk differences appear as call-outs where behaviour actually diverges; they
are not separately verified.

## Verification

Each chapter is marked **proven** or **unverified**. *Proven* means the
procedure was run on the rig above. A chapter is never promoted to *proven*
because the code compiles.

## Building the book

```bash
cd book
npm ci
npm run dev
```

## Building a module

```bash
cd modules/01-hello-zygisk
./build.sh          # produces out/01-hello-zygisk.zip
```

Requires the Android NDK; set `ANDROID_NDK_HOME` or have `ndk-build` on PATH.

## Licence

MIT. See [LICENSE](LICENSE).
