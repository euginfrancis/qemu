/*
 * ESP32-S3 GPIO emulation
 *
 * Copyright (c) 2023 Espressif Systems (Shanghai) Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/esp32s3_gpio.h"

static uint64_t esp32_gpio_read(void *opaque, hwaddr addr, unsigned int size) {
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);
    uint64_t r = 0;
    switch (addr) {
        case A_GPIO_OUT:
            r = s->gpio_out;
            break;
        case A_GPIO_ENABLE:
            r = s->gpio_enable;
            break;
        case A_GPIO_OUT1:
            r = s->gpio_out1;
            break;
        case A_GPIO_ENABLE1:
            r = s->gpio_enable1;
            break;
        case A_GPIO_STRAP:
            r = s->strap_mode;
            break;
        case A_GPIO_IN:
            r = s->gpio_in;
            break;
        case A_GPIO_IN1:
            r = s->gpio_in1;
            break;
        case A_GPIO_STATUS:
            r = s->gpio_status;
            break;
        case A_GPIO_STATUS1:
            r = s->gpio_status1;
            break;
        case A_GPIO_CPU_INT:
            r = s->gpio_cpu_int;
            break;
        case A_GPIO_CPU_INT1:
            r = s->gpio_cpu_int1;
            break;
        case A_GPIO_PIN_BASE ... A_GPIO_FUNC_IN_SEL_CFG_BASE-4:
            r = s->gpio_pin[(addr - A_GPIO_PIN_BASE) / 4];
            break;
        case A_GPIO_FUNC_IN_SEL_CFG_BASE ... A_GPIO_FUNC_OUT_SEL_CFG_BASE-4:
            r = s->gpio_in_sel[(addr - A_GPIO_FUNC_IN_SEL_CFG_BASE) / 4];
            break;
        case A_GPIO_FUNC_OUT_SEL_CFG_BASE ... A_GPIO_FUNC_OUT_SEL_CFG_BASE+0x9c:
            r = s->gpio_out_sel[(addr - A_GPIO_FUNC_OUT_SEL_CFG_BASE) / 4];
        default:
            break;
    }
//    printf("esp32_gpio_read %lx %lx\n",addr,r);
    return r;
}
static int get_triggering(int int_type, int oldval, int val) {
    switch (int_type) {
        case 1:
            return (val > oldval);
        case 2:
            return (val < oldval);
        case 3:
            return (val != oldval);
        case 4:
            return (val == 0);
        case 5:
            return (val == 1);
    }
    return 0;
}

static void set_gpio(void *opaque, int n, int val) {
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);

//    printf("set_gpio %d %d\n",n,val);
    if (n < 32) {
        int oldval = (s->gpio_in >> n) & 1;
        int int_type = (s->gpio_pin[n] >> 7) & 7;
        s->gpio_in &= ~(1 << n);
        s->gpio_in |= (val << n);
        int irq = get_triggering(int_type, oldval, val);
        if (irq && (s->gpio_pin[n] & (1 << 13))) {  // cpu int enable
            qemu_set_irq(s->irq, 1);
            s->gpio_cpu_int |= (1 << n);
        }
    } else {
        int n1 = n - 32;
        int oldval = (s->gpio_in1 >> n1) & 1;
        int int_type = (s->gpio_pin[n] >> 7) & 7;
        s->gpio_in1 &= ~(1 << n1);
        s->gpio_in1 |= (val << n1);
        int irq = get_triggering(int_type, oldval, val);
        if (irq && (s->gpio_pin[n] & (1 << 13))) {  // pro cpu int enable
            qemu_set_irq(s->irq, 1);
            s->gpio_cpu_int1 |= (1 << n1);
        }
    }
}
static void esp32_gpio_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned int size) {
    ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);
    int clearirq;
    uint32_t oldvalue,oldvalue1;
    oldvalue = s->gpio_out;
    oldvalue1 = s->gpio_out1;
//    printf("esp32_gpio_write %lx %lx\n",addr,value);
    switch (addr) {
        case A_GPIO_OUT:
            s->gpio_out = value;
            break;
        case A_GPIO_OUT_W1TS:
            s->gpio_out |= value;
            break;
        case A_GPIO_OUT_W1TC:
            s->gpio_out &= ~value;
            break;
        case A_GPIO_OUT1:
            s->gpio_out1 = value;
            break;
        case A_GPIO_OUT1_W1TS:
            s->gpio_out1 |= value;
            break;
        case A_GPIO_OUT1_W1TC:
            s->gpio_out1 &= ~value;
            break;
        case A_GPIO_STRAP:
            s->strap_mode = value;
            break;
        case A_GPIO_STATUS:
            s->gpio_status = value;
            break;
        case A_GPIO_STATUS_W1TS:
            s->gpio_status |= value;
            break;
        case A_GPIO_STATUS_W1TC:
            clearirq = 1;
            for (int i = 0; i < 32; i++) {
                if ((1 << i) & value) {
                    int int_type = (s->gpio_pin[i] >> 7) & 7;
                    if ((int_type == 4 && !(s->gpio_in & (1 << i))) ||
                        (int_type == 5 && (s->gpio_in & (1 << i))))
                        clearirq = 0;
                }
            }
            if (clearirq) {
                s->gpio_status &= ~value;
                s->gpio_cpu_int &= ~value;
                qemu_set_irq(s->irq, 0);
            }
            break;
        case A_GPIO_STATUS1:
            s->gpio_status1 = value;
            break;
        case A_GPIO_STATUS1_W1TS:
            s->gpio_status1 |= value;
            break;
        case A_GPIO_STATUS1_W1TC:
            clearirq = 1;
            for (int i = 0; i < 17; i++) {
                if ((1 << i) & value) {
                    int int_type = (s->gpio_pin[i + 32] >> 7) & 7;
                    if ((int_type == 4 && !(s->gpio_in1 & (1 << i))) ||
                        (int_type == 5 && (s->gpio_in1 & (1 << i))))
                        clearirq = 0;
                }
            }
            if (clearirq) {
                s->gpio_status1 &= ~value;
                s->gpio_cpu_int1 &= ~value;
                qemu_set_irq(s->irq, 0);
            }
            break;
        case A_GPIO_CPU_INT:
            s->gpio_cpu_int=value;
            break;
        case A_GPIO_CPU_INT1:
            s->gpio_cpu_int1=value;
            break;
        case A_GPIO_PIN_BASE ... A_GPIO_FUNC_IN_SEL_CFG_BASE-4:
            s->gpio_pin[(addr - A_GPIO_PIN_BASE) / 4] = value;
            break;
        case A_GPIO_FUNC_IN_SEL_CFG_BASE ... A_GPIO_FUNC_OUT_SEL_CFG_BASE-4:
            s->gpio_in_sel[(addr - A_GPIO_FUNC_IN_SEL_CFG_BASE)/4] = value;
            break;
        case A_GPIO_FUNC_OUT_SEL_CFG_BASE ... A_GPIO_FUNC_OUT_SEL_CFG_BASE+0x9c:
//            printf("gpio_out_sel %lx %lx\n",(addr - A_GPIO_FUNC_OUT_SEL_CFG_BASE)/4,value);
            s->gpio_out_sel[(addr - A_GPIO_FUNC_OUT_SEL_CFG_BASE)/4] = value;
            break;
    }

    if (s->gpio_out != oldvalue) {
        uint32_t diff = (s->gpio_out ^ oldvalue);
        for (int i = 0; i < 32; i++) {
            if ((1 << i) & diff) {
                qemu_set_irq(s->gpios[i], (s->gpio_out & (1 << i)) ? 1 : 0);
            }
        }
    }
    if (s->gpio_out1 != oldvalue1) {
        uint32_t diff = (s->gpio_out1 ^ oldvalue1);
        for (int i = 0; i < 16; i++) {
            if ((1 << i) & diff) {
                qemu_set_irq(s->gpios[i+32], (s->gpio_out1 & (1 << i)) ? 1 : 0);
            }
        }
    }
}

static void func_gpio(void *opaque, int n, int val) {
        ESP32S3GPIOState *s = ESP32S3_GPIO(opaque);
        int func=(val>>1)&0x1ff;
        int v=val & 1;
        int param=((unsigned)val)>>10;
        for(int i=0;i<40;i++) {
                if((s->gpio_out_sel[i] & 0x1ff ) == func) {
                        qemu_set_irq(s->gpios[i], v+(param<<1));
                }
        }
}

static const MemoryRegionOps gpio_ops = {
    .read = esp32_gpio_read,
    .write = esp32_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32_gpio_reset(DeviceState *dev) {
  ESP32S3GPIOState *s = ESP32S3_GPIO(dev);
  for(int i=0;i<256;i++) {
        s->gpio_in_sel[i]=0;
   }
  for(int i=0;i<40;i++) {
        s->gpio_out_sel[i]=0;
   }
}

static void esp32s3_gpio_init(Object *obj)
{
    ESP32S3GPIOState *s = ESP32S3_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    /* Set the default value for the property */
    object_property_set_int(obj, "strap_mode", ESP32S3_STRAP_MODE_FLASH_BOOT, &error_fatal);
    memory_region_init_io(&s->iomem, obj, &gpio_ops, s,
                          TYPE_ESP32S3_GPIO, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(s), &s->irq, SYSBUS_DEVICE_GPIO_IRQ, 1);
    qdev_init_gpio_out_named(DEVICE(s), s->gpios, ESP32_GPIOS, 49);
    qdev_init_gpio_in_named(DEVICE(s), set_gpio, ESP32_GPIOS_IN, 49);
    qdev_init_gpio_in_named(DEVICE(s), func_gpio, ESP32_GPIOS_FUNC,1);

}

static void esp32_gpio_realize(DeviceState *dev, Error **errp) {}

static Property esp32_gpio_properties[] = {
    /* The strap_mode needs to be explicitly set in the instance init, thus, set
     * the default value to 0. */
    DEFINE_PROP_UINT32("strap_mode", ESP32S3GPIOState, strap_mode, 0),
    DEFINE_PROP_END_OF_LIST(),
};

/* If we need to override any function from the parent (reset, realize, ...), it shall be done
 * in this class_init function */
static void esp32s3_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = esp32_gpio_reset;
    dc->realize = esp32_gpio_realize;
    device_class_set_props(dc, esp32_gpio_properties);
}

static const TypeInfo esp32s3_gpio_info = {
    .name = TYPE_ESP32S3_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ESP32S3GPIOState),
    .instance_init = esp32s3_gpio_init,
    .class_init = esp32s3_gpio_class_init,
    .class_size = sizeof(ESP32S3GPIOClass),
};

static void esp32s3_gpio_register_types(void)
{
    type_register_static(&esp32s3_gpio_info);
}

type_init(esp32s3_gpio_register_types)
