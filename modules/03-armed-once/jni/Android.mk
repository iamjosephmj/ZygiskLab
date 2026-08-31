LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := zygisklab_armed
LOCAL_SRC_FILES := main.cpp
LOCAL_LDLIBS    := -llog
LOCAL_CPPFLAGS  := -std=c++20 -fvisibility=hidden -fvisibility-inlines-hidden
include $(BUILD_SHARED_LIBRARY)
