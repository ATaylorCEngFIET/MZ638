#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PKT_MAGIC      0x504B5452
#define PKT_HDR_SIZE   32

#pragma pack(push,1)
typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint8_t  route;
    uint8_t  flags;
    uint16_t hdr_bytes;
    uint32_t payload_bytes;
    uint64_t host_tx_ns;
    uint64_t board_proc_cycles;
} pkt_hdr_t;
#pragma pack(pop)

static uint64_t qpc_freq = 0;

static uint64_t now_ns()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / qpc_freq);
}

int main(int argc, char* argv[])
{
    if (argc < 5) {
        printf("Usage:\n");
        printf("  udp_blaster.exe <dst_ip> <port> <payload_bytes> <seconds> [target_mbps]\n");
        printf("\nExample (max speed):\n");
        printf("  udp_blaster.exe 192.168.10.10 5001 1472 10\n");
        printf("\nExample (rate limited to 900 Mbps):\n");
        printf("  udp_blaster.exe 192.168.10.10 5001 1472 10 900\n");
        return 1;
    }

    const char* dst_ip = argv[1];
    int port = atoi(argv[2]);
    int payload_bytes = atoi(argv[3]);
    int seconds = atoi(argv[4]);
    double target_mbps = 0.0;

    if (argc >= 6)
        target_mbps = atof(argv[5]);

    if (payload_bytes <= 0 || payload_bytes > 1472) {
        printf("Payload must be 1..1472 bytes\n");
        return 1;
    }

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    qpc_freq = freq.QuadPart;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        return 1;
    }

    int sndbuf = 8 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, dst_ip, &dest.sin_addr);

    int total_size = PKT_HDR_SIZE + payload_bytes;
    uint8_t* buffer = (uint8_t*)_aligned_malloc(total_size, 64);

    pkt_hdr_t* hdr = (pkt_hdr_t*)buffer;

    uint64_t start = now_ns();
    uint64_t end = start + (uint64_t)seconds * 1000000000ULL;

    uint64_t packets = 0;
    uint64_t bytes = 0;

    uint64_t next_send_time = start;

    printf("Sending to %s:%d\n", dst_ip, port);
    printf("Payload size: %d bytes\n", payload_bytes);
    if (target_mbps > 0)
        printf("Target rate: %.2f Mbps\n", target_mbps);
    else
        printf("Target rate: MAX SPEED\n");

    while (now_ns() < end)
    {
        uint64_t t = now_ns();

        hdr->magic = PKT_MAGIC;
        hdr->seq = (uint32_t)packets;
        hdr->route = 0;
        hdr->flags = 0;
        hdr->hdr_bytes = PKT_HDR_SIZE;
        hdr->payload_bytes = payload_bytes;
        hdr->host_tx_ns = t;
        hdr->board_proc_cycles = 0;

        int sent = sendto(sock,
            (const char*)buffer,
            total_size,
            0,
            (struct sockaddr*)&dest,
            sizeof(dest));

        if (sent > 0) {
            packets++;
            bytes += sent;
        }

        if (target_mbps > 0) {
            double bits_per_packet = total_size * 8.0;
            double packets_per_sec = (target_mbps * 1000000.0) / bits_per_packet;
            double interval_ns = 1000000000.0 / packets_per_sec;

            next_send_time += (uint64_t)interval_ns;

            while (now_ns() < next_send_time) {
                /* spin */
            }
        }
    }

    uint64_t finish = now_ns();
    double duration = (finish - start) / 1e9;
    double mbps = (bytes * 8.0) / duration / 1e6;

    printf("\nRESULTS:\n");
    printf("Packets sent: %llu\n", packets);
    printf("Bytes sent:   %llu\n", bytes);
    printf("Duration:     %.3f sec\n", duration);
    printf("Throughput:   %.2f Mbps\n", mbps);

    closesocket(sock);
    WSACleanup();
    _aligned_free(buffer);

    return 0;
}