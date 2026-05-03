# pylinkaudio

Python bindings for [Ableton Link](https://github.com/Ableton/link) 4.0 with **Link Audio** support — multi-channel audio streaming between Link peers over LAN.

> Status: pre-alpha. v0.1 targets Link 4.0 (ships with Live 12.4, May 5 2026).

## Why

The existing Python wrapper, [`aalink`](https://github.com/artfwo/aalink), is asyncio-only and does not support Link Audio. `pylinkaudio` is threaded (asyncio support coming in v0.2), wraps Link 4.0, and exposes the new Link Audio send/receive API to Python with a numpy-frame interface.

## Install

```sh
pip install pylinkaudio
```

Wheels are published for macOS arm64, Linux x86_64, and Linux aarch64 (Raspberry Pi) on Python 3.11–3.13.

## Quickstart

### Tempo / transport sync

```python
from pylinkaudio import Link

link = Link(bpm=120.0)
link.enabled = True
print("peers:", link.num_peers())
```

### Link Audio (coming day 4 of v0.1)

```python
from pylinkaudio import Link, AudioReceiver

link = Link(120.0); link.enabled = True
recv = AudioReceiver(link, channels=2, sample_rate=48000)
recv.start()
frames = recv.read(num_frames=1024, timeout=0.1)  # numpy [channels, frames] float32
```

## Caveats

**macOS 15 (Sequoia) multicast.** Link's peer discovery uses multicast. macOS 15+ requires the `com.apple.developer.networking.multicast` entitlement for sandboxed apps to send/receive multicast. Vanilla `python` from Homebrew or python.org is *not* sandboxed and works fine for development. If you wrap pylinkaudio in a packaged `.app`, you will need that entitlement.

**Raspberry Pi / Linux.** WiFi multicast is unreliable under load — for Link Audio specifically, **wired Ethernet is strongly recommended**. See [examples/raspberry_pi_setup.md](examples/raspberry_pi_setup.md).

**Real-time audio + Python.** The Link audio callback runs on the host real-time audio thread. pylinkaudio bridges this thread to Python via lock-free SPSC ring buffers. Reading from a device callback on the Python side will momentarily hold the GIL — for strict-latency applications, run audio I/O on a dedicated worker that pushes/pulls the ring with a generous buffer.

## License

GPLv3. Ableton Link is dual-licensed GPLv2-or-later / commercial, and any FOSS wrapper inherits the GPL constraint.

**Anything that imports `pylinkaudio` becomes a combined GPLv3 work.** If your project cannot be GPL-compatible, you need a [commercial Link license from Ableton](https://www.ableton.com/en/link/).
