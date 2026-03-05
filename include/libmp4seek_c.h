// libmp4seek_c.h - C/C++ interface for libmp4seek to support Android code with exceptions disabled
// This provides exception-safe wrappers for the C++ libmp4seek library with proper error handling

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

// Error codes
typedef enum {
  MP4SEEK_SUCCESS = 0,
  MP4SEEK_ERROR_INVALID_PARAMS = -1,
  MP4SEEK_ERROR_FILE_NOT_FOUND = -2,
  MP4SEEK_ERROR_OPEN_FAILED = -3,
  MP4SEEK_ERROR_NO_VIDEO_TRACK = -4,
  MP4SEEK_ERROR_SEEK_FAILED = -5,
  MP4SEEK_ERROR_WRITE_FAILED = -6,
  MP4SEEK_ERROR_READ_FAILED = -7,
  MP4SEEK_ERROR_OUT_OF_RANGE = -8,
  MP4SEEK_ERROR_EXCEPTION = -9,
  MP4SEEK_ERROR_OUT_OF_MEMORY = -10,
  MP4SEEK_ERROR_UNKNOWN = -11
} mp4seek_error_t;

// ====================
// Configuration API
// ====================

// Configuration structure for mp4seek
typedef struct {
  int debug_level; // Debug verbosity (-1=quiet, 0=normal, 1+=verbose)
  bool accurate_seek; // If true, use EDTS to seek to exact position
} mp4seek_config_t;

// Initialize configuration with default values
void mp4seek_config_init(mp4seek_config_t* config);

// ====================
// Seek Mode Enums
// ====================

// Seek mode determines how the start position is specified
typedef enum {
  MP4SEEK_MODE_TIME = 0, // Start time in seconds
  MP4SEEK_MODE_FRAME = 1, // Start frame number (0-based)
  MP4SEEK_MODE_PTS = 2 // Start PTS in video timescale
} mp4seek_mode_t;

// ====================
// Main Seek API
// ====================

// Seek MP4 file by time (in seconds)
// Negative start_time means from end of file
// Returns: 0 on success, error code otherwise
mp4seek_error_t mp4seek_by_time(
    const char* infile,
    const char* outfile,
    float start_time,
    const mp4seek_config_t* config);

// Seek MP4 file by frame number (0-based)
// Negative start_frame means from end of file
// Returns: 0 on success, error code otherwise
mp4seek_error_t mp4seek_by_frame(
    const char* infile,
    const char* outfile,
    int64_t start_frame,
    const mp4seek_config_t* config);

// Seek MP4 file by PTS value in video timescale
// Negative start_pts means from end of file
// Returns: 0 on success, error code otherwise
mp4seek_error_t mp4seek_by_pts(
    const char* infile,
    const char* outfile,
    int64_t start_pts,
    const mp4seek_config_t* config);

// ====================
// Frame Extraction API
// ====================

// Frame data structure (C-compatible version of Mp4Frame)
typedef struct {
  uint8_t* data; // Pointer to frame data
  size_t size; // Size of frame data in bytes
} mp4seek_frame_t;

// Extract frames from a track
// Parameters:
//   infile: Input MP4 file path
//   track_num: Track number (1-based, typically 1=video, 2=audio)
//   start_frame: First frame to extract (0-based, inclusive)
//   end_frame: Last frame to extract (0-based, inclusive, -1 = to end)
//   out_frames: Output pointer to array of frames (allocated by function)
//   out_frame_count: Output number of frames extracted
// Returns: 0 on success, error code otherwise
// On success, caller must use mp4seek_free_frames() to free the frames
mp4seek_error_t mp4seek_extract_frames(
    const char* infile,
    int track_num,
    int start_frame,
    int end_frame,
    mp4seek_frame_t** out_frames,
    int* out_frame_count);

// Free frames allocated by mp4seek_extract_frames
void mp4seek_free_frames(mp4seek_frame_t* frames, int frame_count);

// ====================
// Utility Functions
// ====================

// Get library version
void mp4seek_get_version(char* version, size_t version_size);

// Get error string for error code
const char* mp4seek_get_error_string(mp4seek_error_t error);

#ifdef __cplusplus
}

// ====================
// C++ std::string Overloads
// ====================
// These overloads provide std::string support for C++ callers

mp4seek_error_t mp4seek_by_time(
    const std::string& infile,
    const std::string& outfile,
    float start_time,
    const mp4seek_config_t* config);

mp4seek_error_t mp4seek_by_frame(
    const std::string& infile,
    const std::string& outfile,
    int64_t start_frame,
    const mp4seek_config_t* config);

mp4seek_error_t mp4seek_by_pts(
    const std::string& infile,
    const std::string& outfile,
    int64_t start_pts,
    const mp4seek_config_t* config);

mp4seek_error_t mp4seek_extract_frames(
    const std::string& infile,
    int track_num,
    int start_frame,
    int end_frame,
    mp4seek_frame_t** out_frames,
    int* out_frame_count);

#endif
