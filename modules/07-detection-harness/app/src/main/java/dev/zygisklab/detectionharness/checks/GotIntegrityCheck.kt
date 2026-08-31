package dev.zygisklab.detectionharness.checks

import dev.zygisklab.detectionharness.Check
import dev.zygisklab.detectionharness.CheckResult
import dev.zygisklab.detectionharness.Outcome
import java.io.File

/**
 * Chapter 22 argues that GOT (Global Offset Table) verification has the
 * best false-positive profile of any self-inspection an app can perform,
 * because it tests a *structural invariant* instead of matching a name or
 * a path: a GOT slot that an imported function resolves through should
 * hold an address that falls inside the mapped range of the library that
 * actually defines that function. If a module hooks `read()` by
 * overwriting a caller's GOT entry for it, the slot now holds an address
 * inside the module's own `.so` (or, for an inline trampoline, inside
 * whatever it `mmap()`'d) instead of inside `libc.so` -- and that mismatch
 * doesn't depend on knowing the module's name in advance the way a
 * signature scan does.
 *
 * **This class does not fully implement that invariant, and says so
 * honestly rather than faking it.** The full check has two parts:
 *
 * 1. Find the address range of the library that *should* define a given
 *    imported symbol.
 * 2. Resolve the address a GOT slot for that symbol *actually* holds, and
 *    confirm it falls inside (1).
 *
 * Part 1 is achievable from pure Kotlin: `/proc/self/maps` is a plain
 * text file, world-readable for this process's own mappings, and this
 * check parses it below to find where `libc.so` is mapped.
 *
 * Part 2 is **not** achievable from pure Kotlin, and this check does not
 * attempt to fake it. Reading what a specific GOT slot in a specific
 * loaded ELF image currently holds means either calling `dlsym()`/walking
 * `.dynamic` to find the slot for a given symbol name, or opening the
 * `.so` file (or the mapped region via `/proc/self/mem`), parsing its ELF
 * header, section headers, `.dynsym`/`.dynstr`, and `.rela.plt`/`.rela.dyn`
 * relocation tables to compute the slot's file offset, then reading the
 * eight (or four) bytes at that offset out of live memory to get the
 * pointer value the process would actually branch to. Both routes need
 * either a native call (`dlsym`, or `mmap`/`ptrace`-level access to
 * `/proc/self/mem`) or a complete ELF relocation-table parser -- neither
 * of which exists in this pure-JVM app, which has no JNI/NDK code of its
 * own. The Android SDK gives a Kotlin app no supported way to resolve a
 * dynamic symbol to a runtime address or to read another mapping's raw
 * bytes at an arbitrary offset. Claiming to test the GOT invariant without
 * that ability would mean silently testing less than the check's own name
 * promises -- exactly what this harness's three-outcome model exists to
 * avoid (see [Outcome.COULD_NOT_RUN]'s doc).
 *
 * So this check does the part it can, honestly stops there, and reports
 * [Outcome.COULD_NOT_RUN] for the invariant itself: it confirms *where*
 * `libc.so` -- the library most GOT/PLT hooking on Android targets --
 * is mapped in this process, and flags any additional mapping whose path
 * ends in `libc.so` but sits outside the small set of locations a stock
 * device ever maps the real one from (`/apex/com.android.runtime/lib64/bionic/`
 * or `/apex/com.android.runtime/lib/bionic/` on API 29+, where the Runtime
 * APEX moved bionic; `/system/lib64/` or `/system/lib/` on older images
 * that still map it directly). A second, impostor `libc.so`
 * mapped from somewhere else is real, structural evidence on its own --
 * it's how a GOT slot could plausibly get redirected to look legitimate at
 * a glance -- and is reported as [Outcome.FOUND] when seen. But its
 * *absence* proves nothing about any individual GOT slot, which is why the
 * clean case is [Outcome.COULD_NOT_RUN] rather than [Outcome.NOT_FOUND]:
 * this check never got to look at a single slot value, so it cannot
 * honestly claim the invariant held.
 */
class GotIntegrityCheck : Check {

    companion object {
        // The libc functions Chapter 22 uses as its running example for
        // GOT hijacking. Listed here only to document which symbols the
        // full invariant (were it implementable) would target -- this
        // check never resolves any of them, for the reasons in the KDoc
        // above.
        internal val WATCHED_SYMBOLS = listOf("open", "read", "write", "ioctl", "stat")

        private val EXPECTED_LIBC_PREFIXES = listOf(
            "/apex/com.android.runtime/lib64/bionic/",
            "/apex/com.android.runtime/lib/bionic/",
            "/system/lib64/",
            "/system/lib/",
        )
    }

    override fun run(): CheckResult {
        val name = "GOT integrity (partial -- see description)"
        val description = "Checks where libc.so is mapped from, as the precondition for GOT-slot " +
            "verification; cannot resolve actual GOT slot values without native code, so the " +
            "core invariant itself is reported as could-not-run, not not-found."

        val file = File("/proc/self/maps")
        val lines = try {
            file.readLines()
        } catch (e: Exception) {
            return CheckResult(
                name = name,
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf("readLines() on /proc/self/maps failed: ${e.javaClass.simpleName}: ${e.message}"),
                description = description,
            )
        }

        val libcMappings = lines.filter { line ->
            val fields = line.trim().split(Regex("\\s+"), limit = 6)
            fields.getOrNull(5)?.endsWith("/libc.so") == true
        }

        if (libcMappings.isEmpty()) {
            return CheckResult(
                name = name,
                outcome = Outcome.COULD_NOT_RUN,
                evidence = listOf(
                    "no mapping ending in /libc.so found in /proc/self/maps -- cannot even locate the " +
                        "library the watched symbols (${WATCHED_SYMBOLS.joinToString(", ")}) should resolve into, " +
                        "let alone resolve a GOT slot against it",
                ),
                description = description,
            )
        }

        val impostors = libcMappings.filterNot { line ->
            val fields = line.trim().split(Regex("\\s+"), limit = 6)
            val path = fields.getOrNull(5).orEmpty()
            EXPECTED_LIBC_PREFIXES.any { path.startsWith(it) }
        }

        val evidence = mutableListOf<String>()
        evidence.add("${libcMappings.size} mapping(s) ending in /libc.so found:")
        evidence.addAll(libcMappings.map { "  $it" })
        evidence.add(
            "Part 1 only (where libc.so is mapped from) was evaluated. Part 2 -- resolving what " +
                "any specific GOT slot for ${WATCHED_SYMBOLS.joinToString(", ")} actually holds, and " +
                "comparing that address against the range(s) above -- needs dlsym() or an ELF " +
                "relocation-table parser this pure-Kotlin app does not have, and was not evaluated.",
        )

        if (impostors.isNotEmpty()) {
            evidence.add("${impostors.size} mapping(s) named libc.so sit outside the expected system/APEX locations:")
            evidence.addAll(impostors.map { "  $it" })
            return CheckResult(
                name = name,
                outcome = Outcome.FOUND,
                evidence = evidence,
                description = description,
            )
        }

        return CheckResult(
            name = name,
            outcome = Outcome.COULD_NOT_RUN,
            evidence = evidence,
            description = description,
        )
    }
}
