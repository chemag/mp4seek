#pragma once

#include <cstddef>
#include <cstdint>

// Frame data structure
struct Mp4Frame {
  uint8_t* data;  // Pointer to frame data
  size_t size;    // Size of frame data in bytes
};

// mp4extractframes: Extract frames from a track
//
// Parameters:
//   infile: Input MP4 file path
//   track_num: Track number (1-based, typically 1=video, 2=audio)
//   start_frame: First frame to extract (0-based, inclusive)
//   end_frame: Last frame to extract (0-based, inclusive, -1 = to end)
//   out_frames: Output pointer to array of Mp4Frame (allocated by function)
//   out_frame_count: Output number of frames extracted
//
// Returns: 0 on success, non-zero on error
// On success, caller must use mp4extractframes_free() to free the frames
int mp4extractframes(const char* infile, int track_num, int start_frame,
                     int end_frame, Mp4Frame** out_frames,
                     int* out_frame_count);

// mp4extractframes_free: Free frames allocated by mp4extractframes
//
// Parameters:
//   frames: Array of frames to free
//   frame_count: Number of frames in the array
void mp4extractframes_free(Mp4Frame* frames, int frame_count);
