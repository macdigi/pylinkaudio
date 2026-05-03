"""Subscribe to a Link Audio channel and print buffer stats.

Run this with Live 12.4 (or any Link Audio sender) on the same LAN. The script
will list available channels, subscribe to one, and print stats on each
received buffer for `--duration` seconds.

    python examples/audio_recv.py --duration 5
    python examples/audio_recv.py --channel 0  # auto-pick channel index 0

Ctrl-C to exit.
"""
from __future__ import annotations

import argparse
import sys
import time

import numpy as np

from pylinkaudio import LinkAudio, AudioSource


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--name", default="pylinkaudio-recv",
                        help="Local peer name visible to other peers")
    parser.add_argument("--discover", type=float, default=2.0,
                        help="Seconds to wait for channel discovery (default: 2)")
    parser.add_argument("--channel", type=int, default=None,
                        help="Channel index to auto-pick. If omitted, prompts.")
    parser.add_argument("--duration", type=float, default=5.0,
                        help="Seconds to receive (default: 5)")
    args = parser.parse_args()

    link = LinkAudio(bpm=args.bpm, name=args.name)
    link.enabled = True
    link.link_audio_enabled = True

    print(f"LinkAudio enabled as '{args.name}'. Waiting {args.discover}s for discovery...")
    time.sleep(args.discover)

    channels = link.channels()
    if not channels:
        print("\nNo channels found. Check that:")
        print("  - Live 12.4 is open on the LAN")
        print("  - Live's Link Audio Send is enabled in preferences")
        print("  - You're not on a network blocking multicast")
        return 1

    print(f"\nFound {len(channels)} channel(s):")
    for i, ch in enumerate(channels):
        print(f"  [{i}] {ch.peer_name} | {ch.name}")

    if args.channel is not None:
        if args.channel >= len(channels):
            print(f"--channel {args.channel} out of range")
            return 1
        idx = args.channel
    elif len(channels) == 1:
        idx = 0
    else:
        try:
            idx = int(input(f"\nSelect channel [0-{len(channels) - 1}]: ").strip())
        except (ValueError, EOFError):
            return 1

    ch = channels[idx]
    print(f"\nSubscribing to: {ch.peer_name} | {ch.name}")
    print(f"Receiving for {args.duration}s...\n")

    src = AudioSource(link, ch.id)
    print(f"Queue capacity: {src.capacity()} slots\n")

    received = 0
    total_frames = 0
    rms_running = 0.0
    deadline = time.monotonic() + args.duration

    while time.monotonic() < deadline:
        result = src.read(timeout=0.1)
        if result is None:
            continue
        frames, info = result
        received += 1
        total_frames += info["num_frames"]

        # RMS as a quick sanity check that audio is non-silent
        x = frames.astype(np.float32) / 32768.0
        rms = float(np.sqrt(np.mean(x * x))) if x.size else 0.0
        rms_running = 0.95 * rms_running + 0.05 * rms

        if received <= 3 or received % 50 == 0:
            print(f"  buf #{received:4d}  frames={info['num_frames']:4d}  "
                  f"ch={info['num_channels']}  sr={info['sample_rate']}Hz  "
                  f"tempo={info['tempo']:6.2f}  count={info['count']:6d}  "
                  f"rms={rms:.4f}  pending={src.pending()}")

    print(f"\nDone: {received} buffers, {total_frames} frames total, "
          f"~{total_frames / args.duration:.0f} frames/sec")
    print(f"Smoothed RMS: {rms_running:.4f}  "
          f"(0.0 = silence, 1.0 = full-scale)")

    link.link_audio_enabled = False
    link.enabled = False
    return 0


if __name__ == "__main__":
    sys.exit(main())
