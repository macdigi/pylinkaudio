"""Announce a Link Audio channel and stream a sine wave to it.

Run this with Live 12.4 (or another Link Audio receiver) on the same LAN. The
example announces a channel named `--channel-name`, then continuously sends an
int16 sine tone at the given frequency until the duration elapses.

    python examples/audio_send.py --duration 30
    python examples/audio_send.py --freq 220 --channel-name bass-drone

Ctrl-C to exit.
"""
from __future__ import annotations

import argparse
import sys
import time

import numpy as np

from pylinkaudio import LinkAudio, AudioSink


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bpm", type=float, default=120.0)
    parser.add_argument("--name", default="pylinkaudio-send",
                        help="Local peer name visible to other peers")
    parser.add_argument("--channel-name", default="sine-440",
                        help="Channel name announced to peers")
    parser.add_argument("--freq", type=float, default=440.0,
                        help="Sine frequency in Hz (default: 440)")
    parser.add_argument("--sample-rate", type=int, default=48000,
                        help="Sample rate (default: 48000)")
    parser.add_argument("--block-size", type=int, default=512,
                        help="Frames per buffer commit (default: 512)")
    parser.add_argument("--gain", type=float, default=0.3,
                        help="Output gain 0..1 (default: 0.3)")
    parser.add_argument("--quantum", type=float, default=4.0,
                        help="Beat quantum (default: 4)")
    parser.add_argument("--duration", type=float, default=30.0,
                        help="Seconds to stream (default: 30)")
    args = parser.parse_args()

    link = LinkAudio(bpm=args.bpm, name=args.name)
    link.enabled = True
    link.link_audio_enabled = True

    print(f"LinkAudio enabled as '{args.name}'. Waiting 2s for discovery...")
    time.sleep(2.0)
    print(f"peers={link.num_peers()}")

    sink = AudioSink(link, args.channel_name, max_samples=args.block_size * 2)
    print(f"Announcing channel '{args.channel_name}' "
          f"(max_samples={sink.max_num_samples()})")
    print(f"Streaming {args.freq:.1f} Hz sine, "
          f"{args.block_size} frames @ {args.sample_rate} Hz "
          f"({args.block_size / args.sample_rate * 1000:.1f} ms / block) "
          f"for {args.duration:.0f}s\n")

    sample_index = 0
    block_count = 0
    committed = 0
    block_period = args.block_size / args.sample_rate
    next_send = time.monotonic()
    deadline = time.monotonic() + args.duration

    try:
        while time.monotonic() < deadline:
            indices = (np.arange(args.block_size, dtype=np.float64)
                       + sample_index)
            tone = np.sin(2 * np.pi * args.freq * indices / args.sample_rate)
            block = (tone * args.gain * 32767).clip(-32768, 32767).astype(np.int16)
            sample_index += args.block_size

            ok = sink.write(
                block,
                sample_rate=args.sample_rate,
                quantum=args.quantum,
            )
            block_count += 1
            if ok:
                committed += 1

            if block_count <= 3 or block_count % 100 == 0:
                ratio = committed / block_count if block_count else 0.0
                print(f"  block #{block_count:5d}: "
                      f"committed={'yes' if ok else 'no '} "
                      f"ratio={ratio:.0%} "
                      f"peers={link.num_peers()}")

            next_send += block_period
            sleep_dt = next_send - time.monotonic()
            if sleep_dt > 0:
                time.sleep(sleep_dt)
            else:
                # Falling behind — reset the schedule.
                next_send = time.monotonic()

    except KeyboardInterrupt:
        print("\ninterrupted")

    print(f"\nDone: {committed}/{block_count} blocks committed "
          f"({committed / block_count:.0%} commit rate)" if block_count else "")
    print("Note: 0% commit rate is normal until at least one peer subscribes "
          "to the channel.")

    link.link_audio_enabled = False
    link.enabled = False
    return 0


if __name__ == "__main__":
    sys.exit(main())
