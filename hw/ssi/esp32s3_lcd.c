/*
 * ESP32 LCD_CAM controller
 *
 * Copyright (c) 2019 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "hw/dma/esp32s3_gdma.h"
#include "hw/ssi/esp32s3_lcd.h"
#include "qemu/error-report.h"

#define LCD1_DEBUG      0
#define LCD1_WARNING    0


static uint64_t esp32s3_lcd_read(void *opaque, hwaddr addr, unsigned int size)
{
    ESP32S3LcdState *s = ESP32S3_LCD(opaque);

    uint64_t r = 0;

    switch(addr) {
        case A_LCD_CAM_LCD_USER:
            r = s->user;
            break;
        case A_LCD_CAM_LC_DMA_INT_RAW:
            r = s->int_raw;
            break;
        case A_LCD_CAM_LC_DMA_INT_ST:
            r = s->int_raw & s->int_en;
            break;
        case A_LCD_CAM_LC_DMA_INT_ENA:
            r = s->int_en;
            break;
    }

#if LCD1_DEBUG
    info_report("[LCD1] Reading 0x%lx (0x%lx)", addr, r);
#endif
    return r;
}


static void esp32_lcd_timer_cb(void *opaque) {
    ESP32S3LcdState *s = ESP32S3_LCD(opaque);
    s->int_raw |= 2;
    qemu_irq_raise(s->irq);
}


static void esp32s3_lcd_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    ESP32S3LcdState *s = ESP32S3_LCD(opaque);
    uint32_t wvalue = (uint32_t) value;

    switch(addr) {
        case A_LCD_CAM_LCD_USER:
            if(wvalue & 0x08000000) {
//                wvalue = wvalue & (~0x08000000);
                uint32_t gdma_out_idx;
                uint64_t ns_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                if ( !esp32s3_gdma_get_channel_periph(s->gdma, GDMA_LCDCAM, ESP32S3_GDMA_OUT_IDX, &gdma_out_idx) ) {
        	        warn_report("[LCD_CAM] GDMA requested but no properly configured channel found");
                    break;
                }
//                printf("cmd %x %x\n",s->cmd_val,wvalue&0x3fff);
                int tr_size;
                if((wvalue&0x3fff) == 0x3fff)
                    tr_size=0;
                else
                    tr_size=esp32s3_gdma_get_transfer_size(s->gdma, gdma_out_idx);
//                printf("transfer size %x\n",tr_size);
                BusState *b = BUS(s->lcd);
                BusChild *ch = QTAILQ_FIRST(&b->children);
                SSIPeripheral *peripheral = SSI_PERIPHERAL(ch->child);
                SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(peripheral);
                qemu_set_irq(s->cmd_gpio,0);
                ssi_transfer(s->lcd, s->cmd_val);
                qemu_set_irq(s->cmd_gpio,1);
                if(tr_size>0) {
                    uint32_t *buffer=(uint32_t *)malloc(tr_size+4);
                    buffer[(tr_size+3)/4-1]=0;
                    esp32s3_gdma_read_channel(s->gdma, gdma_out_idx, (uint8_t *)buffer, tr_size);
                    for (int i = 0; i <(tr_size+3)/4; i++) {
                        ssc->transfer(peripheral,buffer[i]);
                    }
                    free(buffer);
                }
                uint64_t ns_to_timeout = tr_size * 150;
                if(tr_size>32) {
                    qemu_irq_lower(s->irq);
                    s->user = wvalue;
                    timer_mod_ns(&s->lcd_timer, ns_now + ns_to_timeout);
                } else {
                    wvalue &= ~0x08000000;
                  //s->user = wvalue;
                    s->int_raw |= 2;
                    qemu_irq_raise(s->irq);
                }
	        }
            s->user = wvalue;

            break;
        case A_LCD_CAM_CMD_VAL:
            s->cmd_val = wvalue;
            break;
        case A_LCD_CAM_LC_DMA_INT_ENA:
            s->int_en = wvalue;
            break;
        case A_LCD_CAM_LC_DMA_INT_CLR:
            s->int_raw &= (~wvalue);
            break;
    }


#if LCD1_DEBUG
    info_report("[LCD1] Writing 0x%lx = %08lx", addr, value);
#endif


}


static const MemoryRegionOps esp32s3_lcd_ops = {
    .read =  esp32s3_lcd_read,
    .write = esp32s3_lcd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32s3_lcd_reset(DeviceState *dev)
{
    ESP32S3LcdState *s = ESP32S3_LCD(dev);
    s->int_en=0;
    s->user = 0;
    timer_del(&s->lcd_timer);
}

static void esp32s3_lcd_realize(DeviceState *dev, Error **errp)
{
  //  ESP32S3LcdState *s = ESP32S3_LCD(dev);

}

static void esp32s3_lcd_init(Object *obj)
{
    ESP32S3LcdState *s = ESP32S3_LCD(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32s3_lcd_ops, s,
                          TYPE_ESP32S3_LCD, ESP32S3_LCD_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    timer_init_ns(&s->lcd_timer, QEMU_CLOCK_VIRTUAL, esp32_lcd_timer_cb, s);

    esp32s3_lcd_reset(DEVICE(s));

    s->lcd = ssi_create_bus(DEVICE(s), "LCD");
}

static Property esp32s3_lcd_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32s3_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32s3_lcd_reset;
    dc->realize = esp32s3_lcd_realize;
    device_class_set_props(dc, esp32s3_lcd_properties);
}

static const TypeInfo esp32s3_lcd_info = {
    .name = TYPE_ESP32S3_LCD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3LcdState),
    .instance_init = esp32s3_lcd_init,
    .class_init = esp32s3_lcd_class_init
};

static void esp32s3_lcd_register_types(void)
{
    type_register_static(&esp32s3_lcd_info);
}

type_init(esp32s3_lcd_register_types)
