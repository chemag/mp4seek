#include "include/mp4extract.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <mp4v2/mp4v2.h>

int mp4extractframes(const char* infile, int track_num, int start_frame,
                     int end_frame, Mp4Frame** out_frames,
                     int* out_frame_count) {
  // Validate output parameters
  if (out_frames == nullptr || out_frame_count == nullptr) {
    fprintf(stderr, "Error: output parameters cannot be null\n");
    return 1;
  }

  *out_frames = nullptr;
  *out_frame_count = 0;

  // Open input file
  MP4FileHandle hFile = MP4Read(infile);
  if (hFile == MP4_INVALID_FILE_HANDLE) {
    fprintf(stderr, "Error: cannot open input file: %s\n", infile);
    return 1;
  }

  // Find the requested track (1-based index)
  // mp4v2 uses MP4FindTrackId with 0-based index to get track ID
  MP4TrackId trackId = MP4FindTrackId(hFile, track_num - 1);
  if (trackId == MP4_INVALID_TRACK_ID) {
    fprintf(stderr, "Error: track %d not found\n", track_num);
    MP4Close(hFile);
    return 1;
  }

  MP4SampleId total_samples = MP4GetTrackNumberOfSamples(hFile, trackId);
  if (total_samples == 0) {
    fprintf(stderr, "Error: track %d has no samples\n", track_num);
    MP4Close(hFile);
    return 1;
  }

  // Validate frame range
  // Note: mp4v2 uses 1-based sample IDs, but we use 0-based frame indices
  if (start_frame < 0) {
    start_frame = 0;
  }
  if (end_frame < 0 || end_frame >= static_cast<int>(total_samples)) {
    end_frame = static_cast<int>(total_samples) - 1;
  }
  if (start_frame > end_frame) {
    fprintf(stderr, "Error: start_frame (%d) > end_frame (%d)\n", start_frame,
            end_frame);
    MP4Close(hFile);
    return 1;
  }

  int frame_count = end_frame - start_frame + 1;

  // Allocate frame array
  Mp4Frame* frames = new (std::nothrow) Mp4Frame[frame_count];
  if (frames == nullptr) {
    fprintf(stderr, "Error: failed to allocate frame array\n");
    MP4Close(hFile);
    return 1;
  }

  // Initialize all frames to null
  for (int i = 0; i < frame_count; i++) {
    frames[i].data = nullptr;
    frames[i].size = 0;
  }

  // Extract each frame
  for (int i = 0; i < frame_count; i++) {
    // mp4v2 uses 1-based sample IDs
    MP4SampleId sampleId = static_cast<MP4SampleId>(start_frame + i + 1);

    uint8_t* pBytes = nullptr;
    uint32_t numBytes = 0;

    bool success = MP4ReadSample(hFile, trackId, sampleId, &pBytes, &numBytes,
                                 nullptr, nullptr, nullptr, nullptr);
    if (!success || pBytes == nullptr) {
      fprintf(stderr, "Error: could not read sample %d\n", sampleId);
      mp4extractframes_free(frames, frame_count);
      MP4Close(hFile);
      return 1;
    }

    // Allocate and copy frame data (mp4v2 allocates pBytes, we need our own copy)
    uint8_t* data = new (std::nothrow) uint8_t[numBytes];
    if (data == nullptr) {
      fprintf(stderr, "Error: failed to allocate frame data for sample %d\n",
              sampleId);
      free(pBytes);  // mp4v2 uses malloc
      mp4extractframes_free(frames, frame_count);
      MP4Close(hFile);
      return 1;
    }

    memcpy(data, pBytes, numBytes);
    frames[i].data = data;
    frames[i].size = numBytes;

    free(pBytes);  // mp4v2 uses malloc for sample data
  }

  MP4Close(hFile);

  *out_frames = frames;
  *out_frame_count = frame_count;

  return 0;
}

void mp4extractframes_free(Mp4Frame* frames, int frame_count) {
  if (frames == nullptr) {
    return;
  }

  for (int i = 0; i < frame_count; i++) {
    delete[] frames[i].data;
  }
  delete[] frames;
}
