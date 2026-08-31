package dev.zygisklab.detectionharness.checks

import android.system.Os
import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Walks `/proc/self/fd` and reports the `readlink` target of every open
 * descriptor that points outside the ordinary places a stock app process
 * has file descriptors open: its own APK/data directories, the platform's
 * shared libraries and framework files, and anonymous kernel objects
 * (sockets, pipes, ashmem, dmabuf) that carry no filesystem path at all.
 *
 * A descriptor left open onto `/data/adb/...`, a module's own package
 * directory, or anything else outside that ordinary set is exactly the
 * kind of thing this check exists to surface -- not because every such
 * descriptor is evidence of anything, but because on a clean process there
 * shouldn't be one, so any that show up are worth reading.
 */
/**
 * @param ownDataDir this app's own data directory (`applicationInfo.dataDir`),
 * so descriptors the process legitimately holds open onto its own files
 * (databases, cache, code cache) aren't misreported as "outside the sandbox".
 */
class OpenFdCheck(private val ownDataDir: String) : Check {

    companion object {
        // Prefixes considered ordinary for a stock app process. Anything a
        // descriptor points at that doesn't start with one of these -- and
        // isn't one of the path-less kernel object descriptions below -- is
        // reported as evidence.
        private val ORDINARY_PREFIXES = listOf(
            "/system/",
            "/apex/",
            "/vendor/",
            "/product/",
            "/data/app/",
            "/data/dalvik-cache/",
            "/data/resource-cache/",
            "/data/misc/",
            "/dev/",
            "/proc/",
            "/vendor_dlkm/",
            "/odm/",
        )

        // Descriptor targets with no real filesystem path -- normal, and
        // not worth listing individually.
        private val PATHLESS_PREFIXES = listOf(
            "socket:", "pipe:", "anon_inode:", "/memfd:",
        )
    }

    override fun run(): CheckResult {
        val fdDir = File("/proc/self/fd")
        val entries = fdDir.listFiles() ?: return CheckResult(
            name = "Open file descriptors (/proc/self/fd)",
            outcome = Outcome.COULD_NOT_RUN,
            evidence = listOf("listFiles() on /proc/self/fd returned null"),
            description = "Lists this process's own open descriptors and flags any target outside the app sandbox.",
        )

        val flagged = mutableListOf<String>()
        var readable = 0
        var pathless = 0
        var ordinary = 0

        for (entry in entries) {
            // Each entry under /proc/self/fd is a symlink whose text *is*
            // the descriptor's target. Os.readlink() reads that link
            // literally, unlike canonicalPath, which resolves every
            // symlink component it can and would quietly rewrite the raw
            // target into whatever it ultimately points at -- masking the
            // exact string the kernel reports, which is the evidence this
            // check exists to preserve.
            val target = try {
                Os.readlink(entry.path)
            } catch (e: Exception) {
                // The descriptor can legitimately close between listFiles()
                // and this read -- fds churn constantly. That race is not
                // evidence of anything and is not counted as "flagged".
                continue
            }
            readable++

            // Anchored to the start of the string only -- a `contains`
            // check would also match a real, ordinary path that merely
            // happens to have "socket:" or "memfd:" somewhere inside it
            // (a file literally named that in an app-owned directory,
            // say), silently dropping it from the flagged set instead of
            // evaluating it against ORDINARY_PREFIXES like any other path.
            val isPathless = PATHLESS_PREFIXES.any { target.startsWith(it) }
            if (isPathless) {
                pathless++
                continue
            }
            val isOrdinary = ORDINARY_PREFIXES.any { target.startsWith(it) } ||
                target.startsWith(ownDataDir)
            if (isOrdinary) {
                ordinary++
            } else {
                flagged.add("fd ${entry.name} -> $target")
            }
        }

        val summary = "$readable descriptor(s) read: $ordinary in ordinary app locations, " +
            "$pathless path-less kernel objects (sockets/pipes/anon inodes), " +
            "${flagged.size} outside the expected sandbox"

        return CheckResult(
            name = "Open file descriptors (/proc/self/fd)",
            outcome = if (flagged.isNotEmpty()) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = listOf(summary) + flagged,
            description = "Lists this process's own open descriptors and flags any target outside the app sandbox.",
        )
    }
}
