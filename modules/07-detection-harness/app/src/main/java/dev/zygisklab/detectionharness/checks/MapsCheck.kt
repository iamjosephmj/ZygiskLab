package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Reads this process's own `/proc/self/maps` and reports every line whose
 * mapped path looks module- or provider-related.
 *
 * `/proc/self/maps` is exactly what it sounds like: every memory mapping
 * this process currently has, one line per region, with the backing file
 * path (if any) at the end. Any `.so` a Zygisk module loaded into this
 * process, any file a provider left mapped, shows up here under its own
 * path -- this is the same view Chapter 22 describes an app taking of
 * itself, and it costs nothing but a file read.
 */
class MapsCheck : Check {
    override fun run(): CheckResult {
        val file = File("/proc/self/maps")
        val lines = try {
            file.readLines()
        } catch (e: Exception) {
            return CheckResult(
                name = "Memory maps (/proc/self/maps)",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("readLines() failed: ${e.javaClass.simpleName}: ${e.message}"),
                description = "Scans this process's own mapped-file list for module- or provider-shaped paths.",
            )
        }

        val matches = lines.filter { looksInteresting(it) }

        return CheckResult(
            name = "Memory maps (/proc/self/maps)",
            outcome = if (matches.isNotEmpty()) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = if (matches.isNotEmpty()) matches
            else listOf("${lines.size} mapping(s) read, none matched a module- or provider-shaped path"),
            description = "Scans this process's own mapped-file list for module- or provider-shaped paths.",
        )
    }
}
