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
  AP4_Track* audio_track;
  AP4_UI32 video_duration_ms;
  AP4_UI32 video_timescale;
  AP4_UI32 audio_timescale;
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

  // Find the video and audio tracks
  info.video_track = nullptr;
  info.audio_track = nullptr;
  AP4_List<AP4_Track>& tracks = info.movie->GetTracks();
  for (AP4_List<AP4_Track>::Item* item = tracks.FirstItem(); item != nullptr;
       item = item->GetNext()) {
    AP4_Track* track = item->GetData();
    if (track->GetType() == AP4_Track::TYPE_VIDEO &&
        info.video_track == nullptr) {
      info.video_track = track;
    } else if (track->GetType() == AP4_Track::TYPE_AUDIO &&
               info.audio_track == nullptr) {
      info.audio_track = track;
    }
  }

  if (info.video_track == nullptr) {
    fprintf(stderr, "Error: no video track found\n");
    return 1;
  }

  info.video_duration_ms = info.video_track->GetDurationMs();
  info.video_timescale = info.video_track->GetMediaTimeScale();
  info.audio_timescale =
      info.audio_track ? info.audio_track->GetMediaTimeScale() : 0;

  if (debug_level > 0) {
    fprintf(stderr, "Video track ID: %d\n", info.video_track->GetId());
    fprintf(stderr, "Video duration: %u ms\n", info.video_duration_ms);
    fprintf(stderr, "Video timescale: %u\n", info.video_timescale);
    fprintf(stderr, "Video sample count: %u\n",
            info.video_track->GetSampleCount());
    if (info.audio_track) {
      fprintf(stderr, "Audio track ID: %d\n", info.audio_track->GetId());
      fprintf(stderr, "Audio timescale: %u\n", info.audio_timescale);
      fprintf(stderr, "Audio sample count: %u\n",
              info.audio_track->GetSampleCount());
    } else {
      fprintf(stderr, "No audio track found\n");
    }
  }

  return 0;
}

// Edit list info parsed from EDTS/ELST
struct EdtsInfo {
  bool has_edts;
  AP4_SI64 media_time;        // Media time offset (in media timescale)
  AP4_UI64 segment_duration;  // Segment duration (in movie timescale)
  bool has_empty_edit;        // True if there's an initial empty edit (delay)
  AP4_UI64 empty_duration;    // Duration of empty edit (in movie timescale)
};

// Parse EDTS from a track
// Returns EdtsInfo with parsed values
EdtsInfo parse_track_edts(AP4_Track* track, int debug_level) {
  EdtsInfo edts_info = {false, 0, 0, false, 0};

  const AP4_TrakAtom* trak = track->GetTrakAtom();
  if (trak == nullptr) {
    return edts_info;
  }

  AP4_ContainerAtom* edts =
      AP4_DYNAMIC_CAST(AP4_ContainerAtom, trak->GetChild(AP4_ATOM_TYPE_EDTS));
  if (edts == nullptr) {
    return edts_info;
  }

  AP4_ElstAtom* elst =
      AP4_DYNAMIC_CAST(AP4_ElstAtom, edts->GetChild(AP4_ATOM_TYPE_ELST));
  if (elst == nullptr) {
    return edts_info;
  }

  AP4_Array<AP4_ElstEntry>& entries = elst->GetEntries();
  if (entries.ItemCount() == 0) {
    return edts_info;
  }

  edts_info.has_edts = true;

  // Process edit list entries
  for (unsigned int i = 0; i < entries.ItemCount(); i++) {
    AP4_ElstEntry& entry = entries[i];

    if (entry.m_MediaTime == -1) {
      // Empty edit (initial delay)
      edts_info.has_empty_edit = true;
      edts_info.empty_duration = entry.m_SegmentDuration;
    } else {
      // Normal edit - use the first non-empty edit
      edts_info.media_time = entry.m_MediaTime;
      edts_info.segment_duration = entry.m_SegmentDuration;
      break;
    }
  }

  if (debug_level > 1) {
    fprintf(stderr, "  EDTS: media_time=%lld, segment_duration=%llu",
            (long long)edts_info.media_time,
            (unsigned long long)edts_info.segment_duration);
    if (edts_info.has_empty_edit) {
      fprintf(stderr, ", empty_duration=%llu",
              (unsigned long long)edts_info.empty_duration);
    }
    fprintf(stderr, "\n");
  }

  return edts_info;
}

// Get the media time of a sample (in media timescale units)
AP4_UI64 get_sample_media_time(AP4_Track* track, AP4_Ordinal sample_index) {
  AP4_Sample sample;
  if (AP4_FAILED(track->GetSample(sample_index, sample))) {
    return 0;
  }
  return sample.GetDts();
}

// Step 6: Create a trimmed audio track with EDTS
// video_cut_media_time is the video keyframe's DTS in video timescale
// Returns 0 on success, non-zero on error
int audio_track_trim_sseof(AP4_Track* input_audio_track,
                           AP4_UI64 video_cut_media_time,
                           AP4_UI32 video_timescale, AP4_UI32 movie_timescale,
                           int debug_level, AP4_Track*& output_track,
                           AP4_UI64& audio_skip_samples) {
  // Parse input audio EDTS
  EdtsInfo audio_edts = parse_track_edts(input_audio_track, debug_level);

  AP4_UI32 audio_timescale = input_audio_track->GetMediaTimeScale();
  AP4_Cardinal total_samples = input_audio_track->GetSampleCount();

  // Convert video cut time to audio timescale
  AP4_UI64 audio_cut_time =
      AP4_ConvertTime(video_cut_media_time, video_timescale, audio_timescale);

  // Account for input audio EDTS offset
  if (audio_edts.has_edts && audio_edts.media_time > 0) {
    // The audio media_time tells us where audio playback starts
    // We need to adjust our cut point accordingly
    audio_cut_time += audio_edts.media_time;
  }

  if (debug_level > 1) {
    fprintf(stderr, "Audio cut time: %llu (audio timescale %u)\n",
            (unsigned long long)audio_cut_time, audio_timescale);
  }

  // Find the audio sample at or before the cut time
  AP4_Ordinal audio_start_sample = 0;
  AP4_Result result = input_audio_track->GetSampleIndexForTimeStampMs(
      static_cast<AP4_UI32>(
          AP4_ConvertTime(audio_cut_time, audio_timescale, 1000)),
      audio_start_sample);
  if (AP4_FAILED(result)) {
    // If we can't find the exact sample, start from the beginning
    audio_start_sample = 0;
  }

  // Get the media time where this audio sample starts
  AP4_UI64 audio_sample_start_time =
      get_sample_media_time(input_audio_track, audio_start_sample);

  // Calculate how many samples to skip at the beginning
  // (the difference between the audio frame start and the actual cut point)
  if (audio_cut_time > audio_sample_start_time) {
    audio_skip_samples = audio_cut_time - audio_sample_start_time;
  } else {
    audio_skip_samples = 0;
  }

  if (debug_level > 0) {
    fprintf(stderr, "Audio: starting at sample %u, skip %llu samples in EDTS\n",
            audio_start_sample, (unsigned long long)audio_skip_samples);
  }

  // Create a synthetic sample table
  AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

  // Copy sample descriptions
  for (unsigned int i = 0; i < input_audio_track->GetSampleDescriptionCount();
       i++) {
    AP4_SampleDescription* sample_desc =
        input_audio_track->GetSampleDescription(i);
    if (sample_desc == nullptr) {
      fprintf(stderr, "Error: invalid audio sample description\n");
      delete sample_table;
      return 1;
    }
    sample_table->AddSampleDescription(sample_desc, false);
  }

  // Add samples from audio_start_sample to end
  AP4_UI64 dts = 0;
  AP4_Cardinal output_sample_count = 0;
  for (AP4_Ordinal i = audio_start_sample; i < total_samples; i++) {
    AP4_Sample sample;
    result = input_audio_track->GetSample(i, sample);
    if (AP4_FAILED(result)) {
      fprintf(stderr, "Error: could not get audio sample %u\n", i);
      delete sample_table;
      return 1;
    }

    AP4_ByteStream* sample_stream = sample.GetDataStream();
    if (sample_stream == nullptr) {
      fprintf(stderr, "Error: could not get audio sample data stream %u\n", i);
      delete sample_table;
      return 1;
    }

    sample_table->AddSample(*sample_stream, sample.GetOffset(),
                            sample.GetSize(), sample.GetDuration(),
                            sample.GetDescriptionIndex(), dts,
                            sample.GetCtsDelta(),
                            true);  // audio samples are always sync
    sample_stream->Release();

    dts += sample.GetDuration();
    output_sample_count++;
  }

  AP4_UI32 output_duration_ms =
      static_cast<AP4_UI32>(AP4_ConvertTime(dts, audio_timescale, 1000));

  if (debug_level > 0) {
    fprintf(stderr, "Trimmed audio: samples %u-%u (%u samples), %u ms\n",
            audio_start_sample, total_samples - 1, output_sample_count,
            output_duration_ms);
  }

  // Create output track
  output_track =
      new AP4_Track(AP4_Track::TYPE_AUDIO, sample_table,
                    2,  // track id (video is 1)
                    movie_timescale,
                    AP4_ConvertTime(output_duration_ms, 1000, movie_timescale),
                    audio_timescale, dts,  // media duration
                    input_audio_track->GetTrackLanguage(), 0,
                    0);  // width, height (not applicable for audio)

  // Add EDTS to skip the extra audio at the beginning
  if (audio_skip_samples > 0) {
    AP4_ContainerAtom* new_edts = new AP4_ContainerAtom(AP4_ATOM_TYPE_EDTS);
    AP4_ElstAtom* new_elst = new AP4_ElstAtom();

    // Calculate segment duration in movie timescale
    // This is the duration of audio we want to play (total - skipped)
    AP4_UI64 playable_duration = dts - audio_skip_samples;
    AP4_UI64 segment_duration =
        AP4_ConvertTime(playable_duration, audio_timescale, movie_timescale);

    // media_time is where to start playback (skip the extra samples)
    AP4_ElstEntry entry(segment_duration, audio_skip_samples, 1);
    new_elst->AddEntry(entry);
    new_edts->AddChild(new_elst);
    output_track->UseTrakAtom()->AddChild(new_edts, 1);  // add after tkhd

    if (debug_level > 0) {
      fprintf(stderr, "Audio EDTS: media_time=%llu, segment_duration=%llu\n",
              (unsigned long long)audio_skip_samples,
              (unsigned long long)segment_duration);
    }
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

// Step 4: Find the keyframe (sync sample) at or before a given frame
// Returns the 0-based frame number of the keyframe
AP4_Ordinal find_keyframe_before_frame(AP4_Track* video_track,
                                       AP4_Ordinal frame_num, int debug_level) {
  AP4_Ordinal keyframe_frame_num =
      video_track->GetNearestSyncSampleIndex(frame_num, true);

  if (debug_level > 0) {
    fprintf(stderr, "Keyframe before target: frame %u\n", keyframe_frame_num);
  }

  return keyframe_frame_num;
}

// Step 5: Create a trimmed video track containing frames from start_frame_num
// to end Returns 0 on success, non-zero on error On success, output_track,
// output_duration_ms, and output_frame_count are set
int video_track_trim_sseof(AP4_Track* input_track, AP4_Ordinal start_frame_num,
                           AP4_UI32 movie_timescale, int debug_level,
                           AP4_Track*& output_track,
                           AP4_UI32& output_duration_ms,
                           AP4_Cardinal& output_frame_count) {
  AP4_Cardinal total_samples = input_track->GetSampleCount();

  // Create a synthetic sample table to hold the output samples
  AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

  // Copy sample descriptions from input track (usually just one)
  for (unsigned int i = 0; i < input_track->GetSampleDescriptionCount(); i++) {
    AP4_SampleDescription* sample_desc = input_track->GetSampleDescription(i);
    if (sample_desc == nullptr) {
      fprintf(stderr, "Error: invalid sample description\n");
      delete sample_table;
      return 1;
    }
    sample_table->AddSampleDescription(sample_desc, false);
  }

  // Add samples from start_frame_num to end
  AP4_UI64 dts = 0;
  output_frame_count = 0;
  for (AP4_Ordinal i = start_frame_num; i < total_samples; i++) {
    AP4_Sample sample;
    AP4_Result result = input_track->GetSample(i, sample);
    if (AP4_FAILED(result)) {
      fprintf(stderr, "Error: could not get sample %u\n", i);
      delete sample_table;
      return 1;
    }

    // Get sample data stream
    AP4_ByteStream* sample_stream = sample.GetDataStream();
    if (sample_stream == nullptr) {
      fprintf(stderr, "Error: could not get sample data stream for sample %u\n",
              i);
      delete sample_table;
      return 1;
    }

    // Add sample to the table
    sample_table->AddSample(*sample_stream, sample.GetOffset(),
                            sample.GetSize(), sample.GetDuration(),
                            sample.GetDescriptionIndex(), dts,
                            sample.GetCtsDelta(), sample.IsSync());
    sample_stream->Release();

    dts += sample.GetDuration();
    output_frame_count++;
  }

  // Compute output duration in milliseconds
  output_duration_ms = static_cast<AP4_UI32>(
      AP4_ConvertTime(dts, input_track->GetMediaTimeScale(), 1000));

  if (debug_level > 0) {
    fprintf(stderr, "Trimmed video: frames %u-%u (%u frames), %u ms\n",
            start_frame_num, total_samples - 1, output_frame_count,
            output_duration_ms);
  }

  // Create output track
  output_track =
      new AP4_Track(AP4_Track::TYPE_VIDEO, sample_table,
                    1,  // track id
                    movie_timescale,
                    AP4_ConvertTime(output_duration_ms, 1000,
                                    movie_timescale),  // track duration
                    input_track->GetMediaTimeScale(),
                    dts,  // media duration
                    input_track->GetTrackLanguage(), input_track->GetWidth(),
                    input_track->GetHeight());

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
  AP4_Ordinal keyframe_frame_num =
      find_keyframe_before_frame(info.video_track, frame_num, debug_level);

  // 5. Cut video from that sync sample
  AP4_Track* output_video_track = nullptr;
  AP4_UI32 output_duration_ms = 0;
  AP4_Cardinal output_frame_count = 0;
  result = video_track_trim_sseof(
      info.video_track, keyframe_frame_num, info.movie->GetTimeScale(),
      debug_level, output_video_track, output_duration_ms, output_frame_count);
  if (result != 0) {
    return result;
  }

  // 6. Cut audio at same timestamp
  AP4_Track* output_audio_track = nullptr;
  if (info.audio_track != nullptr) {
    // Get the video keyframe's media time (DTS) to sync audio
    AP4_UI64 video_cut_media_time =
        get_sample_media_time(info.video_track, keyframe_frame_num);

    if (debug_level > 0) {
      fprintf(stderr, "Video cut media time: %llu (timescale %u)\n",
              (unsigned long long)video_cut_media_time, info.video_timescale);
    }

    AP4_UI64 audio_skip_samples = 0;
    result = audio_track_trim_sseof(info.audio_track, video_cut_media_time,
                                    info.video_timescale,
                                    info.movie->GetTimeScale(), debug_level,
                                    output_audio_track, audio_skip_samples);
    if (result != 0) {
      delete output_video_track;
      return result;
    }
  }

  // 7. Write output file
  // Create output movie
  AP4_Movie* output_movie = new AP4_Movie(info.movie->GetTimeScale());

  // Add video track to movie (movie takes ownership)
  output_movie->AddTrack(output_video_track);

  // Add audio track to movie if present
  if (output_audio_track != nullptr) {
    output_movie->AddTrack(output_audio_track);
  }

  // Create output file
  AP4_File output_file(output_movie);

  // Set file type (MP4 video)
  AP4_UI32 compatible_brands[2] = {AP4_FILE_BRAND_ISOM, AP4_FILE_BRAND_MP42};
  output_file.SetFileType(AP4_FILE_BRAND_MP42, 0, compatible_brands, 2);

  // Create output stream
  AP4_ByteStream* output_stream = nullptr;
  AP4_Result ap4_result = AP4_FileByteStream::Create(
      outfile, AP4_FileByteStream::STREAM_MODE_WRITE, output_stream);
  if (AP4_FAILED(ap4_result)) {
    fprintf(stderr, "Error: cannot open output file: %s\n", outfile);
    return 1;
  }

  // Write output file
  AP4_FileWriter::Write(output_file, *output_stream);
  output_stream->Release();

  if (debug_level >= 0) {
    fprintf(stderr, "Wrote %s (%u ms, %u frames)\n", outfile,
            output_duration_ms, output_frame_count);
  }

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
