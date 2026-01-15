# 1. mp4seek

mp4seek is a tool for trimming MP4 files at keyframe boundaries without transcoding. It performs stream copy (remux) operations, copying video NALUs and audio frames without re-encoding.

mp4seek is based on [Bento4](https://github.com/axiomatic-systems/Bento4.git). The goal is to show how to use bento4 to edit ISOBMFF files.


# 2. Features

* Trim MP4 files from a start position to the end
* Three ways to specify the start position (inspired in ffmpeg's trim filter):
  * Time in seconds (--start)
  * Frame number (--start_frame)
  * PTS value in video timescale (--start_pts)
* Negative values mean "from end of file"
* Positive values mean "from beginning of file"
* Accurate seek mode (default): uses EDTS to seek to exact position
* Noaccurate seek mode: seeks to nearest keyframe only
* Handles both video and audio tracks
* Manages EDTS/ELST (Edit List) for audio sync
* Stream copy only (no transcoding)


# 3. Build Instructions

```bash
cd mp4seek
mkdir -p build && cd build
cmake ..
make
```


# 4. Usage

```
usage: mp4seek [options] <infile> <outfile>
where options are:
    -d, --debug:            Increase debug verbosity [0]
    -q, --quiet:            Set debug verbosity to -1
    --start <float>:        Start time in seconds (negative = from end)
    --start_frame <int>:    Start frame number (negative = from end)
    --start_pts <int>:      Start PTS in video timescale (negative = from end)
    --accurate_seek:        Seek to exact position using EDTS [default]
    --noaccurate_seek:      Seek to nearest keyframe only
    -h, --help:             Show this help message
```


# 5. Examples

# 5.1. Trim Using Time (--start)

Trim from 3.9 seconds from the beginning:
```bash
./mp4seek --start 3.9 input.mp4 output.mp4
```

Trim from 1.1 seconds before the end (equivalent to old --sseof 1.1):
```bash
./mp4seek --start -1.1 input.mp4 output.mp4
```

# 5.2. Trim Using Frame Number (--start_frame)

Trim from frame 100:
```bash
./mp4seek --start_frame 100 input.mp4 output.mp4
```

Trim from 33 frames before the end:
```bash
./mp4seek --start_frame -33 input.mp4 output.mp4
```

# 5.3. Trim Using PTS (--start_pts)

Trim from PTS value 59904 (in video timescale units):
```bash
./mp4seek --start_pts 59904 input.mp4 output.mp4
```

Trim from 16896 PTS units before the end:
```bash
./mp4seek --start_pts -16896 input.mp4 output.mp4
```

# 5.4. Accurate vs Noaccurate Seek

By default, mp4seek uses accurate seek, which adds EDTS (Edit List) to the video track to skip pre-roll frames and start playback at the exact requested position.

Use noaccurate seek to start playback at the keyframe (may start earlier than requested):
```bash
./mp4seek --noaccurate_seek --start -1.1 input.mp4 output.mp4
```

# 5.5. Debug Output

Add -d flags for debug output:
```bash
./mp4seek -d --start -1.1 input.mp4 output.mp4
./mp4seek -ddd --start_frame -33 input.mp4 output.mp4
```


# 6. How It Works

| Step | Description                                                                   |
|------|-------------------------------------------------------------------------------|
| 1    | Parse MP4 file, extract video and audio tracks                                |
| 2    | Convert start option to a frame number                                        |
| 3    | Find the keyframe (sync sample) at or before the start frame                  |
| 4    | Cut video track from keyframe to end (stream copy)                            |
| 5    | If accurate_seek, add video EDTS to skip pre-roll frames                      |
| 6    | Cut audio track synced to video cut point, with EDTS if needed                |
| 7    | Write output MP4 with both tracks                                             |


# 7. Dependencies

* Bento4 (included as git submodule in lib/bento4)
* CMake 3.10+
* C++17 compiler


# 8. Limitations

* Only processes first video and first audio track
* Requires video track (audio-only files not supported)
* With --noaccurate_seek, cut point is always at or before the requested position (snaps to previous keyframe)
* With --accurate_seek (default), video starts at exact position via EDTS but includes pre-roll frames from keyframe


# Appendix 1. Atom Ordering (Fast Start)

mp4seek always writes the output file with the moov (metadata) atom before mdat (actual video data), regardless of the input file's atom order. This is commonly called "fast start" or "web optimized" layout.

| Layout        | Atom Order       | Behavior                                        |
|---------------|------------------|-------------------------------------------------|
| moov at end   | ftyp, mdat, moov | Must read entire file before playback can start |
| moov at front | ftyp, moov, mdat | Enables progressive playback and streaming      |

Input files may have moov at the end (common for single-pass encoding) or at the end. mp4seek outputs are always moov-first. This is generally desirable for streaming and web delivery. Tools like ffmpeg use `-movflags +faststart` to achieve the same result.
