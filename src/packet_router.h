#ifndef PACKET_ROUTER_H
#define PACKET_ROUTER_H

#include <stdint.h>
#include <stddef.h>
#include "xparameters.h"

/* Use canonical XPARs from xparameters.h */
#ifndef XPAR_BRAM_CTRL0_BASEADDR
# error "XPAR_BRAM_CTRL_0_BASEADDR not found (check BD: AXI BRAM Ctrl 0 present?)"
#endif
#ifndef XPAR_BRAM_CTRL0_HIGHADDR
# error "XPAR_BRAM_CTRL0_HIGHADDR not found"
#endif
#ifndef XPAR_BRAM_CTRL1_BASEADDR
# error "XPAR_BRAM_CTRL_1_BASEADDR not found"
#endif
#ifndef XPAR_BRAM_CTRL1_HIGHADDR
# error "XPAR_BRAM_CTRL_1_HIGHADDR not found"
#endif

#define PL_BRAM0_BASE   ((uintptr_t)XPAR_BRAM_CTRL0_BASEADDR)
#define PL_BRAM0_HIGH   ((uintptr_t)XPAR_BRAM_CTRL0_HIGHADDR)
#define PL_BRAM1_BASE   ((uintptr_t)XPAR_BRAM_CTRL1_BASEADDR)
#define PL_BRAM1_HIGH   ((uintptr_t)XPAR_BRAM_CTRL1_HIGHADDR)

/* Auto-compute actual mapped size */
#define PL_BRAM0_SIZE_BYTES ((uint32_t)(PL_BRAM0_HIGH - PL_BRAM0_BASE + 1U))
#define PL_BRAM1_SIZE_BYTES ((uint32_t)(PL_BRAM1_HIGH - PL_BRAM1_BASE + 1U))

#define PKT_MAGIC      0x504B5452u /* 'PKTR' */
#define PKT_HDR_SIZE   32u
#define PKT_FLAG_ECHO  (1u << 0)

/* 32-byte wire header (little-endian) */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint8_t  route;             /* 0 -> BRAM0, 1 -> BRAM1 */
    uint8_t  flags;             /* bit0 echo request */
    uint16_t hdr_bytes;         /* must be 32 */
    uint32_t payload_bytes;     /* bytes after header */
    uint64_t host_tx_ns;        /* PC timestamp */
    uint64_t board_proc_cycles; /* board fills (if echo reply) */
} pkt_hdr_t;

void router_set_cntfrq(uint64_t cntfrq_hz);
void router_init(void);

/* payload points to UDP payload (header+data), len is tot_len */
void router_handle_payload(const uint8_t *payload, uint32_t len, uint64_t sw_copy_cycles);

/* call from main loop every iteration with current ms tick */
void router_tick_1s(uint64_t now_ms);

#endif
