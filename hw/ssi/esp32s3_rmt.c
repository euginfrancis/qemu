/*
 * ESP32 RMT controller
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
#include "hw/ssi/esp32s3_rmt.h"

#define ESP32S3_RMT_REG_SIZE    0x1000

static void restart_timer(Esp32S3RmtState *s, int channel) {
    timer_mod_anticipate_ns(&s->rmt_timer,qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)+1250*s->txlim[channel]);
}

#define DEBUG(x) 

// send txlim data values, stop if a value is 0
// set the correct raw int for tx_end or tx_thr_event
static void send_data(Esp32S3RmtState *s, int channel) {
    DEBUG(printf("send %d %d %d\n",channel, s->txlim[channel], s->sent);)
    BusState *b = BUS(s->rmt);
    BusChild *ch = QTAILQ_FIRST(&b->children);
    SSIPeripheral *slave = SSI_PERIPHERAL(ch->child);
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(slave);
    // don't send data when interrupt hasn't been handled
    // a real device can't do this but it's necessary because 
    // qemu can't always keep up.
    if( s->int_raw & s->int_en & (1<<(channel+8) | (1<<channel))) {
        // send data when the interrupt is cleared
        s->unsent_data=true;
        return;
    } else 
        s->unsent_data=false;
    
    int memsize=((s->conf0[channel]>>16)&0xf)*48;
    int divcnt=((s->conf0[channel]>>8)&0xff);
    DEBUG(printf("divcnt %d memsize \n",divcnt,memsize);)
    for (int i = 0; i < s->txlim[channel] ; i++) {    
        int v=s->data[((i+s->sent)%memsize+channel*48)%512];
        // adjust periods based on the divider
        int d0=v&0x7fff;
        int d1=(v>>16)&0x7fff;
        d0=(d0*divcnt)/8;
        d1=(d1*divcnt)/8;
        v=(v&0x80008000) | d0 | (d1<<16);
        if((v&0x7fff7fff)==0) { // stop sending when we see a zero period
            DEBUG(printf("end send\n");) 
            s->int_raw|=(1<<channel);  // TX_END
            s->int_raw&=~(1<<(channel+8)); // TX_THR
            s->sent=0;
            s->conf0[channel] &= ~1; // TX_START
            if(s->int_en & (1<<channel)) {
                qemu_irq_raise(s->irq);
            }
            return;            
        }
        DEBUG(printf("transfer %x\n",v);)
        ssc->transfer(slave,v);
    }
    s->sent+=s->txlim[channel];
    s->int_raw|=(1<<(channel+8));
    if(s->int_en & (1<<(channel+8)))
        qemu_irq_raise(s->irq);
    restart_timer(s,channel);
}

static void esp32_rmt_timer_cb(void *opaque) {
    Esp32S3RmtState *s = ESP32S3_RMT(opaque);
    // send data for any enabled channels 
    for(int i=0;i<4;i++) {
        if((s->conf0[i] & 1)) {
            send_data(s,i);
        }
    }
}

static uint64_t esp32_rmt_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32S3RmtState *s = ESP32S3_RMT(opaque);
    uint64_t r = 0;
    int channel;
    switch (addr) {
    case A_RMT_CH0CONF0 ... A_RMT_CH3CONF0:
        channel=(addr-A_RMT_CH0CONF0)/4; 
        r=s->conf0[channel];
        break;
    case A_RMT_INT_RAW:
        r = s->int_raw ;
        break;
    case A_RMT_INT_ST:
        r = s->int_raw & s->int_en;
        break;
    case A_RMT_INT_ENA:
        r = s->int_en;
        break;
    case A_RMT_CH0_TX_LIM ... A_RMT_CH3_TX_LIM:
        channel=(addr-A_RMT_CH0_TX_LIM)/4;
        r = s->txlim[channel];
        break;
    case A_RMT_DATA ... A_RMT_DATA+(ESP32S3_RMT_BUF_WORDS-1)* sizeof(uint32_t):
        r = s->data[(addr-A_RMT_DATA)/sizeof(uint32_t)];
        break;
    case A_RMT_SYS_CONF:
        r = s->apb_conf;
        break;
    }
    DEBUG(printf("rmt read %ld %ld\n",addr,r);)
    return r;
}


static void esp32_rmt_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    Esp32S3RmtState *s = ESP32S3_RMT(opaque);
    DEBUG(if(addr<A_RMT_DATA) printf("rmt write %lx %lx\n",addr,value);)
    int channel;
    switch (addr) {
    case A_RMT_CH0CONF0 ... A_RMT_CH3CONF0:
        channel=(addr-A_RMT_CH0CONF0)/4; 
        s->conf0[channel]=value;
        if((value & 0x2)) {
            s->sent=0;
        }
        if((value & 0x1)) {
            // start timer to send data
            restart_timer(s,channel);
        }
        break;
    case A_RMT_CH0_TX_LIM ... A_RMT_CH3_TX_LIM:
        channel=(addr-A_RMT_CH0_TX_LIM)/4;
        s->txlim[channel]=value;
        break;
    case A_RMT_INT_ENA:
        s->int_en=value;
        DEBUG (printf("int ena %x %x\n",s->int_en,s->int_raw);)
        if(s->int_en & s->int_raw)
            qemu_irq_raise(s->irq);
        else
            qemu_irq_lower(s->irq);
        break;
    case A_RMT_INT_CLR:
        s->int_raw&=(~value);
        if((s->int_raw & s->int_en)==0) {
            qemu_irq_lower(s->irq);
        }
        if(s->unsent_data) 
            esp32_rmt_timer_cb(s);
        break;
    case A_RMT_DATA ... A_RMT_DATA+(ESP32S3_RMT_BUF_WORDS-1)* sizeof(uint32_t):
        s->data[(addr-A_RMT_DATA)/sizeof(uint32_t)]=value;
        break;
    case A_RMT_SYS_CONF:
        s->apb_conf=value;
        break;
    }
}


static const MemoryRegionOps esp32_rmt_ops = {
    .read =  esp32_rmt_read,
    .write = esp32_rmt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_rmt_reset(DeviceState *dev)
{
    Esp32S3RmtState *s = ESP32S3_RMT(dev);
    s->int_raw=0;
    s->sent=0;
    s->int_en=0;
    qemu_irq_lower(s->irq);
    timer_del(&s->rmt_timer);
    for(int i=0;i<8;i++) {
        s->conf0[i]=0;
    }
}

static void esp32_rmt_realize(DeviceState *dev, Error **errp)
{
}

static void esp32_rmt_init(Object *obj)
{
    Esp32S3RmtState *s = ESP32S3_RMT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_rmt_ops, s,
                          TYPE_ESP32S3_RMT, ESP32S3_RMT_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    timer_init_ns(&s->rmt_timer, QEMU_CLOCK_VIRTUAL, esp32_rmt_timer_cb, s);
    s->rmt = ssi_create_bus(DEVICE(s), "rmt");
}

static Property esp32_rmt_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void esp32_rmt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = esp32_rmt_reset;
    dc->realize = esp32_rmt_realize;
    device_class_set_props(dc, esp32_rmt_properties);
}

static const TypeInfo esp32_rmt_info = {
    .name = TYPE_ESP32S3_RMT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32S3RmtState),
    .instance_init = esp32_rmt_init,
    .class_init = esp32_rmt_class_init
};

static void esp32_rmt_register_types(void)
{
    type_register_static(&esp32_rmt_info);
}

type_init(esp32_rmt_register_types)
