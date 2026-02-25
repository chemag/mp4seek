#pragma once

// Platform-dependent logging for mp4seek.
// On Android, uses __android_log_print (logcat).
// On Linux/desktop, uses fprintf(stderr, ...).

#ifdef __ANDROID__

#include <android/log.h>

#define MP4SEEK_LOG_TAG "mp4seek"
#define MP4SEEK_LOGE(fmt, ...) \
  __android_log_print(ANDROID_LOG_ERROR, MP4SEEK_LOG_TAG, fmt, ##__VA_ARGS__)
#define MP4SEEK_LOGW(fmt, ...) \
  __android_log_print(ANDROID_LOG_WARN, MP4SEEK_LOG_TAG, fmt, ##__VA_ARGS__)
#define MP4SEEK_LOGI(fmt, ...) \
  __android_log_print(ANDROID_LOG_INFO, MP4SEEK_LOG_TAG, fmt, ##__VA_ARGS__)
#define MP4SEEK_LOGD(fmt, ...) \
  __android_log_print(ANDROID_LOG_DEBUG, MP4SEEK_LOG_TAG, fmt, ##__VA_ARGS__)

#else

#include <stdio.h>

#define MP4SEEK_LOGE(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGW(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGI(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGD(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)

#endif
