#pragma once

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"


#define TYPE_ESP32S3_SENS "misc.esp32s3.sens"
#define ESP32S3_SENS(obj) OBJECT_CHECK(Esp32S3SensState, (obj), TYPE_ESP32S3_SENS)

typedef struct Esp32S3SensState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
} Esp32S3SensState;

REG32(SENS_SAR_MEAS1_CTRL2_REG, 0x00c)
REG32(SENS_SAR_TOUCH_CHN_ST_REG, 0x09c)



