#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Forward declaration
int mp4seek(const std::string& infile, const std::string& outfile,
            int debug_level, float sseof);

void print_usage(const char* program_name) {
  std::cerr << "Usage: " << program_name << " [options] <infile> <outfile>\n"
            << "\n"
            << "Options:\n"
            << "  -q, --quiet       Quiet mode (debug_level = -1)\n"
            << "  -d, --debug       Increase debug level (can be repeated)\n"
            << "  --sseof <float>   Seek position in seconds from end of file\n"
            << "  -h, --help        Show this help message\n";
}

int main(int argc, char* argv[]) {
  int debug_level = 0;
  float sseof = 0.0f;
  std::string infile;
  std::string outfile;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
      debug_level = -1;
    } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
      ++debug_level;
    } else if (strcmp(argv[i], "--sseof") == 0) {
      if (i + 1 >= argc) {
        std::cerr << "Error: --sseof requires a float argument\n";
        return 1;
      }
      ++i;
      char* endptr;
      sseof = std::strtof(argv[i], &endptr);
      if (*endptr != '\0') {
        std::cerr << "Error: Invalid float value for --sseof: " << argv[i]
                  << "\n";
        return 1;
      }
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (argv[i][0] == '-') {
      std::cerr << "Error: Unknown option: " << argv[i] << "\n";
      print_usage(argv[0]);
      return 1;
    } else {
      // Positional arguments
      if (infile.empty()) {
        infile = argv[i];
      } else if (outfile.empty()) {
        outfile = argv[i];
      } else {
        std::cerr << "Error: Too many positional arguments\n";
        print_usage(argv[0]);
        return 1;
      }
    }
  }

  // Validate required arguments
  if (infile.empty() || outfile.empty()) {
    std::cerr << "Error: Both infile and outfile are required\n";
    print_usage(argv[0]);
    return 1;
  }

  if (debug_level > 0) {
    std::cerr << "Input file: " << infile << "\n";
    std::cerr << "Output file: " << outfile << "\n";
    std::cerr << "Debug level: " << debug_level << "\n";
    std::cerr << "Seek from EOF: " << sseof << " seconds\n";
  }

  return mp4seek(infile, outfile, debug_level, sseof);
}


// Trimming Algorithm
// 1. Parse file, get video track duration
// 2. Calculate target_time = duration - msec
// 3. Find sample index at target_time
// 4. Find previous sync sample (keyframe)
// 5. Cut video from that sync sample
// 6. Cut audio at same timestamp
// 7. Rewrite moov box with updated sample tables
int mp4seek(const std::string& infile, const std::string& outfile,
            int debug_level, float sseof) {
  // TODO: Implement MP4 seeking/trimming logic
  return 0;
}
