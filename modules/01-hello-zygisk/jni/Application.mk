APP_ABI      := arm64-v8a
APP_PLATFORM := android-29
APP_STL      := c++_static
APP_CPPFLAGS := -fno-exceptions -fno-rtti
APP_CFLAGS   := -Oz -flto
APP_LDFLAGS  := -flto -Wl,--gc-sections
