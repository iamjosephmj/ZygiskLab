#!/usr/bin/env bash
# Installs the built .so onto a connected device SAFELY.
#
# The module .so must never be overwritten in place: zygote holds it mapped,
# and `cp` rewrites the same inode, changing pages under executing code. The
# result is a SIGSEGV during app specialization that looks like a bug in your
# module. Push to a staging path and `mv` instead - an atomic rename gives a
# new inode and leaves existing mappings intact. Then reboot. See Chapter 7.
#
# `mv` is also not label-safe: the file keeps the SELinux label of the
# staging path it came from (/data/local/tmp) rather than inheriting the
# label of the directory it lands in, and `chmod` only touches the mode
# bits, not the label. On a device that enforces the module directory's
# label, a mismatched label makes the loader silently refuse the file -
# the module is listed but never fires, with no error anywhere. `restorecon`
# (or `chcon` if `restorecon` isn't present) after the `mv` fixes the label
# to match the destination directory.
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
"${ADB[@]}" shell su -M -c "mv /data/local/tmp/arm64-v8a.so /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so && chmod 644 /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so && (restorecon /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so || chcon --reference=/data/adb/modules/$MODULE_ID/zygisk /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so)"

echo "installed. Verify the hash matches, then reboot:"
"${ADB[@]}" shell su -M -c "md5sum /data/adb/modules/$MODULE_ID/zygisk/arm64-v8a.so"
md5sum "$SO"
echo "then: adb reboot"
