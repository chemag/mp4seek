#include <mp4seek.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "Ap4.h"
#include <mp4seek_log.h>

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
static int parse_mp4_file(const char* infile, int debug_level, Mp4Info& info) {
  AP4_ByteStream* input = nullptr;
  AP4_Result result = AP4_FileByteStream::Create(
      infile, AP4_FileByteStream::STREAM_MODE_READ, input);
  if (AP4_FAILED(result)) {
    MP4SEEK_LOGE("Error: cannot open input file: %s", infile);
    return 1;
  }

  info.file = std::make_unique<AP4_File>(*input, true);
  input->Release();

  info.movie = info.file->GetMovie();
  if (info.movie == nullptr) {
    MP4SEEK_LOGE("Error: no movie found in file");
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
    MP4SEEK_LOGE("Error: no video track found");
    return 1;
  }

  info.video_duration_ms = info.video_track->GetDurationMs();
  info.video_timescale = info.video_track->GetMediaTimeScale();
  info.audio_timescale =
      info.audio_track ? info.audio_track->GetMediaTimeScale() : 0;

  if (debug_level > 0) {
    MP4SEEK_LOGD("Video track ID: %d", info.video_track->GetId());
    MP4SEEK_LOGD("Video duration: %u ms", info.video_duration_ms);
    MP4SEEK_LOGD("Video timescale: %u", info.video_timescale);
    MP4SEEK_LOGD("Video sample count: %u",
            info.video_track->GetSampleCount());
    if (info.audio_track) {
      MP4SEEK_LOGD("Audio track ID: %d", info.audio_track->GetId());
      MP4SEEK_LOGD("Audio timescale: %u", info.audio_timescale);
      MP4SEEK_LOGD("Audio sample count: %u",
              info.audio_track->GetSampleCount());
    } else {
      MP4SEEK_LOGD("No audio track found");
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
static EdtsInfo parse_track_edts(AP4_Track* track, int debug_level) {
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
    if (edts_info.has_empty_edit) {
      MP4SEEK_LOGD("  EDTS: media_time=%lld, segment_duration=%llu, empty_duration=%llu",
              (long long)edts_info.media_time,
              (unsigned long long)edts_info.segment_duration,
              (unsigned long long)edts_info.empty_duration);
    } else {
      MP4SEEK_LOGD("  EDTS: media_time=%lld, segment_duration=%llu",
              (long long)edts_info.media_time,
              (unsigned long long)edts_info.segment_duration);
    }
  }

  return edts_info;
}

// Get the media time of a sample (in media timescale units)
static AP4_UI64 get_sample_media_time(AP4_Track* track,
                                      AP4_Ordinal sample_index) {
  AP4_Sample sample;
  if (AP4_FAILED(track->GetSample(sample_index, sample))) {
    return 0;
  }
  return sample.GetDts();
}

// Step 6: Create a trimmed audio track with EDTS
// video_cut_media_time is the video keyframe's DTS in video timescale
// Returns 0 on success, non-zero on error
static int audio_track_trim_sseof(AP4_Track* input_audio_track,
                                  AP4_UI64 video_cut_media_time,
                                  AP4_UI32 video_timescale,
                                  AP4_UI32 movie_timescale, int debug_level,
                                  AP4_Track*& output_track,
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
    MP4SEEK_LOGD("Audio cut time: %llu (audio timescale %u)",
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
    MP4SEEK_LOGD("Audio: starting at sample %u, skip %llu samples in EDTS",
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
      MP4SEEK_LOGE("Error: invalid audio sample description");
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
      MP4SEEK_LOGE("Error: could not get audio sample %u", i);
      delete sample_table;
      return 1;
    }

    AP4_ByteStream* sample_stream = sample.GetDataStream();
    if (sample_stream == nullptr) {
      MP4SEEK_LOGE("Error: could not get audio sample data stream %u", i);
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
    MP4SEEK_LOGD("Trimmed audio: samples %u-%u (%u samples), %u ms",
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
      MP4SEEK_LOGD("Audio EDTS: media_time=%llu, segment_duration=%llu",
              (unsigned long long)audio_skip_samples,
              (unsigned long long)segment_duration);
    }
  }

  // Copy sample grouping atoms (sgpd/sbgp) from input to output
  // These contain audio pre-roll information
  AP4_Atom* input_stbl =
      input_audio_track->UseTrakAtom()->FindChild("mdia/minf/stbl");
  AP4_Atom* output_stbl =
      output_track->UseTrakAtom()->FindChild("mdia/minf/stbl");
  if (input_stbl != nullptr && output_stbl != nullptr) {
    AP4_ContainerAtom* input_stbl_container =
        AP4_DYNAMIC_CAST(AP4_ContainerAtom, input_stbl);
    AP4_ContainerAtom* output_stbl_container =
        AP4_DYNAMIC_CAST(AP4_ContainerAtom, output_stbl);
    if (input_stbl_container != nullptr && output_stbl_container != nullptr) {
      // Copy sgpd (Sample Group Description) - no changes needed
      AP4_Atom* sgpd = input_stbl_container->GetChild(AP4_ATOM_TYPE_SGPD);
      if (sgpd != nullptr) {
        AP4_Atom* sgpd_clone = sgpd->Clone();
        if (sgpd_clone != nullptr) {
          output_stbl_container->AddChild(sgpd_clone);
        }
      }
      // Copy sbgp (Sample to Group) with recalculated entries
      AP4_Atom* sbgp = input_stbl_container->GetChild(AP4_ATOM_TYPE_SBGP);
      if (sbgp != nullptr) {
        AP4_SbgpAtom* input_sbgp = AP4_DYNAMIC_CAST(AP4_SbgpAtom, sbgp);
        if (input_sbgp != nullptr) {
          // Clone and modify entries
          AP4_Atom* sbgp_clone = sbgp->Clone();
          AP4_SbgpAtom* output_sbgp =
              AP4_DYNAMIC_CAST(AP4_SbgpAtom, sbgp_clone);
          if (output_sbgp != nullptr) {
            AP4_Array<AP4_SbgpAtom::Entry>& input_entries =
                input_sbgp->GetEntries();
            AP4_Array<AP4_SbgpAtom::Entry>& output_entries =
                output_sbgp->GetEntries();

            // Clear output entries and rebuild for trimmed range
            output_entries.Clear();

            // Track cumulative sample index in input
            AP4_UI32 cumulative_sample = 0;
            for (AP4_Cardinal i = 0; i < input_entries.ItemCount(); i++) {
              AP4_UI32 entry_start = cumulative_sample;
              AP4_UI32 entry_end =
                  cumulative_sample + input_entries[i].sample_count;

              // Check if this entry overlaps with kept range
              // Kept range: [audio_start_sample, total_samples)
              if (entry_end > audio_start_sample &&
                  entry_start < total_samples) {
                // Calculate overlap
                AP4_UI32 overlap_start = (entry_start > audio_start_sample)
                                             ? entry_start
                                             : audio_start_sample;
                AP4_UI32 overlap_end =
                    (entry_end < total_samples) ? entry_end : total_samples;
                AP4_UI32 new_sample_count = overlap_end - overlap_start;

                if (new_sample_count > 0) {
                  AP4_SbgpAtom::Entry new_entry;
                  new_entry.sample_count = new_sample_count;
                  new_entry.group_description_index =
                      input_entries[i].group_description_index;
                  output_entries.Append(new_entry);
                }
              }

              cumulative_sample = entry_end;
            }

            output_stbl_container->AddChild(output_sbgp);
          }
        }
      }
    }
  }

  return 0;
}

// Step 3: Find the frame at a given time
// Returns 0 on success, non-zero on error
// On success, frame_num contains the 0-based frame number
static int find_frame_at_time(AP4_Track* video_track, AP4_UI32 target_time_ms,
                              int debug_level, AP4_Ordinal& frame_num) {
  AP4_Result result =
      video_track->GetSampleIndexForTimeStampMs(target_time_ms, frame_num);
  if (AP4_FAILED(result)) {
    MP4SEEK_LOGE("Error: could not find frame at time %u ms",
            target_time_ms);
    return 1;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD("Frame at target time: %u", frame_num);
  }

  return 0;
}

// Step 4: Find the keyframe (sync sample) at or before a given frame
// Returns the 0-based frame number of the keyframe
static AP4_Ordinal find_keyframe_before_frame(AP4_Track* video_track,
                                              AP4_Ordinal frame_num,
                                              int debug_level) {
  AP4_Ordinal keyframe_frame_num =
      video_track->GetNearestSyncSampleIndex(frame_num, true);

  if (debug_level > 0) {
    MP4SEEK_LOGD("Keyframe before target: frame %u", keyframe_frame_num);
  }

  return keyframe_frame_num;
}

// Step 5: Create a trimmed video track containing frames from start_frame_num
// to end. If accurate_seek is true and target_frame_num > start_frame_num,
// adds EDTS to skip to the target frame.
// Returns 0 on success, non-zero on error. On success, output_track,
// output_duration_ms, and output_frame_count are set.
static int video_track_trim_sseof(AP4_Track* input_track,
                                  AP4_Ordinal start_frame_num,
                                  AP4_Ordinal target_frame_num,
                                  bool accurate_seek, AP4_UI32 movie_timescale,
                                  int debug_level, AP4_Track*& output_track,
                                  AP4_UI32& output_duration_ms,
                                  AP4_Cardinal& output_frame_count) {
  AP4_Cardinal total_samples = input_track->GetSampleCount();

  // Create a synthetic sample table to hold the output samples
  AP4_SyntheticSampleTable* sample_table = new AP4_SyntheticSampleTable();

  // Copy sample descriptions from input track (usually just one)
  for (unsigned int i = 0; i < input_track->GetSampleDescriptionCount(); i++) {
    AP4_SampleDescription* sample_desc = input_track->GetSampleDescription(i);
    if (sample_desc == nullptr) {
      MP4SEEK_LOGE("Error: invalid sample description");
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
      MP4SEEK_LOGE("Error: could not get sample %u", i);
      delete sample_table;
      return 1;
    }

    // Get sample data stream
    AP4_ByteStream* sample_stream = sample.GetDataStream();
    if (sample_stream == nullptr) {
      MP4SEEK_LOGE("Error: could not get sample data stream for sample %u",
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
    MP4SEEK_LOGD("Trimmed video: frames %u-%u (%u frames), %u ms",
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

  // Add EDTS for accurate seek if needed
  if (accurate_seek && target_frame_num > start_frame_num) {
    // Calculate the media time to skip (from keyframe to target frame)
    AP4_UI64 skip_media_time = 0;
    for (AP4_Ordinal i = start_frame_num; i < target_frame_num; i++) {
      AP4_Sample sample;
      if (AP4_SUCCEEDED(input_track->GetSample(i, sample))) {
        skip_media_time += sample.GetDuration();
      }
    }

    if (skip_media_time > 0) {
      AP4_ContainerAtom* new_edts = new AP4_ContainerAtom(AP4_ATOM_TYPE_EDTS);
      AP4_ElstAtom* new_elst = new AP4_ElstAtom();

      // Calculate segment duration (total - skipped) in movie timescale
      AP4_UI64 playable_duration = dts - skip_media_time;
      AP4_UI64 segment_duration = AP4_ConvertTime(
          playable_duration, input_track->GetMediaTimeScale(), movie_timescale);

      // media_time is where to start playback (skip the pre-roll frames)
      AP4_ElstEntry entry(segment_duration, skip_media_time, 1);
      new_elst->AddEntry(entry);
      new_edts->AddChild(new_elst);
      output_track->UseTrakAtom()->AddChild(new_edts, 1);  // add after tkhd

      if (debug_level > 0) {
        MP4SEEK_LOGD(
                "Video EDTS: media_time=%llu, segment_duration=%llu "
                "(skipping %u frames)",
                (unsigned long long)skip_media_time,
                (unsigned long long)segment_duration,
                target_frame_num - start_frame_num);
      }
    }
  }

  return 0;
}

// Calculate start frame from options (--start, --start_frame, or --start_pts)
// Returns 0 on success, non-zero on error
// On success, frame_num contains the 0-based frame number
static int calculate_start_frame(AP4_Track* video_track,
                                 AP4_UI32 video_duration_ms,
                                 AP4_UI32 video_timescale, float start,
                                 int64_t start_frame, int64_t start_pts,
                                 int debug_level, AP4_Ordinal& frame_num) {
  AP4_Cardinal total_frames = video_track->GetSampleCount();
  int result = 0;

  if (!std::isnan(start)) {
    // --start: time in seconds
    int64_t start_ms;
    if (start < 0) {
      // Negative: from end
      start_ms = static_cast<int64_t>(video_duration_ms) +
                 static_cast<int64_t>(start * 1000.0f);
    } else {
      // Positive: from beginning
      start_ms = static_cast<int64_t>(start * 1000.0f);
    }

    if (start_ms < 0 || start_ms > static_cast<int64_t>(video_duration_ms)) {
      MP4SEEK_LOGE("Error: start time %lld ms is out of range (0-%u ms)",
              (long long)start_ms, video_duration_ms);
      return 1;
    }

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start time: %lld ms (from --start %.3f)",
              (long long)start_ms, start);
    }

    result = find_frame_at_time(video_track, static_cast<AP4_UI32>(start_ms),
                                debug_level, frame_num);
    if (result != 0) {
      return result;
    }

  } else if (start_frame != INT64_MIN) {
    // --start_frame: frame number
    int64_t frame;
    if (start_frame < 0) {
      // Negative: from end
      frame = static_cast<int64_t>(total_frames) + start_frame;
    } else {
      // Positive: from beginning
      frame = start_frame;
    }

    if (frame < 0 || frame >= static_cast<int64_t>(total_frames)) {
      MP4SEEK_LOGE("Error: start frame %lld is out of range (0-%u)",
              (long long)frame, total_frames - 1);
      return 1;
    }

    frame_num = static_cast<AP4_Ordinal>(frame);

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start frame: %u (from --start_frame %lld)", frame_num,
              (long long)start_frame);
    }

  } else if (start_pts != INT64_MIN) {
    // --start_pts: PTS value in video timescale
    int64_t pts;
    AP4_UI64 total_duration_pts =
        AP4_ConvertTime(video_duration_ms, 1000, video_timescale);

    if (start_pts < 0) {
      // Negative: from end
      pts = static_cast<int64_t>(total_duration_pts) + start_pts;
    } else {
      // Positive: from beginning
      pts = start_pts;
    }

    if (pts < 0 || pts > static_cast<int64_t>(total_duration_pts)) {
      MP4SEEK_LOGE("Error: start PTS %lld is out of range (0-%llu)",
              (long long)pts, (unsigned long long)total_duration_pts);
      return 1;
    }

    // Convert PTS to milliseconds and find frame
    AP4_UI32 target_ms =
        static_cast<AP4_UI32>(AP4_ConvertTime(pts, video_timescale, 1000));

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start PTS: %lld (from --start_pts %lld) = %u ms",
              (long long)pts, (long long)start_pts, target_ms);
    }

    result = find_frame_at_time(video_track, target_ms, debug_level, frame_num);
    if (result != 0) {
      return result;
    }

  } else {
    MP4SEEK_LOGE(
        "Error: one of --start, --start_frame, or --start_pts is required");
    return 1;
  }

  return 0;
}

// Trimming Algorithm
// 1. Parse file, get video track duration
// 2. Calculate start frame from options (--start, --start_frame, or
// --start_pts)
// 3. Find previous sync sample (keyframe) at or before start frame
// 4. Cut video from that sync sample
// 5. Cut audio at same timestamp
// 6. Rewrite moov box with updated sample tables
int mp4seek(const char* infile, const char* outfile, int debug_level,
            float start, int64_t start_frame, int64_t start_pts,
            bool accurate_seek) {
  // 1. Parse file, get video track duration
  Mp4Info info;
  int result = parse_mp4_file(infile, debug_level, info);
  if (result != 0) {
    return result;
  }

  // 2. Calculate start frame from the provided option
  AP4_Ordinal frame_num = 0;
  result = calculate_start_frame(info.video_track, info.video_duration_ms,
                                 info.video_timescale, start, start_frame,
                                 start_pts, debug_level, frame_num);
  if (result != 0) {
    return result;
  }

  // 3. Find previous sync sample (keyframe)
  AP4_Ordinal keyframe_frame_num =
      find_keyframe_before_frame(info.video_track, frame_num, debug_level);

  // 4. Cut video from that sync sample
  AP4_Track* output_video_track = nullptr;
  AP4_UI32 output_duration_ms = 0;
  AP4_Cardinal output_frame_count = 0;
  result = video_track_trim_sseof(
      info.video_track, keyframe_frame_num, frame_num, accurate_seek,
      info.movie->GetTimeScale(), debug_level, output_video_track,
      output_duration_ms, output_frame_count);
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
      MP4SEEK_LOGD("Video cut media time: %llu (timescale %u)",
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

  // Copy udta (user data/metadata) from input moov if present
  AP4_MoovAtom* input_moov = info.movie->GetMoovAtom();
  if (input_moov != nullptr) {
    AP4_Atom* input_udta = input_moov->GetChild(AP4_ATOM_TYPE_UDTA);
    if (input_udta != nullptr) {
      AP4_Atom* output_udta = input_udta->Clone();
      if (output_udta != nullptr) {
        output_movie->GetMoovAtom()->AddChild(output_udta);
      }
    }
  }

  // Create output file
  AP4_File output_file(output_movie);

  // Copy file type from input file
  AP4_FtypAtom* input_ftyp = info.file->GetFileType();
  if (input_ftyp != nullptr) {
    AP4_Array<AP4_UI32>& brands = input_ftyp->GetCompatibleBrands();
    output_file.SetFileType(
        input_ftyp->GetMajorBrand(), input_ftyp->GetMinorVersion(),
        brands.ItemCount() > 0 ? &brands[0] : nullptr, brands.ItemCount());
  } else {
    // Fallback if no ftyp in input
    AP4_UI32 compatible_brands[2] = {AP4_FILE_BRAND_ISOM, AP4_FILE_BRAND_MP42};
    output_file.SetFileType(AP4_FILE_BRAND_MP42, 0, compatible_brands, 2);
  }

  // Create output stream
  AP4_ByteStream* output_stream = nullptr;
  AP4_Result ap4_result = AP4_FileByteStream::Create(
      outfile, AP4_FileByteStream::STREAM_MODE_WRITE, output_stream);
  if (AP4_FAILED(ap4_result)) {
    MP4SEEK_LOGE("Error: cannot open output file: %s", outfile);
    return 1;
  }

  // Write output file
  AP4_FileWriter::Write(output_file, *output_stream);
  output_stream->Release();

  if (debug_level >= 0) {
    MP4SEEK_LOGI("Wrote %s (%u ms, %u frames)", outfile,
            output_duration_ms, output_frame_count);
  }

  return 0;
}
