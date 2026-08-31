# Modules

Each subdirectory here is one lab from the book, numbered to match the
chapter it belongs to (`01-hello-zygisk` is Lab 1, from Chapter 4).

Every module directory follows the layout `01-hello-zygisk` establishes:

```
NN-lab-name/
├── module.prop     # Magisk/KernelSU module metadata
├── jni/
│   ├── Android.mk
│   ├── Application.mk
│   ├── zygisk.hpp   # vendored, unmodified Zygisk C++ API
│   └── main.cpp
├── build.sh         # ndk-build, then package -> out/<name>.zip
├── deploy.sh        # safe install: push + mv, never cp over a live .so
└── README.md        # what the module proves
```

- `build.sh` runs `ndk-build` and packages the result into a flashable
  `out/<name>.zip`. Build products (`libs/`, `obj/`, `out/`, `*.zip`) are
  gitignored — never commit them.
- `deploy.sh` installs onto a connected device by pushing to
  `/data/local/tmp` and then `mv`-ing into the module directory under
  `su -M`. It never `cp`s over the live `.so` — zygote holds it mapped, and
  overwriting in place crashes app specialization. See Chapter 7 for why.
- `jni/zygisk.hpp` is vendored from upstream, not written here — each copy
  records the source URL, targeted `ZYGISK_API_VERSION`, and fetch date in a
  header comment. Re-check it against upstream before targeting a newer
  Zygisk API; do not hand-edit it.

## The one exception

`07-detection-harness/app/` is a Gradle Android app, not a `jni/`-only
native module — the detection harness has to observe from inside a real
app process, which a bare `.so` can't do on its own.
