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
#
# Every step below is checked rather than assumed. Each of the three failures
# this guards against - a destination that does not exist, a transfer that
# did not land intact, and a label that was not restored - produces a module
# that looks installed and never runs, which is the single most expensive
# way to lose an afternoon in this book.
set -euo pipefail
cd "$(dirname "$0")"

MODULE_ID="$(sed -n 's/^id=//p' module.prop)"
SERIAL="${1:-}"
ADB=(adb ${SERIAL:+-s "$SERIAL"})

DEST_DIR="/data/adb/modules/$MODULE_ID/zygisk"
DEST="$DEST_DIR/arm64-v8a.so"
STAGE="/data/local/tmp/arm64-v8a.so"

SO="libs/arm64-v8a/libzygisklab_armed.so"
[ -f "$SO" ] || { echo "build first: ./build.sh" >&2; exit 1; }

# Run a command as root in the global mount namespace and return its output.
# Writing into the module directory needs mount-master; plain `su -c` is
# denied even as root under KernelSU.
sush() { "${ADB[@]}" shell su -M -c "$1" | tr -d '\r'; }

# 1. The destination must already exist. `mv` itself would fail here - it
#    does not create missing directories - but that failure happens on the
#    far side of `adb shell`, whose exit status does not reliably reach this
#    script, so `set -e` never fires and the deploy reports success. Checking
#    first turns an invisible remote failure into a clear local one.
if [ "$(sush "[ -d '$DEST_DIR' ] && echo yes || echo no")" != "yes" ]; then
    echo "error: $DEST_DIR does not exist on the device." >&2
    echo "Install the module zip once through your manager first, and check" >&2
    echo "that id=$MODULE_ID in module.prop matches the installed module." >&2
    exit 1
fi

# 2. Stage, then rename. The rename is the whole point: a new inode, and any
#    process still mapping the old file keeps the file it already has.
"${ADB[@]}" push "$SO" "$STAGE"
sush "mv '$STAGE' '$DEST' && chmod 644 '$DEST'" >/dev/null

# 3. Restore the label, and verify it took. `module.prop` is the reference:
#    it was written in place by the manager, so it carries whatever label
#    this provider's module files are supposed to have.
sush "restorecon '$DEST' 2>/dev/null || chcon --reference='/data/adb/modules/$MODULE_ID/module.prop' '$DEST'" >/dev/null
DEST_LABEL="$(sush "ls -Z '$DEST'" | awk '{print $1}')"
REF_LABEL="$(sush "ls -Z '/data/adb/modules/$MODULE_ID/module.prop'" | awk '{print $1}')"
if [ "$DEST_LABEL" != "$REF_LABEL" ]; then
    echo "error: SELinux label was not restored." >&2
    echo "  deployed: $DEST_LABEL" >&2
    echo "  expected: $REF_LABEL (from module.prop)" >&2
    echo "The module will be listed but will never load. See Chapter 7." >&2
    exit 1
fi

# 4. Assert the transfer landed intact. Printing two hashes for a human to
#    compare is not a check - nobody compares them at 2am.
LOCAL_MD5="$(md5sum "$SO" | awk '{print $1}')"
REMOTE_MD5="$(sush "md5sum '$DEST'" | awk '{print $1}')"
if [ "$LOCAL_MD5" != "$REMOTE_MD5" ]; then
    echo "error: hash mismatch after deploy." >&2
    echo "  local:  $LOCAL_MD5" >&2
    echo "  device: $REMOTE_MD5" >&2
    exit 1
fi

echo "installed $DEST"
echo "  md5:   $LOCAL_MD5 (matches)"
echo "  label: $DEST_LABEL"
echo
echo "zygote still has the old library mapped. Reboot before testing:"
echo "  adb reboot"
