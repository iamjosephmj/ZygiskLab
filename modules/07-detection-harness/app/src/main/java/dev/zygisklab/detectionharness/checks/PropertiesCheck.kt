package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.BufferedReader
import java.io.InputStreamReader

/**
 * Reads a short, fixed list of system properties that commonly differ on a
 * modified device, and reports their values -- not a verdict.
 *
 * These are read through the `getprop` binary rather than the hidden
 * `android.os.SystemProperties` class: `getprop` is a normal, unprivileged
 * executable any process can run and its properties are world-readable, so
 * this needs no permission and no reflection into a non-public API. Each
 * property is genuinely ambiguous on its own -- `ro.debuggable=1` is
 * completely ordinary on a userdebug/eng build and says nothing about root,
 * `ro.boot.verifiedbootstate` can be `orange` on a device the owner
 * unlocked for entirely unrelated reasons -- which is exactly why this
 * check reports values instead of pretending to score them.
 */
class PropertiesCheck : Check {

    companion object {
        // Each property paired with the value a stock, locked, production
        // build is expected to report -- so this check can tell "read a
        // value" (always true, and not interesting on its own) apart from
        // "read a value that differs from the stock-production expectation"
        // (worth flagging), without claiming that difference proves anything.
        private val PROPERTIES: List<Pair<String, String>> = listOf(
            "ro.build.type" to "user",
            "ro.debuggable" to "0",
            "ro.secure" to "1",
            "ro.build.tags" to "release-keys",
            "ro.boot.verifiedbootstate" to "green",
            "ro.boot.flash.locked" to "1",
            "ro.boot.veritymode" to "enforcing",
            "ro.boot.warranty_bit" to "0",
            "service.adb.root" to "0",
            "ro.kernel.qemu" to "0",
        )
    }

    private fun readProperty(name: String): String? {
        return try {
            val process = ProcessBuilder("getprop", name).redirectErrorStream(true).start()
            val output = BufferedReader(InputStreamReader(process.inputStream)).use { it.readLine() }
            process.waitFor()
            output?.trim()
        } catch (e: Exception) {
            null
        }
    }

    override fun run(): CheckResult {
        val evidence = mutableListOf<String>()
        var readCount = 0
        var interestingCount = 0

        for ((key, expected) in PROPERTIES) {
            val value = readProperty(key)
            if (value == null) {
                evidence.add("$key -> could not read (getprop unavailable or failed)")
                continue
            }
            readCount++
            val display = value.ifEmpty { "(empty)" }
            if (value == expected) {
                evidence.add("$key = $display (matches stock-production expectation \"$expected\")")
            } else {
                evidence.add("$key = $display (stock-production expectation was \"$expected\")")
                interestingCount++
            }
        }

        if (readCount == 0) {
            return CheckResult(
                name = "System properties",
                outcome = Outcome.COULD_NOT_RUN,
                evidence = evidence,
                description = "Reads a fixed list of properties that commonly differ on a modified device and reports the values.",
            )
        }

        // "Found" here means at least one property read back a value that
        // differs from the stock-production expectation above, not that
        // anything was proven -- see the class doc. A build that matches
        // every expectation (the ordinary case on a production, locked
        // device) reports NOT_FOUND for legibility.
        return CheckResult(
            name = "System properties",
            outcome = if (interestingCount > 0) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = evidence,
            description = "Reads a fixed list of properties that commonly differ on a modified device and reports the values.",
        )
    }
}
