package dev.zygisklab.detectionharness.checks

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome

/**
 * Probes a fixed list of well-known root-manager and module-provider paths
 * with `stat(2)` and reports, for each one, one of three outcomes rather
 * than a single yes/no:
 *
 * - **reachable** -- `stat` succeeded; the path exists and this process can
 *   see it.
 * - **not reachable** -- `stat` failed with `ENOENT`: the path genuinely
 *   isn't there.
 * - **permission denied** -- `stat` failed with `EACCES` or `EPERM`: the
 *   path exists (or its parent directory does) but this process isn't
 *   allowed to see it.
 *
 * That third outcome is the point of using `stat` (via `android.system.Os`,
 * a thin wrapper over the real syscall) instead of `java.io.File#exists()`,
 * which collapses "denied" and "not there" into the same `false` and
 * throws away exactly the distinction this check exists to make. A denial
 * is itself information -- it can mean a path is being hidden from this
 * process by a mount namespace or a mount policy, which is a different
 * finding than the path simply not existing, even though naive code often
 * treats them the same.
 */
class FilesystemProbeCheck : Check {

    companion object {
        // Paths associated with root managers and injection/module
        // providers that have no legitimate reason to be visible to an
        // ordinary app process on a stock device.
        private val PROBE_PATHS = listOf(
            "/system/bin/su",
            "/system/xbin/su",
            "/sbin/su",
            "/vendor/bin/su",
            "/system/app/Superuser.apk",
            "/data/adb/magisk",
            "/data/adb/modules",
            "/data/adb/ksu",
            "/data/adb/ap",
            "/debug_ramdisk/su",
            "/data/local/tmp/su",
            "/system/xbin/daemonsu",
            "/data/data/com.topjohnwu.magisk",
            "/data/data/me.weishu.exp", // LSPosed manager package family
        )
    }

    override fun run(): CheckResult {
        val evidence = mutableListOf<String>()
        var anyReachable = false

        for (path in PROBE_PATHS) {
            try {
                val stat = Os.stat(path)
                anyReachable = true
                evidence.add("$path -> reachable (mode=${Integer.toOctalString(stat.st_mode)}, size=${stat.st_size})")
            } catch (e: ErrnoException) {
                val label = when (e.errno) {
                    OsConstants.ENOENT -> "not reachable (no such path)"
                    OsConstants.EACCES, OsConstants.EPERM -> "permission denied (${e.message})"
                    else -> "stat failed: errno=${e.errno} (${e.message})"
                }
                evidence.add("$path -> $label")
            } catch (e: Exception) {
                evidence.add("$path -> could not probe: ${e.javaClass.simpleName}: ${e.message}")
            }
        }

        return CheckResult(
            name = "Filesystem probes (root/provider paths)",
            outcome = if (anyReachable) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = evidence,
            description = "stat()s a fixed list of well-known root/provider paths and reports reachable, not reachable, or permission denied for each.",
        )
    }
}
