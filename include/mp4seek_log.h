#pragma once

// Platform-dependent logging for mp4seek.
// On Linux/desktop, uses fprintf(stderr, ...).

#include <stdio.h>

#define MP4SEEK_LOGE(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGW(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGI(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MP4SEEK_LOGD(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
