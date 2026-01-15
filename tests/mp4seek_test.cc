#include "include/mp4seek.h"

#include <gtest/gtest.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "include/mp4extract.h"

// Test video properties (vid/metiq.30fps.10sec.mp4):
// - Video: 300 frames, 30fps, timescale 15360, frame duration 512
// - Keyframes at 0-based frames: 0, 30, 60, 90, 120, 150, 180, 210, 240, 270
// - Audio: 158 samples, timescale 16000
// - Duration: 10 seconds

static const char* kTestVideoPath = "vid/metiq.30fps.10sec.mp4";
static const int kVideoTrack = 1;
static const int kAudioTrack = 2;
static const int kTotalVideoFrames = 300;
static const int kTotalAudioFrames = 158;
static const int kVideoTimescale = 15360;
static const int kFrameDuration = 512;  // timescale units per frame

// Test case definition
struct Mp4SeekTestCase {
  const char* name;

  // mp4seek parameters
  float start;          // NAN if not used
  int64_t start_frame;  // INT64_MIN if not used
  int64_t start_pts;    // INT64_MIN if not used
  bool accurate_seek;

  // Expected 0-based keyframe that mp4seek will snap to
  int expected_video_keyframe;
};

// Test cases: 2 examples for each of 6 categories
// Note: mp4seek finds the keyframe STRICTLY BEFORE the target frame.
// If the target is exactly a keyframe, it uses the previous keyframe.
static const Mp4SeekTestCase kTestCases[] = {
    // 1. Trim from beginning (positive --start)
    {"start_3p5s", 3.5f, INT64_MIN, INT64_MIN, false,
     90},  // 3.5s -> frame 105, keyframe 90
    {"start_1p0s", 1.0f, INT64_MIN, INT64_MIN, false,
     0},  // 1.0s -> frame 30 (keyframe), uses prev keyframe 0

    // 2. Trim from end (negative --start)
    {"start_neg2p0s", -2.0f, INT64_MIN, INT64_MIN, false,
     210},  // -2s = 8s -> frame 240 (keyframe), uses prev 210
    {"start_neg5p5s", -5.5f, INT64_MIN, INT64_MIN, false,
     120},  // -5.5s = 4.5s -> frame 135, keyframe 120

    // 3. Frame-accurate trim (positive --start_frame)
    {"start_frame_100", NAN, 100, INT64_MIN, false,
     90},  // frame 100, keyframe 90
    {"start_frame_150", NAN, 150, INT64_MIN, false,
     120},  // frame 150 (keyframe), uses prev keyframe 120

    // 4. Frame-accurate trim from end (negative --start_frame)
    {"start_frame_neg50", NAN, -50, INT64_MIN, false,
     240},  // frame 250, keyframe 240
    {"start_frame_neg100", NAN, -100, INT64_MIN, false,
     180},  // frame 200, keyframe 180

    // 5. PTS-accurate trim (positive --start_pts)
    // PTS = frame * 512 (frame duration in timescale units)
    {"start_pts_51200", NAN, INT64_MIN, 51200, false,
     90},  // PTS 51200 = frame 100, keyframe 90
    {"start_pts_76800", NAN, INT64_MIN, 76800, false,
     120},  // PTS 76800 = frame 150 (keyframe), uses prev 120

    // 6. PTS-accurate trim from end (negative --start_pts)
    {"start_pts_neg25600", NAN, INT64_MIN, -25600, false,
     240},  // -50 frames from end, frame 250, keyframe 240
    {"start_pts_neg51200", NAN, INT64_MIN, -51200, false,
     180},  // -100 frames from end, frame 200, keyframe 180
};

class Mp4SeekTest : public ::testing::TestWithParam<Mp4SeekTestCase> {
 protected:
  void SetUp() override {
    // Create temp output file path
    output_path_ = "/tmp/mp4seek_test_output_" + std::to_string(getpid()) +
                   "_" + std::to_string(rand()) + ".mp4";
  }

  void TearDown() override {
    // Clean up temp file
    std::remove(output_path_.c_str());
  }

  std::string output_path_;
};

TEST_P(Mp4SeekTest, VideoFramesMatch) {
  const Mp4SeekTestCase& tc = GetParam();

  // Run mp4seek
  int result =
      mp4seek(kTestVideoPath, output_path_.c_str(), -1,  // quiet
              tc.start, tc.start_frame, tc.start_pts, tc.accurate_seek);
  ASSERT_EQ(0, result) << "mp4seek failed for test: " << tc.name;

  // Calculate expected frame count
  int expected_frame_count = kTotalVideoFrames - tc.expected_video_keyframe;

  // Extract frames from output (all frames)
  Mp4Frame* output_frames = nullptr;
  int output_frame_count = 0;
  result = mp4extractframes(output_path_.c_str(), kVideoTrack, 0, -1,
                            &output_frames, &output_frame_count);
  ASSERT_EQ(0, result) << "Failed to extract output video frames";
  ASSERT_EQ(expected_frame_count, output_frame_count)
      << "Unexpected output frame count for test: " << tc.name;

  // Extract corresponding frames from input
  Mp4Frame* input_frames = nullptr;
  int input_frame_count = 0;
  result = mp4extractframes(kTestVideoPath, kVideoTrack,
                            tc.expected_video_keyframe, kTotalVideoFrames - 1,
                            &input_frames, &input_frame_count);
  ASSERT_EQ(0, result) << "Failed to extract input video frames";
  ASSERT_EQ(expected_frame_count, input_frame_count)
      << "Unexpected input frame count for test: " << tc.name;

  // Compare frames
  for (int i = 0; i < expected_frame_count; i++) {
    ASSERT_EQ(input_frames[i].size, output_frames[i].size)
        << "Frame " << i << " size mismatch in test: " << tc.name;
    ASSERT_EQ(0, memcmp(input_frames[i].data, output_frames[i].data,
                        input_frames[i].size))
        << "Frame " << i << " data mismatch in test: " << tc.name;
  }

  // Clean up
  mp4extractframes_free(input_frames, input_frame_count);
  mp4extractframes_free(output_frames, output_frame_count);
}

TEST_P(Mp4SeekTest, AudioFramesMatch) {
  const Mp4SeekTestCase& tc = GetParam();

  // Run mp4seek
  int result =
      mp4seek(kTestVideoPath, output_path_.c_str(), -1,  // quiet
              tc.start, tc.start_frame, tc.start_pts, tc.accurate_seek);
  ASSERT_EQ(0, result) << "mp4seek failed for test: " << tc.name;

  // Extract all audio frames from output
  Mp4Frame* output_frames = nullptr;
  int output_frame_count = 0;
  result = mp4extractframes(output_path_.c_str(), kAudioTrack, 0, -1,
                            &output_frames, &output_frame_count);
  ASSERT_EQ(0, result) << "Failed to extract output audio frames";
  ASSERT_GT(output_frame_count, 0) << "No audio frames in output";

  // Calculate expected audio start sample
  // Audio is synced to video cut point, so we need to find the audio sample
  // at the video keyframe time
  // Video keyframe time = keyframe * 512 / 15360 seconds
  // Audio sample = time * 16000 / 1024 (AAC samples per frame)
  double video_cut_time_sec = static_cast<double>(tc.expected_video_keyframe) *
                              kFrameDuration / kVideoTimescale;
  int expected_audio_start =
      static_cast<int>(video_cut_time_sec * 16000.0 / 1024.0);
  if (expected_audio_start >= kTotalAudioFrames) {
    expected_audio_start = kTotalAudioFrames - 1;
  }

  int expected_audio_count = kTotalAudioFrames - expected_audio_start;

  // Allow some tolerance for audio sync (within 1 frame)
  ASSERT_NEAR(expected_audio_count, output_frame_count, 1)
      << "Unexpected audio frame count for test: " << tc.name;

  // Extract corresponding frames from input
  int input_start = kTotalAudioFrames - output_frame_count;
  Mp4Frame* input_frames = nullptr;
  int input_frame_count = 0;
  result = mp4extractframes(kTestVideoPath, kAudioTrack, input_start,
                            kTotalAudioFrames - 1, &input_frames,
                            &input_frame_count);
  ASSERT_EQ(0, result) << "Failed to extract input audio frames";
  ASSERT_EQ(output_frame_count, input_frame_count)
      << "Audio frame count mismatch for test: " << tc.name;

  // Compare frames
  for (int i = 0; i < input_frame_count; i++) {
    ASSERT_EQ(input_frames[i].size, output_frames[i].size)
        << "Audio frame " << i << " size mismatch in test: " << tc.name;
    ASSERT_EQ(0, memcmp(input_frames[i].data, output_frames[i].data,
                        input_frames[i].size))
        << "Audio frame " << i << " data mismatch in test: " << tc.name;
  }

  // Clean up
  mp4extractframes_free(input_frames, input_frame_count);
  mp4extractframes_free(output_frames, output_frame_count);
}

INSTANTIATE_TEST_SUITE_P(
    Mp4SeekTests, Mp4SeekTest, ::testing::ValuesIn(kTestCases),
    [](const ::testing::TestParamInfo<Mp4SeekTestCase>& info) {
      return info.param.name;
    });
