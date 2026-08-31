package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Enumerates `/proc/self/task` and reports the name (`comm`) of every
 * thread currently running in this process.
 *
 * A module that injects itself into an app process by the mechanism
 * Chapter 22 describes -- a Zygisk `.so` loaded at zygote fork time -- and
 * that then spawns a background thread of its own (a watchdog, a socket
 * listener back to the manager, anything that outlives the injecting
 * call) leaves that thread sitting in this list for as long as the
 * process runs. `/proc/self/task/<tid>/comm` is the same 16-byte name the
 * kernel shows `ps -T` and Android Studio's thread view, and it's
 * world-readable for this process's own threads -- no root needed.
 *
 * This check does not try to *judge* the thread list -- it has no
 * privileged notion of which names are "normal" the way [PropertiesCheck]
 * has a stock-production property value to compare against, and guessing
 * would mean silently deciding what counts as suspicious on the app's
 * behalf, which is exactly the kind of verdict-instead-of-evidence this
 * harness's data model exists to avoid. Ordinary Android runtime threads
 * (`main`, the various `Compiler`/GC threads the ART runtime starts,
 * `RenderThread`, `binder:<pid>_<n>`, `Jit thread pool`, and so on) are
 * all threads *this app itself* is aware it should have; an app author
 * running this check on their own process knows their own thread names
 * and can eyeball the list far more reliably than a fixed allowlist
 * baked into this class could, especially across the wide variation in
 * ART/Compose/OkHttp/etc. thread names between Android versions and
 * library sets.
 *
 * The one thing this check *does* flag on its own is a structural
 * anomaly, not a name: a `tid` directory that disappears between being
 * listed and being read is unremarkable (threads start and exit
 * constantly), but a `comm` file that cannot be read for a thread that
 * *is* still listed a moment later is worth a second look, since a
 * normal thread's own name is always readable. Even that is reported as
 * evidence for the reader to weigh, not folded into a verdict on its own.
 */
class ThreadsCheck : Check {

    override fun run(): CheckResult {
        val taskDir = File("/proc/self/task")
        val taskEntries = taskDir.listFiles() ?: return CheckResult(
            name = "Threads (/proc/self/task)",
            outcome = Outcome.COULD_NOT_RUN,
            evidence = listOf("listFiles() on /proc/self/task returned null"),
            description = "Lists this process's own threads by name, for the reader to judge against what the app itself expects to have running.",
        )

        val threads = mutableListOf<String>()
        val unreadable = mutableListOf<String>()

        for (entry in taskEntries.sortedBy { it.name.toIntOrNull() ?: Int.MAX_VALUE }) {
            val tid = entry.name
            val commFile = File(entry, "comm")
            val name = try {
                commFile.readText().trim()
            } catch (e: Exception) {
                // A thread that exits between listFiles() and this read is
                // ordinary churn, not evidence -- but only if the tid
                // directory itself is now gone. If the directory is still
                // there and just the comm read failed, that is unusual
                // enough to note.
                if (entry.exists()) {
                    unreadable.add("tid $tid -> comm unreadable: ${e.javaClass.simpleName}: ${e.message}")
                }
                continue
            }
            threads.add("tid $tid -> \"$name\"")
        }

        val evidence = mutableListOf(
            "${threads.size} thread(s) enumerated in /proc/self/task",
        )
        evidence.addAll(threads)
        if (unreadable.isNotEmpty()) {
            evidence.add("${unreadable.size} thread(s) listed but with an unreadable comm file:")
            evidence.addAll(unreadable)
        }

        // FOUND is reserved for the one thing this check can defend on its
        // own -- a structural anomaly (a still-listed thread whose own
        // name it cannot read) -- not for any particular thread name.
        // Everything else is the inventory itself, reported as NOT_FOUND
        // evidence for a human reader who knows their own app's threads
        // to interpret.
        return CheckResult(
            name = "Threads (/proc/self/task)",
            outcome = if (unreadable.isNotEmpty()) Outcome.FOUND else Outcome.NOT_FOUND,
            evidence = evidence,
            description = "Lists this process's own threads by name, for the reader to judge against what the app itself expects to have running.",
        )
    }
}
