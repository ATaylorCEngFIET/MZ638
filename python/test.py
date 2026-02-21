#!/usr/bin/env python3
import argparse
import socket
import time
import os

def build_payload(route: int, size: int, pattern: str) -> bytes:
    if size < 2:
        raise ValueError("size must be >= 2")
    data_len = size - 1
    if pattern == "A":
        data = b"A" * data_len
    elif pattern == "B":
        data = b"B" * data_len
    else:
        data = os.urandom(data_len)
    return bytes([route & 0xFF]) + data

def main():
    ap = argparse.ArgumentParser(description="UDP throughput sweep for ZUBoard router")
    ap.add_argument("--ip", required=True, help="Board IP (e.g. 192.168.1.10)")
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--route", type=int, choices=[0, 1], default=0)
    ap.add_argument("--size", type=int, default=513, help="UDP payload bytes incl route byte")
    ap.add_argument("--seconds", type=float, default=5.0, help="Duration to send")
    ap.add_argument("--pps", type=int, default=0, help="Target packets/sec (0 = max speed)")
    ap.add_argument("--pattern", choices=["A", "B", "rand"], default="A")
    ap.add_argument("--report", type=float, default=1.0, help="Report interval seconds")
    args = ap.parse_args()

    dst = (args.ip, args.port)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    payload = build_payload(args.route, args.size, args.pattern)

    interval = (1.0 / args.pps) if args.pps > 0 else 0.0

    t_start = time.perf_counter()
    t_next_report = t_start + args.report

    sent_pkts = 0
    sent_bytes = 0

    # Simple rate control using sleep; for high pps this becomes approximate.
    while True:
        now = time.perf_counter()
        if (now - t_start) >= args.seconds:
            break

        s.sendto(payload, dst)
        sent_pkts += 1
        sent_bytes += len(payload)

        if interval > 0:
            # sleep until next packet time (best effort)
            time.sleep(interval)

        if now >= t_next_report:
            elapsed = now - t_start
            mbps = (sent_bytes * 8) / (elapsed * 1e6) if elapsed > 0 else 0.0
            print(f"{elapsed:6.2f}s  pkts={sent_pkts:10d}  Mbps={mbps:8.3f}")
            t_next_report += args.report

    t_end = time.perf_counter()
    elapsed = t_end - t_start
    mbps = (sent_bytes * 8) / (elapsed * 1e6) if elapsed > 0 else 0.0
    print("\nDONE")
    print(f"Target: {dst}, route={args.route}, payload={len(payload)} bytes")
    print(f"Sent: {sent_pkts} packets, {sent_bytes} bytes in {elapsed:.3f}s")
    print(f"Average throughput: {mbps:.3f} Mbps")
    if args.pps > 0:
        print(f"Requested rate: {args.pps} pps (best-effort)")

if __name__ == "__main__":
    main()
