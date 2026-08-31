# ZygiskLab Scaffold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the ZygiskLab book — a buildable Starlight site with all 25 chapter stubs, 7 lab stubs, a working Lab 1 module, and automated Pages deployment — so every later session writes prose into a structure that already builds and publishes.

**Architecture:** A single `chapters.json` manifest is the source of truth for the book's structure. A generator script reads it and emits both the Starlight sidebar config and any missing chapter stub files, so the navigation and the content can never drift apart. Chapters are Markdown with frontmatter carrying a `status` field (`unverified` / `proven`) rendered as a visible banner. Example modules live outside the site in `modules/`, built with `ndk-build`, one per lab.

**Tech Stack:** Astro 6, @astrojs/starlight 0.38, Node 20 (local) / 22 (CI), sharp, Android NDK 29.0.14206865, `ndk-build`, GitHub Actions → GitHub Pages.

**Spec:** `docs/superpowers/specs/2026-08-31-zygisklab-design.md`

## Global Constraints

- Repository root: `~/IdeaProjects/ZygiskLab`. All paths below are relative to it.
- Site lives in `book/`; `astro.config.mjs` must set `site: 'https://iamjosephmj.github.io'` and `base: '/ZygiskLab'`.
- Dependencies pinned to `astro@^6.0.1`, `@astrojs/starlight@^0.38.1`, `sharp@^0.34.2`. Do not add other dependencies.
- Reference rig — wherever the rig is named, these five values appear exactly, in whatever layout suits the surrounding prose (prose, list, or table): **Pixel 6 Pro**, **Android 16**, **arm64**, **KernelSU-Next 3.3.0**, **Zygisk Next 1.4.5**. What is binding is the values, not the punctuation between them.
- Every chapter file's frontmatter carries `status: unverified`. Nothing is set to `proven` in this plan — that flag is only ever set after the author reports an on-device result.
- No frametap code, filenames, or references anywhere in this repository. Lessons are re-derived generically.
- Part VI and its labs must not contain novel evasion tooling. Mechanisms and detection only.
- `ndk-build` for all example modules. The single exception is `modules/07-detection-harness/app/`, which is a Gradle Android app.
- Commit after every task. Commit messages use the imperative mood and no emoji.

---

### Task 1: Repository skeleton and licence

**Files:**
- Create: `README.md`
- Create: `LICENSE`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: a repository root that later tasks add to. No code interfaces.

- [ ] **Step 1: Write `.gitignore`**

```gitignore
node_modules/
book/dist/
book/.astro/
modules/**/libs/
modules/**/obj/
modules/**/out/
modules/**/*.zip
.DS_Store
*.local
.gradle/
build/
local.properties
```

- [ ] **Step 2: Write `LICENSE`**

Use the MIT licence text, copyright line: `Copyright (c) 2026 Joseph James`.

- [ ] **Step 3: Write `README.md`**

```markdown
# ZygiskLab

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
```

- [ ] **Step 4: Verify the tree**

Run: `ls -a ~/IdeaProjects/ZygiskLab`
Expected: `.gitignore`, `LICENSE`, `README.md`, `docs/`, `.git/` present.

- [ ] **Step 5: Commit**

```bash
git add README.md LICENSE .gitignore
git commit -m "Add repository skeleton, README, and licence"
```

---

### Task 2: Starlight site that builds

**Files:**
- Create: `book/package.json`
- Create: `book/astro.config.mjs`
- Create: `book/tsconfig.json`
- Create: `book/src/styles/custom.css`
- Create: `book/src/content/docs/index.mdx`
- Create: `book/public/.gitkeep`

**Interfaces:**
- Consumes: nothing.
- Produces: a `book/` Astro project where `npm run build` succeeds. Task 4 replaces the placeholder `sidebar` array in `astro.config.mjs` with generated content; it must remain a top-level `sidebar:` key inside the `starlight({...})` call for that edit to apply cleanly.

- [ ] **Step 1: Create the project directory and `package.json`**

```json
{
  "name": "zygisklab-book",
  "type": "module",
  "version": "0.0.1",
  "scripts": {
    "dev": "astro dev",
    "start": "astro dev",
    "build": "astro build",
    "preview": "astro preview",
    "astro": "astro",
    "gen": "node scripts/generate.mjs"
  },
  "dependencies": {
    "@astrojs/starlight": "^0.38.1",
    "astro": "^6.0.1",
    "sharp": "^0.34.2"
  }
}
```

- [ ] **Step 2: Write `book/astro.config.mjs`**

```js
// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://iamjosephmj.github.io',
  base: '/ZygiskLab',
  integrations: [
    starlight({
      title: 'ZygiskLab',
      description:
        'Writing Zygisk modules: from hello-world, through injection inside a live app process, to the traces a module leaves behind.',
      customCss: ['./src/styles/custom.css'],
      social: [
        { icon: 'github', label: 'GitHub', href: 'https://github.com/iamjosephmj/ZygiskLab' },
      ],
      head: [
        { tag: 'meta', attrs: { name: 'theme-color', content: '#161616' } },
      ],
      sidebar: [],
    }),
  ],
});
```

- [ ] **Step 3: Write `book/tsconfig.json`**

```json
{
  "extends": "astro/tsconfigs/strict",
  "include": [".astro/types.d.ts", "**/*"],
  "exclude": ["dist"]
}
```

- [ ] **Step 4: Write `book/src/styles/custom.css`**

```css
:root {
  --sl-font: ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
  --sl-font-mono: ui-monospace, "JetBrains Mono", "SF Mono", Menlo, monospace;
}

/* Chapter verification banner. Set by the `status` frontmatter field. */
.zl-status {
  display: inline-block;
  font-size: 0.8rem;
  font-weight: 600;
  letter-spacing: 0.02em;
  text-transform: uppercase;
  padding: 0.15rem 0.5rem;
  border-radius: 0.25rem;
  margin-bottom: 1rem;
}
.zl-status[data-status='proven'] {
  background: var(--sl-color-green-low);
  color: var(--sl-color-green-high);
}
.zl-status[data-status='unverified'] {
  background: var(--sl-color-orange-low);
  color: var(--sl-color-orange-high);
}
```

- [ ] **Step 5: Write `book/src/content/docs/index.mdx`**

```mdx
---
title: ZygiskLab
description: Writing Zygisk modules, from hello-world to the traces you leave behind.
template: splash
hero:
  tagline: A book on writing Zygisk modules — from a hello-world that only proves it loaded, to the traces a module leaves and how apps look for them.
  actions:
    - text: Start reading
      link: /ZygiskLab/book/foundations/01-what-zygisk-is/
      icon: right-arrow
    - text: GitHub
      link: https://github.com/iamjosephmj/ZygiskLab
      icon: external
      variant: minimal
---

import { Card, CardGrid } from '@astrojs/starlight/components';

## The spine

Zygisk is a narrow window into a process that has not become an app yet.
Almost every difficulty is a question of *when* your code runs, so this book
is organised by execution stage rather than by feature.

<CardGrid>
  <Card title="Part I — Foundations">What Zygisk is, the rig, and a module that proves it loaded.</Card>
  <Card title="Part II — The load stage">You are inside zygote. How you got there, and how to deploy without bricking it.</Card>
  <Card title="Part III — preAppSpecialize">Forked, but not yet an app. The narrowest and most dangerous window.</Card>
  <Card title="Part IV — postAppSpecialize">You are the app now. JNI, classloaders, hooks, and threads.</Card>
  <Card title="Part V — The root side">The companion process, and the asymmetry of privilege it exists to solve.</Card>
  <Card title="Part VI — Footprint">The traces you left at every stage, and how an app looks for them.</Card>
</CardGrid>

## Authorized use only

This material is for education and for security work on devices and
applications you own or have written permission to assess. See
[Rules of Engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).

Provided as-is, without warranty of any kind.
```

- [ ] **Step 6: Install and build**

Run:
```bash
cd book && npm install && npm run build
```
Expected: install succeeds and the build FAILS, because `index.mdx` links to
`01-what-zygisk-is`, which does not exist until Task 4. Record the error text;
this is the failing state Task 4 fixes.

- [ ] **Step 7: Commit**

```bash
git add book/package.json book/package-lock.json book/astro.config.mjs book/tsconfig.json book/src book/public
git commit -m "Scaffold the Starlight site"
```

---

### Task 3: The chapter manifest

**Files:**
- Create: `book/chapters.json`

**Interfaces:**
- Consumes: the chapter outline in the spec, verbatim.
- Produces: `book/chapters.json`, the single source of truth consumed by Task 4's generator. Exact shape:

```json
{
  "parts": [
    {
      "id": "foundations",
      "label": "Part I: Foundations",
      "blurb": "What you are standing on before you write a line of module code.",
      "chapters": [
        {
          "num": 1,
          "slug": "01-what-zygisk-is",
          "title": "What Zygisk is, and what it is not",
          "description": "Zygote, specialization, where Zygisk inserts itself, and how it compares to Xposed, Frida, and LD_PRELOAD.",
          "lab": null,
          "outline": ["...", "..."]
        }
      ]
    }
  ],
  "labs": [
    { "num": 1, "slug": "lab-01-hello-zygisk", "title": "Hello, Zygisk", "chapter": 4, "module": "01-hello-zygisk", "deliverable": "A log line from inside a named app's process, printing pid and uid." }
  ]
}
```

Field contracts, relied on by Task 4:
- `parts[].id` — directory name under `src/content/docs/book/`
- `chapters[].slug` — filename without extension, and the last path segment of the Starlight slug
- `chapters[].outline` — array of strings; each becomes one bullet in the stub's "In this chapter" list. Copy these verbatim from the spec's sub-bullets.
- `chapters[].lab` — the lab number this chapter carries, or `null`
- `labs[].module` — directory name under `modules/`, or `null`

- [ ] **Step 1: Write the manifest**

Transcribe all six parts, 25 chapters, and 7 labs from the spec's "Chapter outline" section into the shape above. Part ids in order: `foundations`, `load`, `prespecialize`, `postspecialize`, `companion`, `footprint`, `appendices`. The appendices part uses letter slugs: `a-api-reference`, `b-troubleshooting`, `c-cheatsheet`, `d-glossary`, `e-further-reading`, and its entries carry `"num": null`.

Every `outline` array must be non-empty — an empty one means a chapter was transcribed without its contents, which is the failure this manifest exists to prevent.

- [ ] **Step 2: Validate it parses and is complete**

Run:
```bash
cd book && node -e '
const m = JSON.parse(require("fs").readFileSync("chapters.json","utf8"));
const ch = m.parts.flatMap(p => p.chapters);
const numbered = ch.filter(c => c.num !== null);
console.log("parts", m.parts.length, "chapters", ch.length, "numbered", numbered.length, "labs", m.labs.length);
const bad = ch.filter(c => !c.outline || c.outline.length === 0);
if (bad.length) { console.error("EMPTY OUTLINE:", bad.map(c => c.slug)); process.exit(1); }
const nums = numbered.map(c => c.num);
const expected = Array.from({length: 25}, (_, i) => i + 1);
if (JSON.stringify(nums) !== JSON.stringify(expected)) { console.error("CHAPTER NUMBERS WRONG:", nums); process.exit(1); }
console.log("ok");
'
```
Expected: `parts 7 chapters 30 numbered 25 labs 7` followed by `ok`.

- [ ] **Step 3: Commit**

```bash
git add book/chapters.json
git commit -m "Add the chapter manifest as the book's source of truth"
```

---

### Task 4: Generator, sidebar, and 25 chapter stubs

**Files:**
- Create: `book/scripts/generate.mjs`
- Create: `book/src/content/docs/book/**/*.md` (generated, 30 files)
- Create: `book/src/content/docs/labs/*.md` (generated, 7 files)
- Modify: `book/astro.config.mjs` — replace `sidebar: []`

**Interfaces:**
- Consumes: `book/chapters.json` with the field contract from Task 3.
- Produces:
  - `npm run gen` — regenerates `src/sidebar.json` and creates any missing stub file. **Never overwrites an existing stub**, so written prose is safe.
  - `book/src/sidebar.json` — imported by `astro.config.mjs`.
  - Chapter frontmatter contract used by every later writing session: `title`, `description`, `sidebar.order`, `status: unverified`.

- [ ] **Step 1: Write `book/scripts/generate.mjs`**

```js
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';

const root = new URL('..', import.meta.url).pathname;
const manifest = JSON.parse(readFileSync(join(root, 'chapters.json'), 'utf8'));
const docs = join(root, 'src/content/docs');

let created = 0;

function write(path, body) {
  if (existsSync(path)) return false;
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, body);
  created++;
  return true;
}

function stub({ title, description, order, outline, labNote }) {
  const bullets = outline.map((o) => `- ${o}`).join('\n');
  return `---
title: ${JSON.stringify(title)}
description: ${JSON.stringify(description)}
sidebar:
  order: ${order}
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

:::caution[Not yet verified on the rig]
This chapter has been written but not yet run end to end on the reference rig
(Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5).
Treat the procedures here as untested until this banner says otherwise.
:::

## In this chapter

${bullets}
${labNote ?? ''}
`;
}

const sidebar = [];

for (const part of manifest.parts) {
  const items = [];
  for (const [i, c] of part.chapters.entries()) {
    const order = i + 1;
    const file = join(docs, 'book', part.id, `${c.slug}.md`);
    const labNote = c.lab
      ? `\n:::note[Lab ${c.lab}]\nThis chapter carries [Lab ${c.lab}](/ZygiskLab/labs/${manifest.labs.find((l) => l.num === c.lab).slug}/).\n:::\n`
      : '';
    write(file, stub({ title: c.title, description: c.description, order, outline: c.outline, labNote }));
    const label = c.num === null ? c.title : `${c.num}. ${c.title}`;
    items.push({ label, slug: `book/${part.id}/${c.slug}` });
  }
  sidebar.push({ label: part.label, items });
}

const labItems = [];
for (const lab of manifest.labs) {
  const file = join(docs, 'labs', `${lab.slug}.md`);
  write(
    file,
    `---
title: ${JSON.stringify(`Lab ${lab.num}: ${lab.title}`)}
description: ${JSON.stringify(lab.deliverable)}
sidebar:
  order: ${lab.num}
status: unverified
---

<span class="zl-status" data-status="unverified">Unverified</span>

**Chapter:** ${lab.chapter}
**Module:** ${lab.module ? `\`modules/${lab.module}/\`` : 'none'}

## Deliverable

${lab.deliverable}

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0,
Zygisk Next 1.4.5. Use a spare device, not your daily driver.

## Steps

_To be written._

## Self-check

_To be written._
`,
  );
  labItems.push({ label: `Lab ${lab.num}: ${lab.title}`, slug: `labs/${lab.slug}` });
}

sidebar.push({ label: 'Labs', items: labItems });

writeFileSync(join(root, 'src/sidebar.json'), JSON.stringify(sidebar, null, 2) + '\n');
console.log(`sidebar: ${sidebar.length} groups; created ${created} new stub(s)`);
```

- [ ] **Step 2: Run the generator**

Run: `cd book && npm run gen`
Expected: `sidebar: 8 groups; created 37 new stub(s)`

- [ ] **Step 3: Wire the sidebar into `astro.config.mjs`**

Add the import below the existing imports:

```js
import sidebar from './src/sidebar.json' with { type: 'json' };
```

and replace `sidebar: [],` with:

```js
      sidebar,
```

- [ ] **Step 4: Build the site**

Run: `cd book && npm run build`
Expected: PASS. The `index.mdx` link that failed in Task 2 Step 6 now resolves,
and every sidebar entry points at a file that exists.

- [ ] **Step 5: Confirm the generator is non-destructive**

Run:
```bash
cd book && echo "sentinel" >> src/content/docs/book/foundations/01-what-zygisk-is.md && npm run gen && tail -1 src/content/docs/book/foundations/01-what-zygisk-is.md
```
Expected: `created 0 new stub(s)` and the last line is still `sentinel`.
Then remove the sentinel line before committing.

- [ ] **Step 6: Commit**

```bash
git add book/scripts book/src book/astro.config.mjs
git commit -m "Generate the sidebar and all chapter and lab stubs from the manifest"
```

---

### Task 5: Lab 1's module, and the module template

**Files:**
- Create: `modules/README.md`
- Create: `modules/01-hello-zygisk/module.prop`
- Create: `modules/01-hello-zygisk/jni/Android.mk`
- Create: `modules/01-hello-zygisk/jni/Application.mk`
- Create: `modules/01-hello-zygisk/jni/zygisk.hpp`
- Create: `modules/01-hello-zygisk/jni/main.cpp`
- Create: `modules/01-hello-zygisk/build.sh`
- Create: `modules/01-hello-zygisk/deploy.sh`
- Create: `modules/01-hello-zygisk/README.md`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: the layout every later module copies — `jni/`, `module.prop`,
  `build.sh` emitting `out/<name>.zip`, `deploy.sh` implementing the safe
  `mv`-based install, and a README stating what the module proves.

- [ ] **Step 1: Vendor `zygisk.hpp`**

Fetch the Zygisk C++ API header from the Zygisk Next / Magisk module sample
and place it at `jni/zygisk.hpp` unmodified. Add a comment block at the top
recording where it came from and the API version it targets. Do not edit the
header — later chapters quote it.

- [ ] **Step 2: Write `jni/main.cpp`**

```cpp
#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// Lab 1: the smallest module that proves it loaded, and proves *where*.
class HelloZygisk : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("onLoad: module loaded into pid=%d", getpid());
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // Still inside the zygote fork. The process is not an app yet.
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("preAppSpecialize: pid=%d uid=%d nice_name=%s", getpid(), args->uid, name);
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        // We are the app now. Compare this pid/uid with the line above.
        LOGI("postAppSpecialize: pid=%d uid=%d", getpid(), getuid());
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(HelloZygisk)
```

- [ ] **Step 3: Write the build files**

`jni/Android.mk`:
```make
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := zygisklab
LOCAL_SRC_FILES := main.cpp
LOCAL_LDLIBS    := -llog
LOCAL_CPPFLAGS  := -std=c++20 -fvisibility=hidden -fvisibility-inlines-hidden
include $(BUILD_SHARED_LIBRARY)
```

`jni/Application.mk`:
```make
APP_ABI      := arm64-v8a
APP_PLATFORM := android-29
APP_STL      := none
APP_CPPFLAGS := -fno-exceptions -fno-rtti
APP_CFLAGS   := -Oz -flto
APP_LDFLAGS  := -flto -Wl,--gc-sections
```

`module.prop`:
```properties
id=zygisklab_hello
name=ZygiskLab 01 - Hello Zygisk
version=1.0
versionCode=1
author=Joseph James
description=Lab 1. Logs pid, uid, and nice_name at each Zygisk callback. Does nothing else.
```

- [ ] **Step 4: Write `build.sh`**

```bash
#!/usr/bin/env bash
# Builds the module and packages a flashable zip into out/.
set -euo pipefail
cd "$(dirname "$0")"

NAME="$(basename "$PWD")"
NDK_BUILD="${ANDROID_NDK_HOME:+$ANDROID_NDK_HOME/ndk-build}"
NDK_BUILD="${NDK_BUILD:-$(command -v ndk-build || true)}"
[ -x "$NDK_BUILD" ] || { echo "ndk-build not found; set ANDROID_NDK_HOME" >&2; exit 1; }

rm -rf out obj libs
"$NDK_BUILD" -j"$(nproc)"

mkdir -p "out/pkg/zygisk"
cp module.prop "out/pkg/module.prop"
cp libs/arm64-v8a/libzygisklab.so "out/pkg/zygisk/arm64-v8a.so"

(cd out/pkg && zip -qr "../$NAME.zip" .)
echo "built out/$NAME.zip"
```

- [ ] **Step 5: Write `deploy.sh`**

```bash
#!/usr/bin/env bash
# Installs the built .so onto a connected device SAFELY.
#
# The module .so must never be overwritten in place: zygote holds it mapped,
# and `cp` rewrites the same inode, changing pages under executing code. The
# result is a SIGSEGV during app specialization that looks like a bug in your
# module. Push to a staging path and `mv` instead - an atomic rename gives a
# new inode and leaves existing mappings intact. Then reboot. See Chapter 7.
set -euo pipefail
cd "$(dirname "$0")"

MODULE_ID="$(sed -n 's/^id=//p' module.prop)"
SERIAL="${1:-}"
ADB=(adb ${SERIAL:+-s "$SERIAL"})

SO="libs/arm64-v8a/libzygisklab.so"
[ -f "$SO" ] || { echo "build first: ./build.sh" >&2; exit 1; }

"${ADB[@]}" push "$SO" /data/local/tmp/arm64-v8a.so
# Writing into the module directory needs mount-master; plain `su -c` is
# denied even as root under KernelSU.
"${ADB[@]}" shell su -M -c "mv /data/local/tmp/arm64-v8a.so /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so && chmod 644 /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so"

echo "installed. Verify the hash matches, then reboot:"
"${ADB[@]}" shell su -M -c "md5sum /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so"
md5sum "$SO"
echo "then: adb reboot"
```

- [ ] **Step 6: Write `modules/01-hello-zygisk/README.md`**

```markdown
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
```

- [ ] **Step 7: Write `modules/README.md`**

A short index: what a module directory contains, that every module follows the
`01-hello-zygisk` layout, that `build.sh` emits `out/<name>.zip` and
`deploy.sh` performs the safe install, and the one exception —
`07-detection-harness/app/`, which is a Gradle Android app because the harness
must observe from inside a real app process.

- [ ] **Step 8: Build it**

Run:
```bash
chmod +x modules/01-hello-zygisk/build.sh modules/01-hello-zygisk/deploy.sh
cd modules/01-hello-zygisk && ./build.sh
```
Expected: `built out/01-hello-zygisk.zip`, and
`unzip -l out/01-hello-zygisk.zip` lists `module.prop` and
`zygisk/arm64-v8a.so`.

- [ ] **Step 9: Commit**

```bash
git add modules/
git commit -m "Add Lab 1's hello-world module and the module layout"
```

---

### Task 6: Publish

**Files:**
- Create: `.github/workflows/deploy.yml`
- Modify: `README.md` — add a build badge

**Interfaces:**
- Consumes: `book/package-lock.json` from Task 2, a building site from Task 4.
- Produces: a green build on `main` and a live site at
  `https://iamjosephmj.github.io/ZygiskLab`.

- [ ] **Step 1: Write `.github/workflows/deploy.yml`**

```yaml
name: Deploy to GitHub Pages

on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: false

jobs:
  build:
    runs-on: ubuntu-latest
    defaults:
      run:
        working-directory: ./book
    steps:
      - name: Checkout
        uses: actions/checkout@v5

      - name: Setup Node
        uses: actions/setup-node@v5
        with:
          node-version: 22
          cache: npm
          cache-dependency-path: book/package-lock.json

      - name: Install dependencies
        run: npm ci

      - name: Check the sidebar is in sync with the manifest
        run: |
          npm run gen
          git diff --exit-code src/sidebar.json

      - name: Build site
        run: npm run build

      - name: Upload artifact
        uses: actions/upload-pages-artifact@v3
        with:
          path: ./book/dist

  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - name: Deploy to GitHub Pages
        id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 2: Verify the sync check passes locally**

Run:
```bash
cd book && npm run gen && git diff --exit-code src/sidebar.json && echo "in sync"
```
Expected: `in sync`. If it fails, `src/sidebar.json` was committed stale in
Task 4 — commit the regenerated file.

- [ ] **Step 3: Add the badge to `README.md`**

Insert immediately under the `# ZygiskLab` heading:

```markdown
[![Deploy to GitHub Pages](https://github.com/iamjosephmj/ZygiskLab/actions/workflows/deploy.yml/badge.svg)](https://github.com/iamjosephmj/ZygiskLab/actions/workflows/deploy.yml)
```

- [ ] **Step 4: Commit**

```bash
git add .github README.md book/src/sidebar.json
git commit -m "Deploy the book to GitHub Pages"
```

- [ ] **Step 5: Push (requires the author)**

Creating the GitHub repository and enabling Pages (Settings → Pages → Source:
GitHub Actions) needs the author's account. Stop here and hand back rather
than creating a remote.

---

## What this plan does not do

Writing the chapters. Every chapter and lab ends this plan as a stub carrying
its title, description, and its "In this chapter" outline, with an
`unverified` banner. Prose is written in later sessions, one part at a time,
and a chapter's banner is only cleared after the procedure is run on the rig.
