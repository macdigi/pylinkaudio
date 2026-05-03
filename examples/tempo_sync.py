"""Join a Link session and print live tempo, beat, peer count.

Run this, open Live 12.4 (or any Link-capable device) on the same LAN, enable
Link, and you should see peer count > 0 and tempo updates as you change Live's
master tempo.

    python examples/tempo_sync.py

Ctrl-C to exit.
"""
from __future__ import annotations

import argparse
import time

from pylinkaudio import Link


QUANTUM = 4.0  # 4-beat (one bar of 4/4) phase reference


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bpm", type=float, default=120.0,
                        help="Initial tempo if no peers yet (default: 120)")
    parser.add_argument("--quantum", type=float, default=QUANTUM,
                        help="Beat quantum for phase calculations (default: 4)")
    parser.add_argument("--rate-hz", type=float, default=4.0,
                        help="Print rate in Hz (default: 4)")
    parser.add_argument("--duration", type=float, default=None,
                        help="Auto-exit after N seconds (default: run until Ctrl-C)")
    args = parser.parse_args()

    link = Link(bpm=args.bpm)
    link.enabled = True
    link.start_stop_sync_enabled = True

    print(f"Link enabled. Initial tempo: {args.bpm} BPM, quantum: {args.quantum}")
    print("(Ctrl-C to exit.)\n")
    print(f"{'peers':>6} {'tempo':>8} {'beat':>10} {'phase':>7} {'playing':>8}")

    period = 1.0 / args.rate_hz
    t_start = time.monotonic()

    try:
        while True:
            state = link.capture_app_session_state()
            now_us = link.clock_micros()
            tempo = state.tempo()
            beat = state.beat_at_time(now_us, args.quantum)
            phase = state.phase_at_time(now_us, args.quantum)
            is_playing = state.is_playing()

            print(
                f"{link.num_peers():>6d} "
                f"{tempo:>8.2f} "
                f"{beat:>10.3f} "
                f"{phase:>7.3f} "
                f"{'yes' if is_playing else 'no':>8}",
                flush=True,
            )

            if args.duration is not None and (time.monotonic() - t_start) >= args.duration:
                break
            time.sleep(period)
    except KeyboardInterrupt:
        print("\ninterrupted")

    link.enabled = False
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
