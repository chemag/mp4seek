#ifndef MP4SEEK_H_
#define MP4SEEK_H_

#include <cstdint>

// mp4seek: Trim MP4 files at keyframe boundaries without transcoding
//
// Parameters:
//   infile: Input MP4 file path
//   outfile: Output MP4 file path
//   debug_level: Debug verbosity (-1=quiet, 0=normal, 1+=verbose)
//   start: Start time in seconds (NAN if not used, negative = from end)
//   start_frame: Start frame number (INT64_MIN if not used, negative = from
//   end) start_pts: Start PTS in video timescale (INT64_MIN if not used,
//   negative = from end) accurate_seek: If true, use EDTS to seek to exact
//   position
//
// Returns: 0 on success, non-zero on error
int mp4seek(const char* infile, const char* outfile, int debug_level,
            float start, int64_t start_frame, int64_t start_pts,
            bool accurate_seek);

#endif  // MP4SEEK_H_
