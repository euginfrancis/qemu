#pragma once

#include "hw/hw.h"
#include "hw/registerfields.h"
#include "hw/ssi/ssi.h"

#define TYPE_ESP32S3_RMT "ssi.esp32s3.rmt"
#define ESP32S3_RMT(obj) OBJECT_CHECK(Esp32S3RmtState, (obj), TYPE_ESP32S3_RMT)

#define ESP32S3_RMT_CS_COUNT      3
#define ESP32S3_RMT_BUF_WORDS     512

typedef struct Esp32S3RmtState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    int num_cs;
    SSIBus *rmt;
    uint32_t conf0[8];
   // uint32_t conf1[8];
    uint32_t int_raw;
    uint32_t int_en;
    uint32_t txlim[8];
    uint32_t apb_conf;
    int sent;
    bool unsent_data;
    uint32_t data[ESP32S3_RMT_BUF_WORDS];
    QEMUTimer rmt_timer;
} Esp32S3RmtState;


REG32(RMT_CH0CONF0, 0x20)
REG32(RMT_CH1CONF0, 0x24)
REG32(RMT_CH2CONF0, 0x28)
REG32(RMT_CH3CONF0, 0x2c)
REG32(RMT_CH4CONF0, 0x30)
REG32(RMT_CH4CONF1, 0x34)
REG32(RMT_CH5CONF0, 0x38)
REG32(RMT_CH5CONF1, 0x3c)
REG32(RMT_CH6CONF0, 0x40)
REG32(RMT_CH6CONF1, 0x44)
REG32(RMT_CH7CONF0, 0x48)
REG32(RMT_CH7CONF1, 0x4c)
REG32(RMT_INT_RAW, 0x70)
REG32(RMT_INT_ST, 0x74)
REG32(RMT_INT_ENA, 0x78)
REG32(RMT_INT_CLR, 0x7c)
REG32(RMT_DATA, 0x800)
REG32(RMT_CH0_TX_LIM,0xa0)
REG32(RMT_CH1_TX_LIM,0xa4)
REG32(RMT_CH2_TX_LIM,0xa5)
REG32(RMT_CH3_TX_LIM,0xac)
REG32(RMT_SYS_CONF,0xc0)




