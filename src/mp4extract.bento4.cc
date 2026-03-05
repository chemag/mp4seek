#include <mp4extract.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Ap4.h"
#include <mp4seek_log.h>

int mp4extractframes(const char* infile, int track_num, int start_frame,
                     int end_frame, Mp4Frame** out_frames,
                     int* out_frame_count) {
  // Validate output parameters
  if (out_frames == nullptr || out_frame_count == nullptr) {
    MP4SEEK_LOGE("Error: output parameters cannot be null");
    return 1;
  }

  *out_frames = nullptr;
  *out_frame_count = 0;

  // Open input file
  AP4_ByteStream* input = nullptr;
  AP4_Result result = AP4_FileByteStream::Create(
      infile, AP4_FileByteStream::STREAM_MODE_READ, input);
  if (AP4_FAILED(result)) {
    MP4SEEK_LOGE("Error: cannot open input file: %s", infile);
    return 1;
  }

  AP4_File file(*input, true);
  input->Release();

  AP4_Movie* movie = file.GetMovie();
  if (movie == nullptr) {
    MP4SEEK_LOGE("Error: no movie found in file");
    return 1;
  }

  // Find the requested track (1-based index)
  AP4_Track* track = movie->GetTrack(static_cast<AP4_UI32>(track_num));
  if (track == nullptr) {
    MP4SEEK_LOGE("Error: track %d not found", track_num);
    return 1;
  }

  AP4_Cardinal total_samples = track->GetSampleCount();
  if (total_samples == 0) {
    MP4SEEK_LOGE("Error: track %d has no samples", track_num);
    return 1;
  }

  // Validate frame range
  if (start_frame < 0) {
    start_frame = 0;
  }
  if (end_frame < 0 || end_frame >= static_cast<int>(total_samples)) {
    end_frame = static_cast<int>(total_samples) - 1;
  }
  if (start_frame > end_frame) {
    MP4SEEK_LOGE("Error: start_frame (%d) > end_frame (%d)", start_frame,
            end_frame);
    return 1;
  }

  int frame_count = end_frame - start_frame + 1;

  // Allocate frame array
  Mp4Frame* frames = new (std::nothrow) Mp4Frame[frame_count];
  if (frames == nullptr) {
    MP4SEEK_LOGE("Error: failed to allocate frame array");
    return 1;
  }

  // Initialize all frames to null
  for (int i = 0; i < frame_count; i++) {
    frames[i].data = nullptr;
    frames[i].size = 0;
  }

  // Extract each frame
  for (int i = 0; i < frame_count; i++) {
    AP4_Ordinal sample_index = static_cast<AP4_Ordinal>(start_frame + i);
    AP4_Sample sample;

    result = track->GetSample(sample_index, sample);
    if (AP4_FAILED(result)) {
      MP4SEEK_LOGE("Error: could not get sample %d", sample_index);
      mp4extractframes_free(frames, frame_count);
      return 1;
    }

    AP4_DataBuffer sample_data;
    result = sample.ReadData(sample_data);
    if (AP4_FAILED(result)) {
      MP4SEEK_LOGE("Error: could not read sample data for sample %d",
              sample_index);
      mp4extractframes_free(frames, frame_count);
      return 1;
    }

    // Allocate and copy frame data
    size_t data_size = sample_data.GetDataSize();
    uint8_t* data = new (std::nothrow) uint8_t[data_size];
    if (data == nullptr) {
      MP4SEEK_LOGE("Error: failed to allocate frame data for sample %d",
              sample_index);
      mp4extractframes_free(frames, frame_count);
      return 1;
    }

    memcpy(data, sample_data.GetData(), data_size);
    frames[i].data = data;
    frames[i].size = data_size;
  }

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
