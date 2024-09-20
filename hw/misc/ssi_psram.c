/*
 * ESP-PSRAM basic emulation
 *
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/misc/ssi_psram.h"

#define CMD_READ_ID 0x9f
#define CMD_READ_REG 64
#define PSRAM_ID_MFG 0x0d
#define PSRAM_ID_KGD 0xdd

static int get_eid_by_size(uint32_t size_mbytes) {
    switch (size_mbytes)
    {
    case 2:
        return 0x1;
    case 4:
        return 0x1;
    case 8:
        return 0x3;
    case 16:
        return 0x5;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: PSRAM size %" PRIu32 "MB not implemented\n",
                      __func__, size_mbytes);
        return 0;
    }
}

static uint32_t psram_read(SsiPsramState *s)
{
    uint32_t result = 0;
     uint8_t read_id_response[] = {
            0x00, 0x00, 0x00, 0x00,
            (2<<2) | (1<<5), PSRAM_ID_MFG, (1<<7) | get_eid_by_size(s->size_mbytes) | (2<<3),
            (1<<5) | (1<<6),0,0,0,0,1 | (1<<2),
            0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0
        };
    if (s->command == CMD_READ_ID) {
      int index = s->byte_count - s->dummy;
        if (index < ARRAY_SIZE(read_id_response)) {
            result = read_id_response[index];
        }
    }
    if (s->command == CMD_READ_REG)  {
//	printf("psram_read reg %d %d\n",s->byte_count, s->reg);
        if(s->byte_count >= 10) result=read_id_response[s->reg+4+s->byte_count-10];
    }
//    printf("psram_read %d %d %d\n",s->command,s->byte_count, result);
    return result;
}

static void psram_write(SsiPsramState *s, uint32_t value)
{
//    printf("psram_write %d %d %d\n",s->byte_count, s->dummy, value);
    if (s->byte_count == s->dummy) {
        s->command = value;
    }
    if (s->byte_count == 5)
      	s->reg = value;
    s->byte_count++;
}

static uint32_t psram_transfer(SSIPeripheral *dev, uint32_t value)
{
    SsiPsramState *s = SSI_PSRAM(dev);
    psram_write(s, value);
    return psram_read(s);
}

static int psram_cs(SSIPeripheral *ss, bool select)
{
    SsiPsramState *s = SSI_PSRAM(ss);
    //printf("psram_cs %d\n",select);
    if (!select) {
        s->byte_count = 0;
        s->command = -1;
    }
    return 0;
}

static void psram_realize(SSIPeripheral *ss, Error **errp)
{
    SsiPsramState *s = SSI_PSRAM(ss);
    s->dummy = 1;
}

static Property psram_properties[] = {
    DEFINE_PROP_UINT32("size_mbytes", SsiPsramState, size_mbytes, 4),
    DEFINE_PROP_END_OF_LIST(),
};


static void psram_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->transfer = psram_transfer;
    k->set_cs = psram_cs;
    k->cs_polarity = SSI_CS_LOW;
    k->realize = psram_realize;
    device_class_set_props(dc, psram_properties);
}

static const TypeInfo psram_info = {
    .name          = TYPE_SSI_PSRAM,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SsiPsramState),
    .class_init    = psram_class_init
};

static void psram_register_types(void)
{
    type_register_static(&psram_info);
}

type_init(psram_register_types)
