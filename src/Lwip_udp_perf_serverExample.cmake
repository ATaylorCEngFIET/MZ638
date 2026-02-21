set(AXIETHERNET_NUM_DRIVER_INSTANCES "")
set(EMACLITE_NUM_DRIVER_INSTANCES "")
set(EMACPS_NUM_DRIVER_INSTANCES "psu_ethernet_2")
set(EMACPS0_PROP_LIST "0xff0d0000;0x403d")
list(APPEND TOTAL_EMACPS_PROP_LIST EMACPS0_PROP_LIST)
set(TMRCTR_NUM_DRIVER_INSTANCES "")
set(TTCPS_NUM_DRIVER_INSTANCES "psu_ttc_0;psu_ttc_3;psu_ttc_6;psu_ttc_9")
set(TTCPS0_PROP_LIST "0xff110000;0x4024")
list(APPEND TOTAL_TTCPS_PROP_LIST TTCPS0_PROP_LIST)
set(TTCPS1_PROP_LIST "0xff120000;0x4027")
list(APPEND TOTAL_TTCPS_PROP_LIST TTCPS1_PROP_LIST)
set(TTCPS2_PROP_LIST "0xff130000;0x402a")
list(APPEND TOTAL_TTCPS_PROP_LIST TTCPS2_PROP_LIST)
set(TTCPS3_PROP_LIST "0xff140000;0x402d")
list(APPEND TOTAL_TTCPS_PROP_LIST TTCPS3_PROP_LIST)
set(psu_ddr_0_memory_0 "0x0;0x3ff00000")
set(psu_ocm_ram_0_memory_0 "0xfffc0000;0x40000")
set(bram_ctrl0_memory_0 "0xa0000000;0x2000")
set(bram_ctrl1_memory_1 "0xa0002000;0x2000")
set(DDR psu_ddr_0_memory_0)
set(CODE psu_ddr_0_memory_0)
set(DATA psu_ddr_0_memory_0)
set(TOTAL_MEM_CONTROLLERS "psu_ddr_0_memory_0;psu_ocm_ram_0_memory_0;bram_ctrl0_memory_0;bram_ctrl1_memory_1")
set(MEMORY_SECTION "MEMORY
{
	psu_ddr_0_memory_0 : ORIGIN = 0x0, LENGTH = 0x3ff00000
	psu_qspi_linear_0_memory_0 : ORIGIN = 0xc0000000, LENGTH = 0x20000000
	psu_ocm_ram_0_memory_0 : ORIGIN = 0xfffc0000, LENGTH = 0x40000
	bram_ctrl0_memory_0 : ORIGIN = 0xa0000000, LENGTH = 0x2000
	bram_ctrl1_memory_1 : ORIGIN = 0xa0002000, LENGTH = 0x2000
}")
set(STACK_SIZE 0xa000)
set(HEAP_SIZE 0xa000)
