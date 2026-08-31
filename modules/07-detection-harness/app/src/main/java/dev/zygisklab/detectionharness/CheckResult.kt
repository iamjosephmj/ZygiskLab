package dev.zygisklab.detectionharness

/**
 * What a check can conclude about itself, independent of what it found.
 *
 * This is deliberately not a boolean. A check that only returns
 * true/false throws away the difference between "I looked and there was
 * nothing there" and "I couldn't look" -- and on a locked-down device the
 * second case is common enough (a probe denied by SELinux, a file that
 * simply doesn't exist on this API level) that collapsing it into "false"
 * would misreport a clean result as a broken one, or vice versa.
 */
enum class Outcome(val label: String) {
    /** The check ran to completion and found something worth reporting. */
    FOUND("found"),

    /** The check ran to completion and found nothing worth reporting. */
    NOT_FOUND("not found"),

    /** The check could not complete -- an unreadable file, a denied
     *  syscall, an unexpected exception. This is its own outcome, not a
     *  silent "not found", because a check that can't run tells you
     *  nothing about the process and must not be read as a clean result. */
    COULD_NOT_RUN("could not run"),
}

/**
 * The result of a single self-inspection check.
 *
 * [evidence] is the whole point of this shape: instead of a verdict, each
 * check hands back the actual lines it read -- the matching `/proc/self/maps`
 * row, the `readlink` target, the mount entry, the property value. A reader
 * can look at the evidence and agree or disagree with the outcome; a bare
 * boolean would just have to be trusted.
 */
data class CheckResult(
    val name: String,
    val outcome: Outcome,
    val evidence: List<String>,
    /** One line of plain-language context: what this check looked at and
     *  why, so the report reads on its own without the source open next to it. */
    val description: String,
)

/** One self-inspection check. Every implementation reads only this
 *  process's own `/proc/self` entries or world-readable device state --
 *  nothing that requires root, and nothing that touches another app. */
fun interface Check {
    fun run(): CheckResult
}
