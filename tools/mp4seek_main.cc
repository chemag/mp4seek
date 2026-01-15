#include <getopt.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "include/mp4seek.h"

// Command line options
// TODO(chemag): add support for --end, --end_frame, --end_pts
struct ArgOptions {
  int debug;
  // TODO(chemag): add support for sexagesimal format for time
  float start;          // Start time in seconds (NaN if not set)
  int64_t start_frame;  // Start frame number (INT64_MIN if not set)
  int64_t start_pts;    // Start PTS value (INT64_MIN if not set)
  bool accurate_seek;   // If true, use EDTS to seek to exact position
  const char* infile;
  const char* outfile;
};

const ArgOptions DEFAULT_OPTIONS{
    .debug = 0,
    .start = NAN,
    .start_frame = INT64_MIN,
    .start_pts = INT64_MIN,
    .accurate_seek = false,
    .infile = nullptr,
    .outfile = nullptr,
};

void print_usage(const char* program_name) {
  fprintf(stderr, "%s version %s\n", PROJECT_NAME, PROJECT_VERSION);
  fprintf(stderr, "usage: %s [options] <infile> <outfile>\n", program_name);
  fprintf(stderr, "where options are:\n");
  fprintf(stderr, "\t-d, --debug:\tIncrease debug verbosity [%i]\n",
          DEFAULT_OPTIONS.debug);
  fprintf(stderr, "\t-q, --quiet:\tSet debug verbosity to -1\n");
  fprintf(stderr,
          "\t--start <float>:\tStart time in seconds (negative = from end)\n");
  fprintf(stderr,
          "\t--start_frame <int>:\tStart frame number (negative = from end)\n");
  fprintf(stderr,
          "\t--start_pts <int>:\tStart PTS in video timescale (negative = from "
          "end)\n");
  fprintf(stderr, "\t--accurate_seek:\tSeek to exact position using EDTS\n");
  fprintf(stderr,
          "\t--noaccurate_seek:\tSeek to nearest keyframe only [default]\n");
  fprintf(stderr, "\t-v, --version:\tShow version information\n");
  fprintf(stderr, "\t-h, --help:\tShow this help message\n");
}

// Long options with no equivalent short option
enum {
  QUIET_OPTION = CHAR_MAX + 1,
  START_OPTION,
  START_FRAME_OPTION,
  START_PTS_OPTION,
  ACCURATE_SEEK_OPTION,
  NOACCURATE_SEEK_OPTION,
  VERSION_OPTION,
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
      {"start", required_argument, nullptr, START_OPTION},
      {"start_frame", required_argument, nullptr, START_FRAME_OPTION},
      {"start_pts", required_argument, nullptr, START_PTS_OPTION},
      {"accurate_seek", no_argument, nullptr, ACCURATE_SEEK_OPTION},
      {"noaccurate_seek", no_argument, nullptr, NOACCURATE_SEEK_OPTION},
      {"version", no_argument, nullptr, VERSION_OPTION},
      {"help", no_argument, nullptr, HELP_OPTION},
      {nullptr, 0, nullptr, 0}};

  // Parse arguments
  while (true) {
    c = getopt_long(argc, argv, "dqvh", longopts, &optindex);
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

      case START_OPTION:
        options.start = strtof(optarg, &endptr);
        if (*endptr != '\0') {
          fprintf(stderr, "Error: Invalid float value for --start: %s\n",
                  optarg);
          print_usage(argv[0]);
          return nullptr;
        }
        break;

      case START_FRAME_OPTION:
        options.start_frame = strtoll(optarg, &endptr, 10);
        if (*endptr != '\0') {
          fprintf(stderr,
                  "Error: Invalid integer value for --start_frame: %s\n",
                  optarg);
          print_usage(argv[0]);
          return nullptr;
        }
        break;

      case START_PTS_OPTION:
        options.start_pts = strtoll(optarg, &endptr, 10);
        if (*endptr != '\0') {
          fprintf(stderr, "Error: Invalid integer value for --start_pts: %s\n",
                  optarg);
          print_usage(argv[0]);
          return nullptr;
        }
        break;

      case ACCURATE_SEEK_OPTION:
        options.accurate_seek = true;
        break;

      case NOACCURATE_SEEK_OPTION:
        options.accurate_seek = false;
        break;

      case VERSION_OPTION:
      case 'v':
        fprintf(stderr, "%s version %s\n", PROJECT_NAME, PROJECT_VERSION);
        exit(0);

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
    if (!std::isnan(options->start)) {
      fprintf(stderr, "Start: %.3f seconds\n", options->start);
    }
    if (options->start_frame != INT64_MIN) {
      fprintf(stderr, "Start frame: %lld\n", (long long)options->start_frame);
    }
    if (options->start_pts != INT64_MIN) {
      fprintf(stderr, "Start PTS: %lld\n", (long long)options->start_pts);
    }
    fprintf(stderr, "Accurate seek: %s\n",
            options->accurate_seek ? "true" : "false");
  }

  return mp4seek(options->infile, options->outfile, options->debug,
                 options->start, options->start_frame, options->start_pts,
                 options->accurate_seek);
}
