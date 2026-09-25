#!/usr/bin/env python3
"""Measure CameraCaptureRawBytes-compatible streams (requires pyserial).

Use the same port/cable/host setup for both boards. No visualizer may be open.
The first three frames are warmup. This measures request-to-full-frame latency,
including capture/queueing, serial transport and host overhead, not sensor FPS.
"""

import argparse
import statistics
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--bytes", type=int, default=320 * 240 * 2)
    args = parser.parse_args()
    if args.frames <= 0 or args.bytes <= 0:
        parser.error("--frames and --bytes must be positive")

    samples = []
    with serial.Serial(args.port, 115200, timeout=0.5, write_timeout=2) as port:
        time.sleep(2)
        port.reset_input_buffer()
        for index in range(args.frames + 3):
            start = time.perf_counter()
            port.write(b"\x01")
            received = 0
            while received < args.bytes:
                received += len(port.read(args.bytes - received))
                if time.perf_counter() - start > 10:
                    raise RuntimeError(
                        f"Frame timeout: {received}/{args.bytes} bytes. "
                        "Check mode, resolution, logs on USB and camera errors."
                    )
            duration = time.perf_counter() - start
            if index >= 3:
                samples.append(duration)

    mean = statistics.mean(samples)
    print(f"Frames: {len(samples)}, FPS: {1 / mean:.2f}, "
          f"payload: {args.bytes / mean / 1e6:.3f} MB/s")
    print(f"Frame latency ms: min={min(samples)*1000:.1f}, "
          f"median={statistics.median(samples)*1000:.1f}, "
          f"max={max(samples)*1000:.1f}")


if __name__ == "__main__":
    main()
