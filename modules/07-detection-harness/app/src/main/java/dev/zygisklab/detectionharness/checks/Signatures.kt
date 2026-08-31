package dev.zygisklab.detectionharness.checks

/**
 * Substrings that, if they show up in a mapped path, an open file
 * descriptor's target, or a mount entry, are worth a second look.
 *
 * This is not a signature database in the antivirus sense -- it is a short,
 * readable list of the names this book's own modules (and the tools they
 * build on) are known to leave lying around: root managers, the Zygisk
 * loaders, and the injection frameworks a reader is likely to have on a
 * test device alongside them. A real detector would keep this list current
 * against whatever it's trying to catch; here it exists so the checks below
 * have something concrete to grep for, and so a reader can extend it and
 * see their own module show up.
 */
val INTERESTING_SUBSTRINGS: List<String> = listOf(
    "magisk",
    "zygisk",
    "zygisklab",
    "ksu",
    "kernelsu",
    "apatch",
    "supersu",
    "superuser",
    "xposed",
    "lsposed",
    "edxposed",
    "riru",
    "substrate",
    "frida",
    "gadget",
    "gum-js-loop",
    "linjector",
    "/data/adb/",
    "/debug_ramdisk/",
)

/** True if [path] contains any of [INTERESTING_SUBSTRINGS], case-insensitively. */
fun looksInteresting(path: String): Boolean {
    val lower = path.lowercase()
    return INTERESTING_SUBSTRINGS.any { lower.contains(it) }
}
