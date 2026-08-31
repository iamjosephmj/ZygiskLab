// Root build file for the Lab 7 detection harness. This is the one Gradle
// project in the repository -- every other module under modules/ is a bare
// ndk-build .so, because a shell script or a native-only module can't do
// what this app does: observe a process from inside that process, the same
// vantage point a real app has. See modules/README.md and the Lab 7 README
// for why that distinction matters.
plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.0.21" apply false
}
