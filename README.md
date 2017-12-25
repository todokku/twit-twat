Twit-Twat
=========

An experiment to receive Twitch.tv streams on Linux with the least possible amount of CPU overhead. Specifically, make use if GStreamer's support of VAAPI plugins.

Note that if VAAPI is not available or not correctly installed, it falls back to less efficient playback silently.

Obviously you need the GStreamer VAAPI plugins installed:

```bash
$ sudo apt install gstreamer1.0-vaapi
```

NVIDIA
------

If you use the NVIDIA proprietary driver you need to install the VDPAU <-> VAAPI bridge as NVIDIA does not directly support VAAPI:

```bash
$ sudo apt install vdpau-va-driver
```

Controls
--------

- **G:** Go to channel
- **+/-:** Change volume
- **D-Click:** Toggle full screen
- **Esc:** Exit full screen
- **S:** Set bandwidth limit
- **H** Controls info
- **Q:** Quit
