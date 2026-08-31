APP_ABI      := arm64-v8a
APP_PLATFORM := android-29
# zygisk.hpp's REGISTER_ZYGISK_MODULE macro expands into entry_impl<T>(),
# which declares function-local statics (`static Api api; static T module;`).
# Their once-only initialisation is guarded by __cxa_guard_acquire /
# __cxa_guard_release, which come from the C++ runtime - so APP_STL := none
# cannot link this header at all, regardless of what main.cpp includes.
# c++_static (not c++_shared) is used so libzygisklab_armed.so stays
# self-contained: c++_shared would require shipping a separate
# libc++_shared.so in the package, which build.sh does not package.
APP_STL      := c++_static
APP_CPPFLAGS := -fno-exceptions -fno-rtti
APP_CFLAGS   := -Oz -flto
APP_LDFLAGS  := -flto -Wl,--gc-sections
