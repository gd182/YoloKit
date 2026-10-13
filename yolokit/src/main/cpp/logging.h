#pragma once

#include <android/log.h>

#ifndef YOLOKIT_ENABLE_DIAGNOSTICS
#define YOLOKIT_ENABLE_DIAGNOSTICS 0
#endif

#if YOLOKIT_ENABLE_DIAGNOSTICS
#define YOLOKIT_LOG_INFO(tag, ...) __android_log_print(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#else
#define YOLOKIT_LOG_INFO(tag, ...) ((void)0)
#endif

#define YOLOKIT_LOG_ERROR(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)
