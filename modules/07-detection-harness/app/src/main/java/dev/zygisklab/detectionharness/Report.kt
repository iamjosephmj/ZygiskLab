package dev.zygisklab.detectionharness

import android.content.Context
import android.os.Build
import dev.zygisklab.detectionharness.checks.FilesystemProbeCheck
import dev.zygisklab.detectionharness.checks.GotIntegrityCheck
import dev.zygisklab.detectionharness.checks.LoadedLibrariesCheck
import dev.zygisklab.detectionharness.checks.MapsCheck
import dev.zygisklab.detectionharness.checks.MountInfoCheck
import dev.zygisklab.detectionharness.checks.OpenFdCheck
import dev.zygisklab.detectionharness.checks.PropertiesCheck
import dev.zygisklab.detectionharness.checks.ThreadsCheck
import dev.zygisklab.detectionharness.checks.TracerCheck
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Builds the fixed list of checks this harness runs, and formats the
 *  results as plain text a reader can paste into their own lab notes. */
object Report {

    fun buildChecks(context: Context): List<Check> {
        val appInfo = context.applicationInfo
        return listOf(
            MapsCheck(),
            OpenFdCheck(ownDataDir = appInfo.dataDir ?: ""),
            MountInfoCheck(),
            LoadedLibrariesCheck(ownNativeLibDir = appInfo.nativeLibraryDir ?: ""),
            TracerCheck(),
            FilesystemProbeCheck(),
            PropertiesCheck(),
            ThreadsCheck(),
            GotIntegrityCheck(),
        )
    }

    fun run(context: Context): List<CheckResult> = buildChecks(context).map { it.run() }

    /**
     * Renders the results as a flat text report. This is the exact text
     * the "copy" and "share" actions in [MainActivity] hand off -- what you
     * see in the UI is what gets pasted, nothing summarized away.
     */
    fun asText(context: Context, results: List<CheckResult>): String {
        val sb = StringBuilder()
        val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
        sb.appendLine("ZygiskLab Lab 7 -- Detection Harness report")
        sb.appendLine("generated $timestamp")
        sb.appendLine("package: ${context.packageName}")
        sb.appendLine("device: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT}), ${Build.SUPPORTED_ABIS.joinToString(",")}")
        sb.appendLine()

        val found = results.count { it.outcome == Outcome.FOUND }
        val notFound = results.count { it.outcome == Outcome.NOT_FOUND }
        val couldNotRun = results.count { it.outcome == Outcome.COULD_NOT_RUN }
        sb.appendLine("summary: $found found, $notFound not found, $couldNotRun could not run, out of ${results.size} checks")
        sb.appendLine()

        for (result in results) {
            sb.appendLine("== ${result.name} [${result.outcome.label}] ==")
            sb.appendLine(result.description)
            for (line in result.evidence) {
                sb.appendLine("  - $line")
            }
            sb.appendLine()
        }

        if (found == 0) {
            sb.appendLine(
                "No check found anything. On an unmodified device this is the expected, " +
                    "honest baseline -- it is what gives a later FOUND result, once one of " +
                    "this book's own modules is armed, its meaning.",
            )
        }

        return sb.toString()
    }
}
