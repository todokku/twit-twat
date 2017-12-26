Twit-Twat
=========

An experiment to receive Twitch.tv streams on Linux with the least possible amount of CPU overhead. Specifically, make use of GStreamer's support of VAAPI and/or NVIDIA decoder plugins.

Note that if VAAPI and/or NVIDIA decoders are not available or not correctly installed, it falls back to less efficient playback silently.

Obviously you need the GStreamer VAAPI plugins installed:

```bash
$ sudo apt install gstreamer1.0-vaapi
```

Controls
--------

- **G**: Go to channel
- **+/-**: Change volume
- **D-Click**: Toggle full screen
- **Esc**: Exit full screen
- **S**: Set bandwidth limit
- **H**: Controls info
- **Q**: Quit
