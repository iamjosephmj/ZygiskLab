package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Reads this process's own mount table from `/proc/self/mountinfo` and
 * reports entries that look unusual for an app process: an `overlayfs`
 * mounted over `/system`, `/product`, or `/vendor` (how Magisk-family root
 * solutions present a modified system partition without touching the real
 * one), or a mount whose source or target names a root manager or module
 * loader directly.
 *
 * `/proc/self/mountinfo` is this process's own view of its mount
 * namespace -- world-readable, no root needed to read it, and exactly what
 * Chapter 22 describes an app comparing "against what it expects". A stock,
 * unmodified device's app mount namespace has no overlay entries over the
 * system partitions and nothing naming a root manager; both are worth
 * flagging on sight.
 */
class MountInfoCheck : Check {

    companion object {
        private val OVERLAY_TARGETS = listOf("/system", "/product", "/vendor", "/odm", "/system_ext")
    }

    override fun run(): CheckResult {
        val file = File("/proc/self/mountinfo")
        val lines = try {
            file.readLines()
        } catch (e: Exception) {
            return CheckResult(
                name = "Mount table (/proc/self/mountinfo)",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("readLines() failed: ${e.javaClass.simpleName}: ${e.message}"),
                description = "Scans this process's own mount namespace for overlay or root-manager mounts.",
            )
        }

        val matches = lines.filter { line ->
            val isOverlayOnSystem = line.contains(" overlay ") &&
                OVERLAY_TARGETS.any { target -> line.contains(" $target ") || line.contains(" $target/") }
            isOverlayOnSystem || looksInteresting(line)
        }

        return CheckResult(
            name = "Mount table (/proc/self/mountinfo)",
            outcome = if (matches.isNotEmpty()) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = if (matches.isNotEmpty()) matches
            else listOf("${lines.size} mount entry/entries read, none were overlay-on-system or module-named"),
            description = "Scans this process's own mount namespace for overlay or root-manager mounts.",
        )
    }
}
