#include "packet_router.h"
#include <string.h>

#include "xil_printf.h"
#include "xil_io.h"

/* BRAM pointers */
static volatile uint32_t * const bram0 = (volatile uint32_t*)PL_BRAM0_BASE;
static volatile uint32_t * const bram1 = (volatile uint32_t*)PL_BRAM1_BASE;

/* write pointers (bytes) */
static uint32_t wr0 = 0;
static uint32_t wr1 = 0;

/* timing */
static uint64_t g_cntfrq = 1000000000ULL; /* default */

static uint64_t s_last_ms = 0;

/* 1-second stats */
static uint32_t s_pkt_1s = 0;
static uint32_t s_drop_1s = 0;
static uint64_t s_bytes_1s = 0;
static uint32_t s_len_sum_1s = 0;

static uint32_t s_r0_1s = 0, s_r1_1s = 0;
static uint64_t s_tot_r0 = 0, s_tot_r1 = 0, s_tot_drop = 0, s_tot_rx = 0;

static uint64_t s_proc_min = 0, s_proc_max = 0, s_proc_sum = 0;
static uint32_t s_proc_n = 0;

static inline uint64_t cycles_to_ns(uint64_t cyc)
{
    if (g_cntfrq == 0) return 0;
    return (cyc * 1000000000ULL) / g_cntfrq;
}

void router_set_cntfrq(uint64_t cntfrq_hz)
{
    if (cntfrq_hz) g_cntfrq = cntfrq_hz;
}

static int bram_probe(uintptr_t base, const char *name)
{
    volatile uint32_t *p = (volatile uint32_t*)base;
    uint32_t a = 0xA5A55A5Au;
    uint32_t b = 0x5AA5A55Au;

    Xil_Out32((UINTPTR)&p[0], a);
    Xil_Out32((UINTPTR)&p[1], b);

    uint32_t ra = Xil_In32((UINTPTR)&p[0]);
    uint32_t rb = Xil_In32((UINTPTR)&p[1]);

    if (ra != a || rb != b) {
        xil_printf("%s BRAM probe FAIL: got %08x %08x\r\n", name, ra, rb);
        return -1;
    }
    xil_printf("%s BRAM probe OK\r\n", name);
    return 0;
}

/* Safe BRAM write with per-BRAM size limit */
static void bram_write_bytes(volatile uint32_t *bram,
                             uint32_t *wr_bytes,
                             uint32_t bram_size_bytes,
                             const uint8_t *data,
                             uint32_t nbytes)
{
    if (!nbytes) return;

    uint32_t w = *wr_bytes;
    if (w >= bram_size_bytes) return;

    uint32_t space = bram_size_bytes - w;
    if (nbytes > space) nbytes = space;

    uint32_t word_index = w >> 2;
    uint32_t byte_off   = w & 3;

    if (byte_off) {
        uint32_t cur = Xil_In32((UINTPTR)&bram[word_index]);
        uint8_t *c = (uint8_t*)&cur;

        while (byte_off < 4 && nbytes) {
            c[byte_off++] = *data++;
            nbytes--;
            (*wr_bytes)++;
        }
        Xil_Out32((UINTPTR)&bram[word_index], cur);
        word_index++;
    }

    while (nbytes >= 4) {
        uint32_t w32;
        memcpy(&w32, data, 4);
        Xil_Out32((UINTPTR)&bram[word_index], w32);
        word_index++;
        data += 4;
        nbytes -= 4;
        (*wr_bytes) += 4;
    }

    if (nbytes) {
        uint32_t cur = 0;
        uint8_t *c = (uint8_t*)&cur;
        for (uint32_t i = 0; i < nbytes; i++) c[i] = data[i];
        Xil_Out32((UINTPTR)&bram[word_index], cur);
        (*wr_bytes) += nbytes;
    }
}

void router_init(void)
{
    xil_printf("Router init\r\n");
    xil_printf("BRAM0 @ 0x%08lx .. 0x%08lx (%lu bytes)\r\n",
               (unsigned long)PL_BRAM0_BASE,
               (unsigned long)PL_BRAM0_HIGH,
               (unsigned long)PL_BRAM0_SIZE_BYTES);

    xil_printf("BRAM1 @ 0x%08lx .. 0x%08lx (%lu bytes)\r\n",
               (unsigned long)PL_BRAM1_BASE,
               (unsigned long)PL_BRAM1_HIGH,
               (unsigned long)PL_BRAM1_SIZE_BYTES);

    (void)bram_probe(PL_BRAM0_BASE, "BRAM0");
    (void)bram_probe(PL_BRAM1_BASE, "BRAM1");

    wr0 = 0;
    wr1 = 0;

    s_last_ms = 0;
}

void router_handle_payload(const uint8_t *payload, uint32_t len, uint64_t sw_copy_cycles)
{
    s_tot_rx++;
    s_pkt_1s++;
    s_bytes_1s += len;
    s_len_sum_1s += len;

    /* proc stats */
    if (s_proc_n == 0) {
        s_proc_min = sw_copy_cycles;
        s_proc_max = sw_copy_cycles;
    } else {
        if (sw_copy_cycles < s_proc_min) s_proc_min = sw_copy_cycles;
        if (sw_copy_cycles > s_proc_max) s_proc_max = sw_copy_cycles;
    }
    s_proc_sum += sw_copy_cycles;
    s_proc_n++;

    if (len < PKT_HDR_SIZE) {
        s_drop_1s++; s_tot_drop++;
        return;
    }

    pkt_hdr_t h;
    memcpy(&h, payload, PKT_HDR_SIZE);

    if (h.magic != PKT_MAGIC || h.hdr_bytes != PKT_HDR_SIZE) {
        s_drop_1s++; s_tot_drop++;
        return;
    }
    if (h.route > 1) {
        s_drop_1s++; s_tot_drop++;
        return;
    }

    uint32_t avail = len - PKT_HDR_SIZE;
    uint32_t pay = h.payload_bytes;
    if (pay > avail) pay = avail;

    const uint8_t *data = payload + PKT_HDR_SIZE;

    if (h.route == 0) {
        s_r0_1s++; s_tot_r0++;
        bram_write_bytes(bram0, &wr0, PL_BRAM0_SIZE_BYTES, data, pay);
    } else {
        s_r1_1s++; s_tot_r1++;
        bram_write_bytes(bram1, &wr1, PL_BRAM1_SIZE_BYTES, data, pay);
    }
}

void router_tick_1s(uint64_t now_ms)
{
    if (s_last_ms == 0) s_last_ms = now_ms;
    if ((now_ms - s_last_ms) < 1000) return;
    s_last_ms += 1000;

    uint32_t pkt = s_pkt_1s;
    uint64_t B = s_bytes_1s;
    uint32_t drop = s_drop_1s;

    uint32_t avg_len = (pkt ? (s_len_sum_1s / pkt) : 0);
    uint64_t mbps = (B * 8ULL) / 1000000ULL;

    xil_printf("PERF: %lu pkt/s  %llu B/s  ~%llu Mbps  avg_len=%lu  drop=%lu\r\n",
               (unsigned long)pkt,
               (unsigned long long)B,
               (unsigned long long)mbps,
               (unsigned long)avg_len,
               (unsigned long)drop);

    xil_printf("ROUTE: r0=%lu r1=%lu (tot r0=%llu r1=%llu)\r\n",
               (unsigned long)s_r0_1s,
               (unsigned long)s_r1_1s,
               (unsigned long long)s_tot_r0,
               (unsigned long long)s_tot_r1);

    if (s_proc_n) {
        uint64_t avg = s_proc_sum / (uint64_t)s_proc_n;
        xil_printf("PROC: cyc min/avg/max = %llu / %llu / %llu\r\n",
                   (unsigned long long)s_proc_min,
                   (unsigned long long)avg,
                   (unsigned long long)s_proc_max);
        xil_printf("PROC: ns  min/avg/max = %llu / %llu / %llu (cntfrq=%llu)\r\n",
                   (unsigned long long)cycles_to_ns(s_proc_min),
                   (unsigned long long)cycles_to_ns(avg),
                   (unsigned long long)cycles_to_ns(s_proc_max),
                   (unsigned long long)g_cntfrq);
    }

    xil_printf("HB: rx_total=%llu rx_rate=%lu pkt/s\r\n",
               (unsigned long long)s_tot_rx,
               (unsigned long)pkt);

    /* reset 1s counters */
    s_pkt_1s = 0;
    s_drop_1s = 0;
    s_bytes_1s = 0;
    s_len_sum_1s = 0;
    s_r0_1s = 0;
    s_r1_1s = 0;

    s_proc_n = 0;
    s_proc_sum = 0;
    s_proc_min = 0;
    s_proc_max = 0;
}
