#include <mp4seek.h>

#include <mp4v2/mp4v2.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include <mp4seek_log.h>

// Parsed MP4 file info
struct Mp4Info {
  MP4FileHandle hFile;
  MP4TrackId videoTrackId;
  MP4TrackId audioTrackId;
  MP4Duration videoDuration;  // in video timescale
  uint32_t videoTimescale;
  uint32_t audioTimescale;
  uint32_t movieTimescale;
  MP4SampleId videoSampleCount;
  MP4SampleId audioSampleCount;
};

// Parse MP4 file and extract track info
static int parse_mp4_file(const char* infile, int debug_level, Mp4Info& info) {
  info.hFile = MP4Read(infile);
  if (info.hFile == MP4_INVALID_FILE_HANDLE) {
    MP4SEEK_LOGE("Error: cannot open input file: %s", infile);
    return 1;
  }

  // Find video track
  info.videoTrackId = MP4_INVALID_TRACK_ID;
  info.audioTrackId = MP4_INVALID_TRACK_ID;

  uint32_t numTracks = MP4GetNumberOfTracks(info.hFile);
  for (uint32_t i = 0; i < numTracks; i++) {
    MP4TrackId trackId = MP4FindTrackId(info.hFile, i);
    const char* trackType = MP4GetTrackType(info.hFile, trackId);

    if (trackType != nullptr) {
      if (strcmp(trackType, MP4_VIDEO_TRACK_TYPE) == 0 &&
          info.videoTrackId == MP4_INVALID_TRACK_ID) {
        info.videoTrackId = trackId;
      } else if (strcmp(trackType, MP4_AUDIO_TRACK_TYPE) == 0 &&
                 info.audioTrackId == MP4_INVALID_TRACK_ID) {
        info.audioTrackId = trackId;
      }
    }
  }

  if (info.videoTrackId == MP4_INVALID_TRACK_ID) {
    MP4SEEK_LOGE("Error: no video track found");
    MP4Close(info.hFile);
    return 1;
  }

  info.videoTimescale = MP4GetTrackTimeScale(info.hFile, info.videoTrackId);
  info.videoDuration = MP4GetTrackDuration(info.hFile, info.videoTrackId);
  info.videoSampleCount =
      MP4GetTrackNumberOfSamples(info.hFile, info.videoTrackId);
  info.movieTimescale = MP4GetTimeScale(info.hFile);

  if (info.audioTrackId != MP4_INVALID_TRACK_ID) {
    info.audioTimescale = MP4GetTrackTimeScale(info.hFile, info.audioTrackId);
    info.audioSampleCount =
        MP4GetTrackNumberOfSamples(info.hFile, info.audioTrackId);
  } else {
    info.audioTimescale = 0;
    info.audioSampleCount = 0;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD("Video track ID: %d", info.videoTrackId);
    MP4SEEK_LOGD("Video duration: %llu (timescale %u)",
            (unsigned long long)info.videoDuration, info.videoTimescale);
    MP4SEEK_LOGD("Video sample count: %u", info.videoSampleCount);
    if (info.audioTrackId != MP4_INVALID_TRACK_ID) {
      MP4SEEK_LOGD("Audio track ID: %d", info.audioTrackId);
      MP4SEEK_LOGD("Audio timescale: %u", info.audioTimescale);
      MP4SEEK_LOGD("Audio sample count: %u", info.audioSampleCount);
    } else {
      MP4SEEK_LOGD("No audio track found");
    }
  }

  return 0;
}

// Get sample time (DTS) in track timescale
static MP4Timestamp get_sample_time(MP4FileHandle hFile, MP4TrackId trackId,
                                    MP4SampleId sampleId) {
  return MP4GetSampleTime(hFile, trackId, sampleId);
}

// Find sample at or before a given time
// Returns 1-based sample ID
static MP4SampleId find_sample_at_time(MP4FileHandle hFile, MP4TrackId trackId,
                                       MP4Timestamp time, int debug_level) {
  // MP4GetSampleIdFromTime returns 1-based sample ID
  MP4SampleId sampleId = MP4GetSampleIdFromTime(hFile, trackId, time, false);
  if (sampleId == MP4_INVALID_SAMPLE_ID) {
    // If not found, return first sample
    return 1;
  }
  return sampleId;
}

// Find sync sample (keyframe) STRICTLY BEFORE a given sample
// If target is a keyframe, returns the PREVIOUS keyframe
// Returns 1-based sample ID
static MP4SampleId find_keyframe_before_sample(MP4FileHandle hFile,
                                               MP4TrackId trackId,
                                               MP4SampleId sampleId,
                                               int debug_level) {
  // We need to find the sync sample STRICTLY BEFORE the target.
  // Start searching from sampleId - 1 to exclude the target itself.
  MP4SampleId syncSampleId = MP4_INVALID_SAMPLE_ID;

  // Search backwards from BEFORE the target sample
  MP4SampleId searchStart = (sampleId > 1) ? sampleId - 1 : 1;
  for (MP4SampleId s = searchStart; s >= 1; s--) {
    int8_t isSync = MP4GetSampleSync(hFile, trackId, s);
    if (isSync == 1) {
      syncSampleId = s;
      break;
    }
  }

  if (syncSampleId == MP4_INVALID_SAMPLE_ID) {
    // No keyframe found before target, use first sample
    syncSampleId = 1;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD("Keyframe before sample %u: sample %u (0-based frame %u)",
            sampleId, syncSampleId, syncSampleId - 1);
  }

  return syncSampleId;
}

// Calculate start sample from options
// Returns 1-based sample ID on success, 0 on error
static MP4SampleId calculate_start_sample(Mp4Info& info, float start,
                                          int64_t start_frame,
                                          int64_t start_pts, int debug_level) {
  MP4SampleId sampleId = MP4_INVALID_SAMPLE_ID;

  // Calculate video duration in milliseconds
  uint64_t videoDurationMs = (info.videoDuration * 1000) / info.videoTimescale;

  if (!std::isnan(start)) {
    // --start: time in seconds
    int64_t start_ms;
    if (start < 0) {
      start_ms = static_cast<int64_t>(videoDurationMs) +
                 static_cast<int64_t>(start * 1000.0f);
    } else {
      start_ms = static_cast<int64_t>(start * 1000.0f);
    }

    if (start_ms < 0 || start_ms > static_cast<int64_t>(videoDurationMs)) {
      MP4SEEK_LOGE("Error: start time %lld ms is out of range (0-%llu ms)",
              (long long)start_ms, (unsigned long long)videoDurationMs);
      return 0;
    }

    // Convert ms to timescale
    MP4Timestamp targetTime = (start_ms * info.videoTimescale) / 1000;

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start time: %lld ms (PTS %llu)", (long long)start_ms,
              (unsigned long long)targetTime);
    }

    sampleId = find_sample_at_time(info.hFile, info.videoTrackId, targetTime,
                                   debug_level);

  } else if (start_frame != INT64_MIN) {
    // --start_frame: 0-based frame number
    int64_t frame;
    if (start_frame < 0) {
      frame = static_cast<int64_t>(info.videoSampleCount) + start_frame;
    } else {
      frame = start_frame;
    }

    if (frame < 0 || frame >= static_cast<int64_t>(info.videoSampleCount)) {
      MP4SEEK_LOGE("Error: start frame %lld is out of range (0-%u)",
              (long long)frame, info.videoSampleCount - 1);
      return 0;
    }

    // Convert 0-based frame to 1-based sample ID
    sampleId = static_cast<MP4SampleId>(frame + 1);

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start frame: %lld (sample ID %u)", (long long)frame,
              sampleId);
    }

  } else if (start_pts != INT64_MIN) {
    // --start_pts: PTS in video timescale
    int64_t pts;
    if (start_pts < 0) {
      pts = static_cast<int64_t>(info.videoDuration) + start_pts;
    } else {
      pts = start_pts;
    }

    if (pts < 0 || pts > static_cast<int64_t>(info.videoDuration)) {
      MP4SEEK_LOGE("Error: start PTS %lld is out of range (0-%llu)",
              (long long)pts, (unsigned long long)info.videoDuration);
      return 0;
    }

    if (debug_level > 0) {
      MP4SEEK_LOGD("Start PTS: %lld", (long long)pts);
    }

    sampleId = find_sample_at_time(info.hFile, info.videoTrackId,
                                   static_cast<MP4Timestamp>(pts), debug_level);

  } else {
    MP4SEEK_LOGE(
        "Error: one of --start, --start_frame, or --start_pts is required");
    return 0;
  }

  return sampleId;
}

// Detect if track uses HEVC codec by checking for hvcC atom
static bool is_hevc_track(MP4FileHandle hFile, MP4TrackId trackId) {
  // Try to find hvcC atom in the track's sample description
  // Path: moov.trak[index].mdia.minf.stbl.stsd.hvc1.hvcC
  // Note: MP4HaveAtom uses 0-based track index
  char path[256];

  // Check for hvc1 sample entry
  snprintf(path, sizeof(path), "moov.trak[%u].mdia.minf.stbl.stsd.hvc1.hvcC",
           trackId - 1);
  if (MP4HaveAtom(hFile, path)) {
    return true;
  }

  // Check for hev1 sample entry
  snprintf(path, sizeof(path), "moov.trak[%u].mdia.minf.stbl.stsd.hev1.hvcC",
           trackId - 1);
  if (MP4HaveAtom(hFile, path)) {
    return true;
  }

  return false;
}

// Detect if track uses hev1 (vs hvc1) sample entry
static bool is_hev1_track(MP4FileHandle hFile, MP4TrackId trackId) {
  char path[256];
  snprintf(path, sizeof(path), "moov.trak[%u].mdia.minf.stbl.stsd.hev1",
           trackId - 1);
  return MP4HaveAtom(hFile, path);
}

// Copy video track with samples from startSample to end
static MP4TrackId copy_video_track(MP4FileHandle srcFile, MP4TrackId srcTrackId,
                                   MP4FileHandle dstFile,
                                   MP4SampleId startSample, int debug_level) {
  uint32_t timescale = MP4GetTrackTimeScale(srcFile, srcTrackId);
  MP4SampleId totalSamples = MP4GetTrackNumberOfSamples(srcFile, srcTrackId);

  // Get video properties
  uint16_t width =
      static_cast<uint16_t>(MP4GetTrackVideoWidth(srcFile, srcTrackId));
  uint16_t height =
      static_cast<uint16_t>(MP4GetTrackVideoHeight(srcFile, srcTrackId));

  // Get first sample duration as default
  MP4Duration sampleDuration = MP4GetSampleDuration(srcFile, srcTrackId, 1);

  MP4TrackId dstTrackId = MP4_INVALID_TRACK_ID;

  // Detect codec type and create appropriate track
  if (is_hevc_track(srcFile, srcTrackId)) {
    // HEVC track
    bool use_hev1 = is_hev1_track(srcFile, srcTrackId);
    const char* sampleEntry = use_hev1 ? "hev1" : "hvc1";

    if (debug_level > 0) {
      MP4SEEK_LOGD("Detected HEVC video track with %s sample entry",
              sampleEntry);
    }

    // Get HEVC profile/level from source hvcC
    uint64_t general_profile_idc = 0;
    uint64_t general_level_idc = 0;
    uint64_t lengthSizeMinusOne = 3;  // Default to 4-byte NAL length

    char propPath[256];
    snprintf(propPath, sizeof(propPath),
             "mdia.minf.stbl.stsd.%s.hvcC.general_profile_idc", sampleEntry);
    MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath,
                               &general_profile_idc);

    snprintf(propPath, sizeof(propPath),
             "mdia.minf.stbl.stsd.%s.hvcC.general_level_idc", sampleEntry);
    MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath,
                               &general_level_idc);

    snprintf(propPath, sizeof(propPath),
             "mdia.minf.stbl.stsd.%s.hvcC.lengthSizeMinusOne", sampleEntry);
    MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath,
                               &lengthSizeMinusOne);

    // Create HEVC track
    if (use_hev1) {
      dstTrackId = MP4AddHEV1VideoTrack(
          dstFile, timescale, sampleDuration, width, height,
          static_cast<uint8_t>(general_profile_idc),
          static_cast<uint8_t>(general_level_idc),
          static_cast<uint8_t>(lengthSizeMinusOne));
    } else {
      dstTrackId = MP4AddHEVCVideoTrack(
          dstFile, timescale, sampleDuration, width, height,
          static_cast<uint8_t>(general_profile_idc),
          static_cast<uint8_t>(general_level_idc),
          static_cast<uint8_t>(lengthSizeMinusOne));
    }

    if (dstTrackId != MP4_INVALID_TRACK_ID) {
      // Copy additional hvcC properties from source to destination
      const char* dstSampleEntry = use_hev1 ? "hev1" : "hvc1";

      // Copy profile space, tier flag
      uint64_t val64;
      snprintf(propPath, sizeof(propPath),
               "mdia.minf.stbl.stsd.%s.hvcC.general_profile_space",
               sampleEntry);
      char dstPropPath[256];
      if (MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath, &val64)) {
        snprintf(dstPropPath, sizeof(dstPropPath),
                 "mdia.minf.stbl.stsd.%s.hvcC.general_profile_space",
                 dstSampleEntry);
        MP4SetTrackIntegerProperty(dstFile, dstTrackId, dstPropPath, val64);
      }

      snprintf(propPath, sizeof(propPath),
               "mdia.minf.stbl.stsd.%s.hvcC.general_tier_flag", sampleEntry);
      if (MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath, &val64)) {
        snprintf(dstPropPath, sizeof(dstPropPath),
                 "mdia.minf.stbl.stsd.%s.hvcC.general_tier_flag",
                 dstSampleEntry);
        MP4SetTrackIntegerProperty(dstFile, dstTrackId, dstPropPath, val64);
      }

      snprintf(
          propPath, sizeof(propPath),
          "mdia.minf.stbl.stsd.%s.hvcC.general_profile_compatibility_flags",
          sampleEntry);
      if (MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath, &val64)) {
        snprintf(
            dstPropPath, sizeof(dstPropPath),
            "mdia.minf.stbl.stsd.%s.hvcC.general_profile_compatibility_flags",
            dstSampleEntry);
        MP4SetTrackIntegerProperty(dstFile, dstTrackId, dstPropPath, val64);
      }

      // Copy constraint flags (bytes property)
      uint8_t* constraintFlags = nullptr;
      uint32_t constraintFlagsSize = 0;
      snprintf(propPath, sizeof(propPath),
               "mdia.minf.stbl.stsd.%s.hvcC.general_constraint_indicator_flags",
               sampleEntry);
      if (MP4GetTrackBytesProperty(srcFile, srcTrackId, propPath,
                                   &constraintFlags, &constraintFlagsSize)) {
        snprintf(
            dstPropPath, sizeof(dstPropPath),
            "mdia.minf.stbl.stsd.%s.hvcC.general_constraint_indicator_flags",
            dstSampleEntry);
        MP4SetTrackBytesProperty(dstFile, dstTrackId, dstPropPath,
                                 constraintFlags, constraintFlagsSize);
        free(constraintFlags);
      }

      // Copy other integer properties
      const char* intProps[] = {
          "chromaFormat",         "bitDepthLumaMinus8",
          "bitDepthChromaMinus8", "avgFrameRate",
          "constantFrameRate",    "numTemporalLayers",
          "temporalIdNested",     "min_spatial_segmentation_idc",
          "parallelismType",      "numOfArrays"};
      for (const char* prop : intProps) {
        snprintf(propPath, sizeof(propPath), "mdia.minf.stbl.stsd.%s.hvcC.%s",
                 sampleEntry, prop);
        if (MP4GetTrackIntegerProperty(srcFile, srcTrackId, propPath, &val64)) {
          snprintf(dstPropPath, sizeof(dstPropPath),
                   "mdia.minf.stbl.stsd.%s.hvcC.%s", dstSampleEntry, prop);
          MP4SetTrackIntegerProperty(dstFile, dstTrackId, dstPropPath, val64);
        }
      }

      // Copy NAL arrays data (VPS, SPS, PPS)
      uint8_t* nalArraysData = nullptr;
      uint32_t nalArraysDataSize = 0;
      snprintf(propPath, sizeof(propPath),
               "mdia.minf.stbl.stsd.%s.hvcC.nalArraysData", sampleEntry);
      if (MP4GetTrackBytesProperty(srcFile, srcTrackId, propPath,
                                   &nalArraysData, &nalArraysDataSize)) {
        snprintf(dstPropPath, sizeof(dstPropPath),
                 "mdia.minf.stbl.stsd.%s.hvcC.nalArraysData", dstSampleEntry);
        MP4SetTrackBytesProperty(dstFile, dstTrackId, dstPropPath,
                                 nalArraysData, nalArraysDataSize);
        free(nalArraysData);
      }
    }
  } else {
    // H.264 or other video track
    if (debug_level > 0) {
      MP4SEEK_LOGD("Detected H.264/AVC video track");
    }

    // Get codec configuration
    uint8_t* esConfig = nullptr;
    uint32_t esConfigSize = 0;
    MP4GetTrackESConfiguration(srcFile, srcTrackId, &esConfig, &esConfigSize);

    // Create H.264 video track
    dstTrackId = MP4AddH264VideoTrack(
        dstFile, timescale, sampleDuration, width, height, 0, 0, 0,
        3);  // Simplified - we'll set SPS/PPS separately

    if (dstTrackId == MP4_INVALID_TRACK_ID) {
      // Fallback to generic video track
      dstTrackId = MP4AddVideoTrack(dstFile, timescale, sampleDuration, width,
                                    height, MP4_MPEG4_VIDEO_TYPE);
    }

    // Set ES configuration if available
    if (dstTrackId != MP4_INVALID_TRACK_ID && esConfig != nullptr &&
        esConfigSize > 0) {
      MP4SetTrackESConfiguration(dstFile, dstTrackId, esConfig, esConfigSize);
    }
    if (esConfig != nullptr) {
      free(esConfig);
    }
  }

  if (dstTrackId == MP4_INVALID_TRACK_ID) {
    MP4SEEK_LOGE("Error: could not create video track");
    return MP4_INVALID_TRACK_ID;
  }

  // Copy samples
  uint32_t copiedSamples = 0;
  for (MP4SampleId s = startSample; s <= totalSamples; s++) {
    uint8_t* pBytes = nullptr;
    uint32_t numBytes = 0;
    MP4Duration duration = 0;
    MP4Duration renderingOffset = 0;
    bool isSyncSample = false;

    bool success =
        MP4ReadSample(srcFile, srcTrackId, s, &pBytes, &numBytes, nullptr,
                      &duration, &renderingOffset, &isSyncSample);
    if (!success || pBytes == nullptr) {
      MP4SEEK_LOGE("Error: could not read video sample %u", s);
      return MP4_INVALID_TRACK_ID;
    }

    success = MP4WriteSample(dstFile, dstTrackId, pBytes, numBytes, duration,
                             renderingOffset, isSyncSample);
    free(pBytes);

    if (!success) {
      MP4SEEK_LOGE("Error: could not write video sample %u", s);
      return MP4_INVALID_TRACK_ID;
    }

    copiedSamples++;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD("Copied %u video samples (from %u to %u)", copiedSamples,
            startSample, totalSamples);
  }

  return dstTrackId;
}

// Copy audio track with samples synced to video cut point
static MP4TrackId copy_audio_track(MP4FileHandle srcFile, MP4TrackId srcTrackId,
                                   MP4FileHandle dstFile,
                                   MP4Timestamp videoCutTime,
                                   uint32_t videoTimescale, int debug_level) {
  uint32_t audioTimescale = MP4GetTrackTimeScale(srcFile, srcTrackId);
  MP4SampleId totalSamples = MP4GetTrackNumberOfSamples(srcFile, srcTrackId);

  // Convert video cut time to audio timescale
  MP4Timestamp audioCutTime = (videoCutTime * audioTimescale) / videoTimescale;

  // Find audio sample at this time
  MP4SampleId startSample =
      MP4GetSampleIdFromTime(srcFile, srcTrackId, audioCutTime, false);
  if (startSample == MP4_INVALID_SAMPLE_ID) {
    startSample = 1;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD(
            "Audio cut time: %llu (audio timescale), starting at sample %u",
            (unsigned long long)audioCutTime, startSample);
  }

  // Get audio properties
  MP4Duration sampleDuration = MP4GetSampleDuration(srcFile, srcTrackId, 1);

  // Create audio track
  MP4TrackId dstTrackId = MP4AddAudioTrack(
      dstFile, audioTimescale, sampleDuration, MP4_MPEG4_AUDIO_TYPE);

  if (dstTrackId == MP4_INVALID_TRACK_ID) {
    MP4SEEK_LOGE("Error: could not create audio track");
    return MP4_INVALID_TRACK_ID;
  }

  // Copy ES configuration (decoder specific info)
  uint8_t* esConfig = nullptr;
  uint32_t esConfigSize = 0;
  MP4GetTrackESConfiguration(srcFile, srcTrackId, &esConfig, &esConfigSize);
  if (esConfig != nullptr && esConfigSize > 0) {
    MP4SetTrackESConfiguration(dstFile, dstTrackId, esConfig, esConfigSize);
    free(esConfig);
  }

  // Calculate skip duration for edit list (difference between audio sample
  // start and actual video cut point)
  MP4Timestamp audioSampleStartTime =
      MP4GetSampleTime(srcFile, srcTrackId, startSample);
  MP4Duration skipDuration = 0;
  if (audioCutTime > audioSampleStartTime) {
    skipDuration = audioCutTime - audioSampleStartTime;
  }

  // Copy samples
  uint32_t copiedSamples = 0;
  for (MP4SampleId s = startSample; s <= totalSamples; s++) {
    uint8_t* pBytes = nullptr;
    uint32_t numBytes = 0;
    MP4Duration duration = 0;

    bool success = MP4ReadSample(srcFile, srcTrackId, s, &pBytes, &numBytes,
                                 nullptr, &duration, nullptr, nullptr);
    if (!success || pBytes == nullptr) {
      MP4SEEK_LOGE("Error: could not read audio sample %u", s);
      return MP4_INVALID_TRACK_ID;
    }

    // Audio samples are always sync samples
    success = MP4WriteSample(dstFile, dstTrackId, pBytes, numBytes, duration, 0,
                             true);
    free(pBytes);

    if (!success) {
      MP4SEEK_LOGE("Error: could not write audio sample %u", s);
      return MP4_INVALID_TRACK_ID;
    }

    copiedSamples++;
  }

  if (debug_level > 0) {
    MP4SEEK_LOGD("Copied %u audio samples (from %u to %u)", copiedSamples,
            startSample, totalSamples);
  }

  // Add edit list to skip extra audio at the beginning
  if (skipDuration > 0) {
    // Get total output duration
    MP4Duration totalDuration = MP4GetTrackDuration(dstFile, dstTrackId);

    // Add edit: start at skipDuration, play for (totalDuration - skipDuration)
    MP4AddTrackEdit(dstFile, dstTrackId, MP4_INVALID_EDIT_ID, skipDuration,
                    totalDuration - skipDuration, false);

    if (debug_level > 0) {
      MP4SEEK_LOGD("Audio EDTS: media_time=%llu, duration=%llu",
              (unsigned long long)skipDuration,
              (unsigned long long)(totalDuration - skipDuration));
    }
  }

  return dstTrackId;
}

int mp4seek(const char* infile, const char* outfile, int debug_level,
            float start, int64_t start_frame, int64_t start_pts,
            bool accurate_seek) {
  // 1. Parse input file
  Mp4Info info;
  int result = parse_mp4_file(infile, debug_level, info);
  if (result != 0) {
    return result;
  }

  // 2. Calculate start sample from options
  MP4SampleId targetSample =
      calculate_start_sample(info, start, start_frame, start_pts, debug_level);
  if (targetSample == 0) {
    MP4Close(info.hFile);
    return 1;
  }

  // 3. Find keyframe at or before target sample
  MP4SampleId keyframeSample = find_keyframe_before_sample(
      info.hFile, info.videoTrackId, targetSample, debug_level);

  // 4. Create output file
  MP4FileHandle outFile = MP4Create(outfile);
  if (outFile == MP4_INVALID_FILE_HANDLE) {
    MP4SEEK_LOGE("Error: cannot create output file: %s", outfile);
    MP4Close(info.hFile);
    return 1;
  }

  // Set movie timescale
  MP4SetTimeScale(outFile, info.movieTimescale);

  // 5. Copy video track from keyframe to end
  MP4TrackId outVideoTrackId = copy_video_track(
      info.hFile, info.videoTrackId, outFile, keyframeSample, debug_level);
  if (outVideoTrackId == MP4_INVALID_TRACK_ID) {
    MP4Close(outFile);
    MP4Close(info.hFile);
    return 1;
  }

  // 6. Copy audio track if present
  if (info.audioTrackId != MP4_INVALID_TRACK_ID) {
    // Get video cut time for audio sync
    MP4Timestamp videoCutTime =
        get_sample_time(info.hFile, info.videoTrackId, keyframeSample);

    MP4TrackId outAudioTrackId =
        copy_audio_track(info.hFile, info.audioTrackId, outFile, videoCutTime,
                         info.videoTimescale, debug_level);
    if (outAudioTrackId == MP4_INVALID_TRACK_ID) {
      MP4Close(outFile);
      MP4Close(info.hFile);
      return 1;
    }
  }

  // 7. Close files
  MP4Close(outFile);
  MP4Close(info.hFile);

  // Get output info for logging
  if (debug_level >= 0) {
    MP4FileHandle checkFile = MP4Read(outfile);
    if (checkFile != MP4_INVALID_FILE_HANDLE) {
      MP4TrackId checkVideoTrack =
          MP4FindTrackId(checkFile, 0, MP4_VIDEO_TRACK_TYPE);
      if (checkVideoTrack != MP4_INVALID_TRACK_ID) {
        MP4SampleId outSamples =
            MP4GetTrackNumberOfSamples(checkFile, checkVideoTrack);
        MP4Duration outDuration =
            MP4GetTrackDuration(checkFile, checkVideoTrack);
        uint32_t outTimescale =
            MP4GetTrackTimeScale(checkFile, checkVideoTrack);
        uint64_t outDurationMs = (outDuration * 1000) / outTimescale;
        MP4SEEK_LOGI("Wrote %s (%llu ms, %u frames)", outfile,
                (unsigned long long)outDurationMs, outSamples);
      }
      MP4Close(checkFile);
    }
  }

  return 0;
}
