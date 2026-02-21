#!/usr/bin/env python3
import argparse
import socket
import struct
import time
import select
from dataclasses import dataclass

PKT_MAGIC = 0x504B5452  # 'PKTR'
PKT_HDR_SIZE = 32

PKT_FLAG_ECHO = 1 << 0

# Little-endian packed struct:
# uint32 magic
# uint32 seq
# uint8  route
# uint8  flags
# uint16 hdr_bytes
# uint32 payload_bytes
# uint64 host_tx_ns
# uint64 board_proc_cycles
HDR_FMT = "<IIBBH I Q Q".replace(" ", "")  # same as "<I I B B H I Q Q"
HDR_STRUCT = struct.Struct(HDR_FMT)

@dataclass
class Stats:
    sent: int = 0
    sent_bytes: int = 0
    recv: int = 0
    rtt_min_ns: int = 0
    rtt_max_ns: int = 0
    rtt_sum_ns: int = 0

def now_ns() -> int:
    return time.perf_counter_ns()

def build_packet(seq: int, route: int, flags: int, total_len: int, pattern: int = 0xA5) -> bytes:
    if total_len < PKT_HDR_SIZE:
        raise ValueError(f"total_len must be >= {PKT_HDR_SIZE}")

    payload_bytes = total_len - PKT_HDR_SIZE
    tx_ns = now_ns()

    hdr = HDR_STRUCT.pack(
        PKT_MAGIC,
        seq & 0xFFFFFFFF,
        route & 0xFF,
        flags & 0xFF,
        PKT_HDR_SIZE,
        payload_bytes & 0xFFFFFFFF,
        tx_ns & 0xFFFFFFFFFFFFFFFF,
        0  # board_proc_cycles (board fills in reply if echo)
    )

    # Fill payload with a simple pattern
    if payload_bytes:
        payload = bytes((pattern + (seq & 0xFF) + i) & 0xFF for i in range(payload_bytes))
    else:
        payload = b""

    return hdr + payload

def parse_reply(pkt: bytes):
    if len(pkt) < PKT_HDR_SIZE:
        return None
    try:
        magic, seq, route, flags, hdr_bytes, payload_bytes, host_tx_ns, board_proc_cycles = HDR_STRUCT.unpack_from(pkt, 0)
    except struct.error:
        return None
    if magic != PKT_MAGIC or hdr_bytes != PKT_HDR_SIZE:
        return None
    return {
        "seq": seq,
        "route": route,
        "flags": flags,
        "payload_bytes": payload_bytes,
        "host_tx_ns": host_tx_ns,
        "board_proc_cycles": board_proc_cycles,
    }

def main():
    ap = argparse.ArgumentParser(description="ZUBoard UDP packet generator with 32B header for routing+latency.")
    ap.add_argument("--dst-ip", default="192.168.10.10")
    ap.add_argument("--dst-port", type=int, default=5001)
    ap.add_argument("--src-ip", default=None, help="Bind source IP to force a specific NIC (e.g. 192.168.10.1)")
    ap.add_argument("--route", type=int, default=0, choices=[0, 1], help="Route field in header (0->BRAM0, 1->BRAM1)")
    ap.add_argument("--size", type=int, default=513, help="Total UDP payload size including 32-byte header")
    ap.add_argument("--count", type=int, default=200000)
    ap.add_argument("--rate", type=float, default=0.0, help="Packets per second (0 = as fast as possible)")
    ap.add_argument("--echo", action="store_true", help="Request echo replies and measure RTT")
    ap.add_argument("--timeout-ms", type=int, default=1000, help="Echo receive timeout at end (ms)")
    args = ap.parse_args()

    flags = PKT_FLAG_ECHO if args.echo else 0

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    if args.src_ip:
        # Bind to a specific interface IP so Windows chooses the correct NIC
        sock.bind((args.src_ip, 0))
        print(f"Socket bound to {sock.getsockname()} (forcing NIC)")
    else:
        # Let OS choose
        sock.bind(("", 0))
        print(f"Socket bound to {sock.getsockname()}")

    sock.setblocking(False)

    dst = (args.dst_ip, args.dst_port)
    print(f"Sending to {dst} route={args.route} size={args.size} count={args.count} echo={args.echo} rate={args.rate}")

    stats = Stats()
    t_start = time.time()
    next_report = t_start + 1.0

    # simple pacing
    if args.rate > 0:
        period = 1.0 / args.rate
        next_send_t = time.perf_counter()

    # track RTT by seq when echo enabled
    tx_time_by_seq = {}

    for seq in range(args.count):
        pkt = build_packet(seq, args.route, flags, args.size)
        try:
            sock.sendto(pkt, dst)
        except OSError as e:
            print(f"send error at seq={seq}: {e}")
            break

        stats.sent += 1
        stats.sent_bytes += len(pkt)

        if args.echo:
            # remember tx timestamp from header (host_tx_ns inside packet)
            tx_ns = struct.unpack_from("<Q", pkt, 16)[0]  # host_tx_ns offset: 4+4+1+1+2+4 = 16
            tx_time_by_seq[seq] = tx_ns

        # pacing
        if args.rate > 0:
            next_send_t += period
            while True:
                now = time.perf_counter()
                if now >= next_send_t:
                    break
                time.sleep(min(0.0005, next_send_t - now))

        # poll replies opportunistically
        if args.echo:
            while True:
                r, _, _ = select.select([sock], [], [], 0.0)
                if not r:
                    break
                try:
                    data, _src = sock.recvfrom(4096)
                except BlockingIOError:
                    break
                rep = parse_reply(data)
                if not rep:
                    continue
                seq_r = rep["seq"]
                if seq_r in tx_time_by_seq:
                    rtt = now_ns() - tx_time_by_seq.pop(seq_r)
                    stats.recv += 1
                    if stats.rtt_min_ns == 0 or rtt < stats.rtt_min_ns:
                        stats.rtt_min_ns = rtt
                    if rtt > stats.rtt_max_ns:
                        stats.rtt_max_ns = rtt
                    stats.rtt_sum_ns += rtt

        # report each second
        t_now = time.time()
        if t_now >= next_report:
            dt = t_now - t_start
            bps = stats.sent_bytes / dt
            mbps = (bps * 8.0) / 1e6
            pps = stats.sent / dt
            print(f"THROUGHPUT: sent={stats.sent} pkts, {stats.sent_bytes} bytes in {dt:.3f}s  "
                  f"rate={pps:.0f} pps, {mbps:.1f} Mbps (payload size={args.size})")

            if args.echo and stats.recv:
                avg = stats.rtt_sum_ns / stats.recv
                print(f"ECHO: recv={stats.recv}  rtt_min={stats.rtt_min_ns/1e3:.1f} us  "
                      f"rtt_avg={avg/1e3:.1f} us  rtt_max={stats.rtt_max_ns/1e3:.1f} us  "
                      f"outstanding={len(tx_time_by_seq)}")

            next_report = t_now + 1.0

    # final drain for echo
    if args.echo:
        end_deadline = time.time() + (args.timeout_ms / 1000.0)
        while time.time() < end_deadline and tx_time_by_seq:
            r, _, _ = select.select([sock], [], [], 0.05)
            if not r:
                continue
            try:
                data, _src = sock.recvfrom(4096)
            except BlockingIOError:
                continue
            rep = parse_reply(data)
            if not rep:
                continue
            seq_r = rep["seq"]
            if seq_r in tx_time_by_seq:
                rtt = now_ns() - tx_time_by_seq.pop(seq_r)
                stats.recv += 1
                if stats.rtt_min_ns == 0 or rtt < stats.rtt_min_ns:
                    stats.rtt_min_ns = rtt
                if rtt > stats.rtt_max_ns:
                    stats.rtt_max_ns = rtt
                stats.rtt_sum_ns += rtt

    dt = time.time() - t_start
    bps = stats.sent_bytes / dt if dt > 0 else 0.0
    mbps = (bps * 8.0) / 1e6
    pps = stats.sent / dt if dt > 0 else 0.0

    print("\nDONE")
    print(f"sent={stats.sent} pkts, {stats.sent_bytes} bytes, dt={dt:.3f}s, rate={pps:.0f} pps, {mbps:.1f} Mbps")

    if args.echo:
        if stats.recv:
            avg = stats.rtt_sum_ns / stats.recv
            print(f"echo_recv={stats.recv}/{stats.sent}  "
                  f"rtt_min={stats.rtt_min_ns/1e3:.1f} us  "
                  f"rtt_avg={avg/1e3:.1f} us  "
                  f"rtt_max={stats.rtt_max_ns/1e3:.1f} us  "
                  f"missing={len(tx_time_by_seq)}")
        else:
            print("echo_recv=0 (no replies)")

if __name__ == "__main__":
    main()
