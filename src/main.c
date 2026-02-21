#include <stdio.h>
#include <stdint.h>

#include "xil_printf.h"
#include "xparameters.h"

/* lwIP types before platform.h */
#include "lwip/arch.h"
#include "platform.h"

#include "lwip/init.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#include "netif/xadapter.h"

#include "packet_router.h"

/* ===================== User settings ===================== */
#define UDP_LISTEN_PORT  5001

#define IP_ADDR0  192
#define IP_ADDR1  168
#define IP_ADDR2  10
#define IP_ADDR3  10

#define NM_ADDR0  255
#define NM_ADDR1  255
#define NM_ADDR2  255
#define NM_ADDR3  0

#define GW_ADDR0  192
#define GW_ADDR1  168
#define GW_ADDR2  10
#define GW_ADDR3  1
/* ========================================================== */

struct netif server_netif;
static struct udp_pcb *g_pcb = NULL;

volatile uint32_t g_rx_cnt = 0;

/* AArch64 virtual counter */
static inline uint64_t read_cntvct(void)
{
    uint64_t v;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static unsigned emacps_baseaddr(void)
{
#if defined(XPAR_XEMACPS_0_BASEADDR)
    return (unsigned)XPAR_XEMACPS_0_BASEADDR;
#elif defined(XPAR_XEMACPS_1_BASEADDR)
    return (unsigned)XPAR_XEMACPS_1_BASEADDR;
#else
# error "No XPAR_XEMACPS_*_BASEADDR found."
#endif
}

static void send_echo_reply(struct udp_pcb *pcb,
                            const ip_addr_t *addr,
                            u16_t port,
                            const pkt_hdr_t *req,
                            uint64_t board_proc_cycles)
{
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)PKT_HDR_SIZE, PBUF_RAM);
    if (!q) return;

    pkt_hdr_t rep = *req;
    rep.board_proc_cycles = board_proc_cycles;

    pbuf_take(q, &rep, PKT_HDR_SIZE);
    udp_sendto(pcb, q, addr, port);
    pbuf_free(q);
}

/* UDP RX callback (NO PRINTS) */
static void udp_rx_cb(void *arg, struct udp_pcb *pcb,
                      struct pbuf *p,
                      const ip_addr_t *addr,
                      u16_t port)
{
    (void)arg;
    (void)pcb;

    if (!p) return;

    g_rx_cnt++;

    uint64_t t0 = read_cntvct();

    static uint8_t buf[2048];
    uint16_t total = p->tot_len;
    if (total > sizeof(buf)) total = sizeof(buf);

    pbuf_copy_partial(p, buf, total, 0);
    pbuf_free(p);

    uint64_t t1 = read_cntvct();
    uint64_t proc_cycles = (t1 >= t0) ? (t1 - t0) : 0;

    router_handle_payload(buf, (uint32_t)total, proc_cycles);

    /* Echo reply if requested */
    if (total >= PKT_HDR_SIZE) {
        const pkt_hdr_t *h = (const pkt_hdr_t*)buf;
        if (h->magic == PKT_MAGIC && (h->flags & PKT_FLAG_ECHO)) {
            send_echo_reply(g_pcb, addr, port, h, proc_cycles);
        }
    }
}

static int udp_server_init(void)
{
    g_pcb = udp_new();
    if (!g_pcb) {
        xil_printf("ERROR: udp_new failed\r\n");
        return -1;
    }

    if (udp_bind(g_pcb, IP_ADDR_ANY, UDP_LISTEN_PORT) != ERR_OK) {
        xil_printf("ERROR: udp_bind failed\r\n");
        udp_remove(g_pcb);
        g_pcb = NULL;
        return -1;
    }

    udp_recv(g_pcb, udp_rx_cb, NULL);
    xil_printf("UDP server listening on port %d\r\n", UDP_LISTEN_PORT);
    return 0;
}

int main(void)
{
    init_platform();

    xil_printf("\r\n--- ZUBoard UDP Router+Benchmark (STATS ONLY) ---\r\n");

    uint64_t cntfrq = read_cntfrq();
    xil_printf("CNTFRQ (Hz): %llu\r\n", (unsigned long long)cntfrq);
    router_set_cntfrq(cntfrq);

    lwip_init();

    ip_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr,  IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NM_ADDR0, NM_ADDR1, NM_ADDR2, NM_ADDR3);
    IP4_ADDR(&gw,      GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);

    unsigned base = emacps_baseaddr();

    if (!xemac_add(&server_netif, &ipaddr, &netmask, &gw, NULL, base)) {
        xil_printf("ERROR: xemac_add failed\r\n");
        return -1;
    }

    netif_set_default(&server_netif);
    netif_set_up(&server_netif);

    xil_printf("IP: %s  port: %d\r\n", ipaddr_ntoa(&ipaddr), UDP_LISTEN_PORT);

    router_init();

    if (udp_server_init() != 0) {
        xil_printf("ERROR: UDP server init failed\r\n");
        return -1;
    }

    /* 1-second ticker using CNTVCT, not get_time_ms */
    const uint64_t ticks_per_sec = cntfrq;
    uint64_t next_tick = read_cntvct() + ticks_per_sec;

    uint32_t last_rx = 0;

    while (1)
    {
        /* pump ethernet */
        xemacif_input(&server_netif);

        uint64_t now = read_cntvct();
        if ((int64_t)(now - next_tick) >= 0)
        {
            next_tick += ticks_per_sec;

            /* call router report once/sec using "fake ms" */
            static uint64_t fake_ms = 0;
            fake_ms += 1000ULL;
            router_tick_1s(fake_ms);

            /* one clean heartbeat line */
            uint32_t rx = g_rx_cnt;
            uint32_t delta = rx - last_rx;
            last_rx = rx;

            xil_printf("HB: rx_total=%lu rx_rate=%lu pkt/s\r\n",
                       (unsigned long)rx,
                       (unsigned long)delta);
        }
    }

    return 0;
}
