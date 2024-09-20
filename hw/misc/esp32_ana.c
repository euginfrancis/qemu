#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/misc/esp32_ana.h"

int esp32_wifi_channel=0;

static uint64_t esp32_ana_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32AnaState *s = ESP32_ANA(opaque);
    uint32_t r = s->mem[addr/4];
    if(s->iss3) {
        switch(addr) {
            case 0:
            case 0x48:
                r=0x00ffffff;
            break;
            case 4:
                r= 0xFDFFFFFF;
            break;
            case 0x50:
                r|=0x07000000;
            break;
            case 0x44:
            case 0x4c:
            case 0xc4:
                r=0xFFFFFFFF;
            break;
        }
    } else {
        switch(addr) {
            case 4:
                r= 0xFDFFFFFF;
            break;
            case 0x44:
            case 0x4c:
            case 0xc4:
                r=0xFFFFFFFF;
            break;
        }
    }
    return r;
}

static void esp32_ana_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size) {
    Esp32AnaState *s = ESP32_ANA(opaque);
    if(addr==196 && !s->iss3) {
        int v=value&255;
        if((v%10)==4) {
            esp32_wifi_channel=(v/10)-1;
//            printf("wifi channel:%x %d\n",(int)value, esp32_wifi_channel);
        }
    }
    if(addr==0x150 && s->iss3) {
        int v= (value & 0x0FF00000) >> 0x14;
        esp32_wifi_channel=(v-7)/5;
        if(esp32_wifi_channel > 14) esp32_wifi_channel = 14;
//        printf("wifi channel:%x %d\n",(int)value, esp32_wifi_channel);
    }
//    if(addr==196 && s->iss3) {
//        printf("wifi channelxx:%x\n",(int)value);
//    }
    s->mem[addr/4]=value;
}

static const MemoryRegionOps esp32_ana_ops = {
    .read =  esp32_ana_read,
    .write = esp32_ana_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_ana_init(Object *obj)
{
    Esp32AnaState *s = ESP32_ANA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32_ana_ops, s,
                          TYPE_ESP32_ANA, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    memset(s->mem,0,sizeof(s->mem));
}


static const TypeInfo esp32_ana_info = {
    .name = TYPE_ESP32_ANA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Esp32AnaState),
    .instance_init = esp32_ana_init,
};

static void esp32_ana_register_types(void)
{
    type_register_static(&esp32_ana_info);
}

type_init(esp32_ana_register_types)
