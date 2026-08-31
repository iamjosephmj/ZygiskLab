package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Reduces `/proc/self/maps` to the distinct set of native libraries this
 * process has loaded, and reports which of them come from somewhere a
 * stock app's libraries never do.
 *
 * This is a different question from [MapsCheck]: that check greps raw
 * mapping lines for module-shaped names; this one asks "what is the
 * *library list*, and does any entry on it sit outside the small set of
 * places a stock app loads `.so` files from" -- its own native library
 * directory (`/data/app/.../lib/<abi>/` or the APK's own `lib/` inside the
 * split), and the platform's own `/system`, `/apex`, `/vendor` libraries.
 * A `.so` mapped from `/data/local/tmp`, `/data/adb`, `/dev`, or an
 * anonymous `memfd:` is not where a normal app's libraries live, Zygisk's
 * own loading mechanism among them.
 */
class LoadedLibrariesCheck(private val ownNativeLibDir: String) : Check {

    companion object {
        private val ORDINARY_LIB_PREFIXES = listOf(
            "/system/",
            "/apex/",
            "/vendor/",
            "/product/",
            "/data/app/",
        )
    }

    override fun run(): CheckResult {
        val file = File("/proc/self/maps")
        val lines = try {
            file.readLines()
        } catch (e: Exception) {
            return CheckResult(
                name = "Loaded native libraries",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("readLines() on /proc/self/maps failed: ${e.javaClass.simpleName}: ${e.message}"),
                description = "Lists distinct .so libraries this process has mapped and flags any outside the ordinary app/platform locations.",
            )
        }

        // A maps line has six fixed fields -- address, perms, offset, dev,
        // inode -- before the backing path, and the path itself can
        // legitimately contain spaces (an app installed from a path with
        // one, "(deleted)" suffixes, and so on). Splitting on whitespace
        // with a limit of 6 lets the regex consume the fixed fields (each
        // run of whitespace between them, however wide, is one delimiter
        // match) and leaves the remainder -- the path, spaces intact -- as
        // the last element, instead of truncating it to the last field the
        // way `substringAfterLast(' ')` did.
        val libraryPaths = lines
            .mapNotNull { line ->
                val fields = line.trim().split(Regex("\\s+"), limit = 6)
                fields.getOrNull(5)?.takeIf { it.endsWith(".so") }
            }
            .distinct()
            .sorted()

        val unusual = libraryPaths.filter { path ->
            val isOrdinary = ORDINARY_LIB_PREFIXES.any { path.startsWith(it) } ||
                path.startsWith(ownNativeLibDir)
            !isOrdinary
        }

        val evidence = mutableListOf(
            "${libraryPaths.size} distinct .so path(s) mapped; ${unusual.size} outside system/vendor/apex/own-app locations",
        )
        evidence.addAll(unusual)

        return CheckResult(
            name = "Loaded native libraries",
            outcome = if (unusual.isNotEmpty()) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = evidence,
            description = "Lists distinct .so libraries this process has mapped and flags any outside the ordinary app/platform locations.",
        )
    }
}
