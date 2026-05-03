# Raspberry Pi setup notes

`pylinkaudio` is built and tested for Linux aarch64 — the wheels published to
PyPI install in seconds on Pi OS Bookworm (Python 3.11).

```sh
pip install pylinkaudio
```

That's the supported path. The notes below cover the easy-to-miss
environment bits.

## Wired Ethernet, not WiFi

Link Audio sends multi-channel audio over multicast. WiFi multicast is
unreliable under load — frames get dropped, latency spikes, peer
discovery wobbles. **Plug the Pi into a wire.** This is the single biggest
quality lever.

If you must use WiFi, expect dropouts on a busy network and avoid the
2.4 GHz band on a Pi 3.

## Audio device permissions

For audio I/O on the Pi, your user must be in the `audio` group:

```sh
sudo usermod -a -G audio $USER
# log out / back in for it to take effect
```

The default `pi` user on Pi OS is already in the group.

## ALSA latency

If you use `python-sounddevice` (or any PortAudio-backed library) to wire
device I/O to a `pylinkaudio.AudioSink` / `AudioSource`, the Pi's default
ALSA period sizes are tuned for general use, not low-latency audio. For
Link Audio you want generous-but-not-bloated buffers — start with:

```python
import sounddevice as sd
sd.default.latency = "low"      # asks PortAudio for the lowest sane buffer
sd.default.samplerate = 48000   # match your Link Audio session rate
```

If you hear stutter on the Pi's audio output, raise the period via
`blocksize` in your `sd.OutputStream`/`sd.RawOutputStream` constructor —
512 or 1024 frames @ 48 kHz is a reasonable starting point.

## USB audio interface quirks

Class-compliant USB audio interfaces (Scarlett, Audient, MOTU UltraLite,
etc.) work out of the box on Pi OS. Some devices need:

```
# /boot/firmware/config.txt
max_usb_current=1
```

so the Pi can supply the bus power they want. Reboot after editing.

## When `pip install` falls back to building from source

If your platform doesn't match a published wheel (for example: a 32-bit
Pi OS install, or musllinux), `pip` will compile `pylinkaudio` from
source — which on a 1GB Pi 3B takes 20–30 minutes and can OOM under
default LTO settings. To avoid that:

- **Use a 64-bit Pi OS install.** `lsb_release -a` should show
  `aarch64`. The published wheels target that.
- If you're on a non-standard distribution, install on a faster machine
  and copy the wheel over: `pip wheel pylinkaudio` produces a
  redistributable `.whl`.
