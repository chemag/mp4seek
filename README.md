# 1. mp4seek

mp4seek is a tool for trimming MP4 files at keyframe boundaries without transcoding. It performs stream copy (remux) operations, copying video NALUs and audio frames without re-encoding.


# 2. Features

* Trim MP4 files from a start position to the end
* Three ways to specify the start position (inspired in ffmpeg's trim filter):
  * Time in seconds (--start)
  * Frame number (--start_frame)
  * PTS value in video timescale (--start_pts)
* Negative values mean "from end of file"
* Positive values mean "from beginning of file"
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
    -d, --debug:          Increase debug verbosity [0]
    -q, --quiet:          Set debug verbosity to -1
    --start <float>:      Start time in seconds (negative = from end)
    --start_frame <int>:  Start frame number (negative = from end)
    --start_pts <int>:    Start PTS in video timescale (negative = from end)
    -h, --help:           Show this help message
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

# 5.4. Debug Output

Add -d flags for debug output:
```bash
./mp4seek -d --start -1.1 input.mp4 output.mp4
./mp4seek -ddd --start_frame -33 input.mp4 output.mp4
```


# 6. How It Works

| Step | Description                                                       |
|------|-------------------------------------------------------------------|
| 1    | Parse MP4 file, extract video and audio tracks                    |
| 2    | Convert start option to a frame number                            |
| 3    | Find the keyframe (sync sample) at or before the start frame      |
| 4    | Cut video track from keyframe to end (stream copy)                |
| 5    | Cut audio track synced to video cut point, with EDTS if needed    |
| 6    | Write output MP4 with both tracks                                 |


# 7. Dependencies

* Bento4 (included as git submodule in lib/bento4)
* CMake 3.10+
* C++17 compiler


# 8. Limitations

* Only processes first video and first audio track
* Requires video track (audio-only files not supported)
* Cut point is always at or before the requested position (snaps to previous keyframe)
