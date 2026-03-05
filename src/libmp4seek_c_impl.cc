// libmp4seek_c_impl.cpp - Complete C interface implementation for libmp4seek

// This file is a wrapper to allow exception-safe C wrappers
// to use the C++ libmp4seek library and present a C ABI. You
// may need this one if you need the C ABI, as the implementation
// uses C++ features throughout: try/catch blocks with
// std::bad_alloc, std::exception, static_cast<>, nullptr,
// std::string (in the C++ overloads at the bottom), or
// C++ headers (<climits>, <cmath>, <cstdio>, <cstring>,
// <new>, <string>).

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include <libmp4seek_c.h>
#include <sys/stat.h>

// Include the actual libmp4seek C++ headers
#include <mp4extract.h>
#include <mp4seek.h>
#include <mp4seek_log.h>
#include <mp4seek_version.h>

// Utility function to check if file exists
static bool file_exists(const char* filename) {
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

extern "C" {

// ====================
// Utility Functions
// ====================

const char* mp4seek_get_error_string(mp4seek_error_t error) {
  switch (error) {
    case MP4SEEK_SUCCESS:
      return "Success";
    case MP4SEEK_ERROR_INVALID_PARAMS:
      return "Invalid parameters";
    case MP4SEEK_ERROR_FILE_NOT_FOUND:
      return "File not found";
    case MP4SEEK_ERROR_OPEN_FAILED:
      return "Failed to open file";
    case MP4SEEK_ERROR_NO_VIDEO_TRACK:
      return "No video track found";
    case MP4SEEK_ERROR_SEEK_FAILED:
      return "Seek operation failed";
    case MP4SEEK_ERROR_WRITE_FAILED:
      return "Write operation failed";
    case MP4SEEK_ERROR_READ_FAILED:
      return "Read operation failed";
    case MP4SEEK_ERROR_OUT_OF_RANGE:
      return "Seek position out of range";
    case MP4SEEK_ERROR_EXCEPTION:
      return "C++ exception occurred";
    case MP4SEEK_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case MP4SEEK_ERROR_UNKNOWN:
      return "Unknown error";
    default:
      return "Unknown error code";
  }
}

void mp4seek_get_version(char* version, size_t version_size) {
  if (version == nullptr || version_size == 0) {
    return;
  }

  snprintf(version, version_size, "%s", MP4SEEK_VERSION);
}

// ====================
// Configuration API
// ====================

void mp4seek_config_init(mp4seek_config_t* config) {
  if (config == nullptr) {
    return;
  }

  config->debug_level = 0;
  config->accurate_seek = false;
}

// ====================
// Main Seek API
// ====================

mp4seek_error_t mp4seek_by_time(
    const char* infile,
    const char* outfile,
    float start_time,
    const mp4seek_config_t* config) {
  if (infile == nullptr || outfile == nullptr) {
    return MP4SEEK_ERROR_INVALID_PARAMS;
  }

  if (!file_exists(infile)) {
    return MP4SEEK_ERROR_FILE_NOT_FOUND;
  }

  try {
    int debug_level = (config != nullptr) ? config->debug_level : 0;
    bool accurate_seek = (config != nullptr) ? config->accurate_seek : false;

    // Call the C++ mp4seek function with time-based seeking
    // start_frame and start_pts are set to INT64_MIN to indicate "not used"
    int result =
        mp4seek(infile, outfile, debug_level, start_time, INT64_MIN, INT64_MIN, accurate_seek);

    if (result != 0) {
      return MP4SEEK_ERROR_SEEK_FAILED;
    }

    return MP4SEEK_SUCCESS;
  } catch (const std::bad_alloc&) {
    return MP4SEEK_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    MP4SEEK_LOGE("mp4seek exception: %s", e.what());
    return MP4SEEK_ERROR_EXCEPTION;
  } catch (...) {
    return MP4SEEK_ERROR_UNKNOWN;
  }
}

mp4seek_error_t mp4seek_by_frame(
    const char* infile,
    const char* outfile,
    int64_t start_frame,
    const mp4seek_config_t* config) {
  if (infile == nullptr || outfile == nullptr) {
    return MP4SEEK_ERROR_INVALID_PARAMS;
  }

  if (!file_exists(infile)) {
    return MP4SEEK_ERROR_FILE_NOT_FOUND;
  }

  try {
    int debug_level = (config != nullptr) ? config->debug_level : 0;
    bool accurate_seek = (config != nullptr) ? config->accurate_seek : false;

    // Call the C++ mp4seek function with frame-based seeking
    // start (time) is set to NAN and start_pts is set to INT64_MIN to indicate
    // "not used"
    int result = mp4seek(infile, outfile, debug_level, NAN, start_frame, INT64_MIN, accurate_seek);

    if (result != 0) {
      return MP4SEEK_ERROR_SEEK_FAILED;
    }

    return MP4SEEK_SUCCESS;
  } catch (const std::bad_alloc&) {
    return MP4SEEK_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    MP4SEEK_LOGE("mp4seek exception: %s", e.what());
    return MP4SEEK_ERROR_EXCEPTION;
  } catch (...) {
    return MP4SEEK_ERROR_UNKNOWN;
  }
}

mp4seek_error_t mp4seek_by_pts(
    const char* infile,
    const char* outfile,
    int64_t start_pts,
    const mp4seek_config_t* config) {
  if (infile == nullptr || outfile == nullptr) {
    return MP4SEEK_ERROR_INVALID_PARAMS;
  }

  if (!file_exists(infile)) {
    return MP4SEEK_ERROR_FILE_NOT_FOUND;
  }

  try {
    int debug_level = (config != nullptr) ? config->debug_level : 0;
    bool accurate_seek = (config != nullptr) ? config->accurate_seek : false;

    // Call the C++ mp4seek function with PTS-based seeking
    // start (time) is set to NAN and start_frame is set to INT64_MIN to
    // indicate "not used"
    int result = mp4seek(infile, outfile, debug_level, NAN, INT64_MIN, start_pts, accurate_seek);

    if (result != 0) {
      return MP4SEEK_ERROR_SEEK_FAILED;
    }

    return MP4SEEK_SUCCESS;
  } catch (const std::bad_alloc&) {
    return MP4SEEK_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    MP4SEEK_LOGE("mp4seek exception: %s", e.what());
    return MP4SEEK_ERROR_EXCEPTION;
  } catch (...) {
    return MP4SEEK_ERROR_UNKNOWN;
  }
}

// ====================
// Frame Extraction API
// ====================

mp4seek_error_t mp4seek_extract_frames(
    const char* infile,
    int track_num,
    int start_frame,
    int end_frame,
    mp4seek_frame_t** out_frames,
    int* out_frame_count) {
  if (infile == nullptr || out_frames == nullptr || out_frame_count == nullptr) {
    return MP4SEEK_ERROR_INVALID_PARAMS;
  }

  if (!file_exists(infile)) {
    return MP4SEEK_ERROR_FILE_NOT_FOUND;
  }

  *out_frames = nullptr;
  *out_frame_count = 0;

  try {
    // Call the C++ mp4extractframes function
    Mp4Frame* cpp_frames = nullptr;
    int cpp_frame_count = 0;

    int result =
        mp4extractframes(infile, track_num, start_frame, end_frame, &cpp_frames, &cpp_frame_count);

    if (result != 0 || cpp_frames == nullptr) {
      return MP4SEEK_ERROR_READ_FAILED;
    }

    // Allocate C-compatible frame array using malloc
    mp4seek_frame_t* c_frames =
        static_cast<mp4seek_frame_t*>(malloc(cpp_frame_count * sizeof(mp4seek_frame_t)));
    if (c_frames == nullptr) {
      mp4extractframes_free(cpp_frames, cpp_frame_count);
      return MP4SEEK_ERROR_OUT_OF_MEMORY;
    }

    // Copy frame data into malloc'd buffers so the C wrapper owns
    // everything with malloc/free, independent of how mp4extractframes
    // allocates internally.
    for (int i = 0; i < cpp_frame_count; i++) {
      c_frames[i].size = cpp_frames[i].size;
      c_frames[i].data = static_cast<uint8_t*>(malloc(cpp_frames[i].size));
      if (c_frames[i].data == nullptr) {
        // Free already-copied frames
        for (int j = 0; j < i; j++) {
          free(c_frames[j].data);
        }
        free(c_frames);
        mp4extractframes_free(cpp_frames, cpp_frame_count);
        return MP4SEEK_ERROR_OUT_OF_MEMORY;
      }
      memcpy(c_frames[i].data, cpp_frames[i].data, cpp_frames[i].size);
    }

    // Free all C++ originals
    mp4extractframes_free(cpp_frames, cpp_frame_count);

    *out_frames = c_frames;
    *out_frame_count = cpp_frame_count;

    return MP4SEEK_SUCCESS;
  } catch (const std::bad_alloc&) {
    return MP4SEEK_ERROR_OUT_OF_MEMORY;
  } catch (const std::exception& e) {
    MP4SEEK_LOGE("mp4seek exception: %s", e.what());
    return MP4SEEK_ERROR_EXCEPTION;
  } catch (...) {
    return MP4SEEK_ERROR_UNKNOWN;
  }
}

void mp4seek_free_frames(mp4seek_frame_t* frames, int frame_count) {
  if (frames == nullptr) {
    return;
  }

  for (int i = 0; i < frame_count; i++) {
    free(frames[i].data);
  }
  free(frames);
}

} // extern "C"

// ====================
// C++ std::string Wrappers
// ====================

mp4seek_error_t mp4seek_by_time(
    const std::string& infile,
    const std::string& outfile,
    float start_time,
    const mp4seek_config_t* config) {
  return mp4seek_by_time(infile.c_str(), outfile.c_str(), start_time, config);
}

mp4seek_error_t mp4seek_by_frame(
    const std::string& infile,
    const std::string& outfile,
    int64_t start_frame,
    const mp4seek_config_t* config) {
  return mp4seek_by_frame(infile.c_str(), outfile.c_str(), start_frame, config);
}

mp4seek_error_t mp4seek_by_pts(
    const std::string& infile,
    const std::string& outfile,
    int64_t start_pts,
    const mp4seek_config_t* config) {
  return mp4seek_by_pts(infile.c_str(), outfile.c_str(), start_pts, config);
}

mp4seek_error_t mp4seek_extract_frames(
    const std::string& infile,
    int track_num,
    int start_frame,
    int end_frame,
    mp4seek_frame_t** out_frames,
    int* out_frame_count) {
  return mp4seek_extract_frames(
      infile.c_str(), track_num, start_frame, end_frame, out_frames, out_frame_count);
}
