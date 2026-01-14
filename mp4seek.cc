#include <getopt.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "Ap4.h"

// Command line options
struct ArgOptions {
  int debug;
  float sseof;
  const char* infile;
  const char* outfile;
};

const ArgOptions DEFAULT_OPTIONS{
    .debug = 0,
    .sseof = 0.0f,
    .infile = nullptr,
    .outfile = nullptr,
};

void print_usage(const char* program_name) {
  fprintf(stderr, "usage: %s [options] <infile> <outfile>\n", program_name);
  fprintf(stderr, "where options are:\n");
  fprintf(stderr, "\t-d, --debug:\tIncrease debug verbosity [%i]\n",
          DEFAULT_OPTIONS.debug);
  fprintf(stderr, "\t-q, --quiet:\tSet debug verbosity to -1\n");
  fprintf(stderr,
          "\t--sseof <float>:\tSeek position in seconds from EOF [%.1f]\n",
          DEFAULT_OPTIONS.sseof);
  fprintf(stderr, "\t-h, --help:\tShow this help message\n");
}

// Long options with no equivalent short option
enum {
  QUIET_OPTION = CHAR_MAX + 1,
  SSEOF_OPTION,
  HELP_OPTION,
};

ArgOptions* parse_args(int argc, char** argv) {
  int c;
  char* endptr;
  static ArgOptions options;

  // Set default option values
  options = DEFAULT_OPTIONS;

  // getopt_long stores the option index here
  int optindex = 0;

  // Long options
  static struct option longopts[] = {
      // Matching options to short options
      {"debug", no_argument, nullptr, 'd'},
      // Options without a short option
      {"quiet", no_argument, nullptr, QUIET_OPTION},
      {"sseof", required_argument, nullptr, SSEOF_OPTION},
      {"help", no_argument, nullptr, HELP_OPTION},
      {nullptr, 0, nullptr, 0}};

  // Parse arguments
  while (true) {
    c = getopt_long(argc, argv, "dqh", longopts, &optindex);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 0:
        // Long options that define flag
        if (longopts[optindex].flag != nullptr) {
          break;
        }
        break;

      case 'd':
        options.debug += 1;
        break;

      case 'q':
      case QUIET_OPTION:
        options.debug = -1;
        break;

      case SSEOF_OPTION:
        options.sseof = strtof(optarg, &endptr);
        if (*endptr != '\0') {
          fprintf(stderr, "Error: Invalid float value for --sseof: %s\n",
                  optarg);
          print_usage(argv[0]);
          return nullptr;
        }
        break;

      case HELP_OPTION:
      case 'h':
        print_usage(argv[0]);
        exit(0);

      default:
        fprintf(stderr, "Unsupported option: %c\n", c);
        print_usage(argv[0]);
        return nullptr;
    }
  }

  // Get positional arguments (infile, outfile)
  int remaining = argc - optind;
  if (remaining < 2) {
    fprintf(stderr, "Error: Both infile and outfile are required\n");
    print_usage(argv[0]);
    return nullptr;
  }
  if (remaining > 2) {
    fprintf(stderr, "Error: Too many positional arguments (%d extra)\n",
            remaining - 2);
    print_usage(argv[0]);
    return nullptr;
  }

  options.infile = argv[optind];
  options.outfile = argv[optind + 1];

  return &options;
}

// Parsed MP4 file info
struct Mp4Info {
  std::unique_ptr<AP4_File> file;
  AP4_Movie* movie;
  AP4_Track* video_track;
  AP4_UI32 video_duration_ms;
  AP4_UI32 video_timescale;
};

// Parse MP4 file and extract video track info
// Returns 0 on success, non-zero on error
int parse_mp4_file(const char* infile, int debug_level, Mp4Info& info) {
  AP4_ByteStream* input = nullptr;
  AP4_Result result = AP4_FileByteStream::Create(
      infile, AP4_FileByteStream::STREAM_MODE_READ, input);
  if (AP4_FAILED(result)) {
    fprintf(stderr, "Error: cannot open input file: %s\n", infile);
    return 1;
  }

  info.file = std::make_unique<AP4_File>(*input, true);
  input->Release();

  info.movie = info.file->GetMovie();
  if (info.movie == nullptr) {
    fprintf(stderr, "Error: no movie found in file\n");
    return 1;
  }

  // Find the video track
  info.video_track = nullptr;
  AP4_List<AP4_Track>& tracks = info.movie->GetTracks();
  for (AP4_List<AP4_Track>::Item* item = tracks.FirstItem(); item != nullptr;
       item = item->GetNext()) {
    AP4_Track* track = item->GetData();
    if (track->GetType() == AP4_Track::TYPE_VIDEO) {
      info.video_track = track;
      break;
    }
  }

  if (info.video_track == nullptr) {
    fprintf(stderr, "Error: no video track found\n");
    return 1;
  }

  info.video_duration_ms = info.video_track->GetDurationMs();
  info.video_timescale = info.video_track->GetMediaTimeScale();

  if (debug_level > 0) {
    fprintf(stderr, "Video track ID: %d\n", info.video_track->GetId());
    fprintf(stderr, "Video duration: %u ms\n", info.video_duration_ms);
    fprintf(stderr, "Video timescale: %u\n", info.video_timescale);
    fprintf(stderr, "Video sample count: %u\n",
            info.video_track->GetSampleCount());
  }

  return 0;
}

// Step 3: Find the frame at a given time
// Returns 0 on success, non-zero on error
// On success, frame_num contains the 0-based frame number
int find_frame_at_time(AP4_Track* video_track, AP4_UI32 target_time_ms,
                       int debug_level, AP4_Ordinal& frame_num) {
  AP4_Result result =
      video_track->GetSampleIndexForTimeStampMs(target_time_ms, frame_num);
  if (AP4_FAILED(result)) {
    fprintf(stderr, "Error: could not find frame at time %u ms\n",
            target_time_ms);
    return 1;
  }

  if (debug_level > 0) {
    fprintf(stderr, "Frame at target time: %u\n", frame_num);
  }

  return 0;
}

// Trimming Algorithm
// 1. Parse file, get video track duration
// 2. Calculate target_time = duration - msec
// 3. Find sample index at target_time
// 4. Find previous sync sample (keyframe)
// 5. Cut video from that sync sample
// 6. Cut audio at same timestamp
// 7. Rewrite moov box with updated sample tables
int mp4seek(const char* infile, const char* outfile, int debug_level,
            float sseof) {
  // 1. Parse file, get video track duration
  Mp4Info info;
  int result = parse_mp4_file(infile, debug_level, info);
  if (result != 0) {
    return result;
  }

  // 2. Calculate target_time = duration - msec
  AP4_UI32 sseof_ms = static_cast<AP4_UI32>(sseof * 1000.0f);
  if (sseof_ms > info.video_duration_ms) {
    fprintf(stderr, "Error: sseof (%u ms) exceeds video duration (%u ms)\n",
            sseof_ms, info.video_duration_ms);
    return 1;
  }
  AP4_UI32 target_time_ms = info.video_duration_ms - sseof_ms;

  if (debug_level > 0) {
    fprintf(stderr, "Target time: %u ms (duration %u ms - sseof %u ms)\n",
            target_time_ms, info.video_duration_ms, sseof_ms);
  }

  // 3. Find frame at target_time
  AP4_Ordinal frame_num = 0;
  result = find_frame_at_time(info.video_track, target_time_ms, debug_level,
                              frame_num);
  if (result != 0) {
    return result;
  }

  // 4. Find previous sync sample (keyframe)
  // 5. Cut video from that sync sample
  // 6. Cut audio at same timestamp
  // 7. Rewrite moov box with updated sample tables

  return 0;
}

int main(int argc, char* argv[]) {
  // Parse args
  ArgOptions* options = parse_args(argc, argv);
  if (options == nullptr) {
    return 1;
  }

  // Print args
  if (options->debug > 0) {
    fprintf(stderr, "Input file: %s\n", options->infile);
    fprintf(stderr, "Output file: %s\n", options->outfile);
    fprintf(stderr, "Debug level: %d\n", options->debug);
    fprintf(stderr, "Seek from EOF: %.3f seconds\n", options->sseof);
  }

  return mp4seek(options->infile, options->outfile, options->debug,
                 options->sseof);
}
