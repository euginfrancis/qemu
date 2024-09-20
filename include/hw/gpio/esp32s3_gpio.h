#pragma once

#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/registerfields.h"

#define TYPE_ESP32S3_GPIO "esp32s3.gpio"
#define ESP32S3_GPIO(obj)           OBJECT_CHECK(ESP32S3GPIOState, (obj), TYPE_ESP32S3_GPIO)
#define ESP32S3_GPIO_GET_CLASS(obj) OBJECT_GET_CLASS(ESP32S3GPIOClass, obj, TYPE_ESP32S3_GPIO)
#define ESP32S3_GPIO_CLASS(klass)   OBJECT_CLASS_CHECK(ESP32S3GPIOClass, klass, TYPE_ESP32S3_GPIO)

/* Bootstrap options for ESP32-S3 (4-bit) */
#define ESP32S3_STRAP_MODE_FLASH_BOOT 0x4   /* SPI Boot */

typedef struct ESP32S3State {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t gpio_out;
    uint32_t gpio_out1;
    uint32_t strap_mode;
    uint32_t gpio_in;
    uint32_t gpio_in1;
    uint32_t gpio_status;
    uint32_t gpio_status1;
    uint32_t gpio_enable;
    uint32_t gpio_enable1;
    uint32_t gpio_cpu_int;
    uint32_t gpio_cpu_int1;
    uint32_t gpio_pin[49];
    uint32_t gpio_in_sel[256];
    uint32_t gpio_out_sel[49];
    qemu_irq gpios[49];
} ESP32S3GPIOState;

typedef struct ESP32S3GPIOClass {
    SysBusDeviceClass parent_class;
} ESP32S3GPIOClass;


REG32(GPIO_OUT, 0x04)
REG32(GPIO_OUT_W1TS, 0x08)
REG32(GPIO_OUT_W1TC, 0x0c)
REG32(GPIO_OUT1, 0x10)
REG32(GPIO_OUT1_W1TS, 0x14)
REG32(GPIO_OUT1_W1TC, 0x18)
REG32(GPIO_ENABLE, 0x020)
REG32(GPIO_ENABLE_W1TS, 0x024)
REG32(GPIO_ENABLE_W1TC, 0x028)
REG32(GPIO_ENABLE1, 0x02c)
REG32(GPIO_ENABLE1_W1TS, 0x030)
REG32(GPIO_ENABLE1_W1TC, 0x034)
REG32(GPIO_STRAP, 0x0038)
REG32(GPIO_IN, 0x003c)
REG32(GPIO_IN1, 0x0040)
REG32(GPIO_STATUS, 0x0044)
REG32(GPIO_STATUS_W1TS, 0x0048)
REG32(GPIO_STATUS_W1TC, 0x004c)
REG32(GPIO_STATUS1, 0x0050)
REG32(GPIO_STATUS1_W1TS, 0x0054)
REG32(GPIO_STATUS1_W1TC, 0x0058)
REG32(GPIO_CPU_INT, 0x005c)
REG32(GPIO_CPU_INT1, 0x0068)
REG32(GPIO_PIN_BASE,0x74)
REG32(GPIO_FUNC_IN_SEL_CFG_BASE,0x154)
REG32(GPIO_FUNC_OUT_SEL_CFG_BASE,0x548)

#define ESP32_GPIOS "esp32_gpios"
#define ESP32_GPIOS_IN "esp32_gpios_in"
#define ESP32_GPIOS_FUNC "esp32_gpios_func"
