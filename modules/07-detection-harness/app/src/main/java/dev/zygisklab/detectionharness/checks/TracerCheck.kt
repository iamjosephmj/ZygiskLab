package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Reads `TracerPid` out of `/proc/self/status` and reports it as-is.
 *
 * The kernel sets this field to the PID of whatever process is attached to
 * this one via `ptrace` -- a debugger, `strace`, Frida's own tracer, or
 * nothing at all. `0` is the ordinary value for an app that no one is
 * attached to; this check does not try to decide whether a nonzero value
 * is a debugger a developer attached on purpose or something else. It
 * reports the number and lets the reader make that call, because the file
 * alone can't distinguish the two -- that distinction needs context this
 * check doesn't have.
 */
class TracerCheck : Check {
    override fun run(): CheckResult {
        val file = File("/proc/self/status")
        val lines = try {
            file.readLines()
        } catch (e: Exception) {
            return CheckResult(
                name = "Tracer presence (TracerPid)",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("readLines() on /proc/self/status failed: ${e.javaClass.simpleName}: ${e.message}"),
                description = "Reads TracerPid from /proc/self/status and reports it.",
            )
        }

        val tracerLine = lines.firstOrNull { it.startsWith("TracerPid:") }
            ?: return CheckResult(
                name = "Tracer presence (TracerPid)",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("no TracerPid line found in /proc/self/status"),
                description = "Reads TracerPid from /proc/self/status and reports it.",
            )

        val pid = tracerLine.substringAfter(':').trim().toIntOrNull()

        return CheckResult(
            name = "Tracer presence (TracerPid)",
            outcome = if (pid != null && pid != 0) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = listOf(tracerLine),
            description = "Reads TracerPid from /proc/self/status and reports it.",
        )
    }
}
