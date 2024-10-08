#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/misc/esp32c3_ledc.h"

#define ESP32C3_LEDC_REGS_SIZE (A_LEDC_CONF_REG + 4)

static Esp32C3LEDCState *current_state;
// calculate on and off time in microseconds
// index is the channel index 0-7 
static void get_duty_time(Esp32C3LEDCState *s, int index, uint32_t *on_time,
                          uint32_t *off_time) {
    int timer = s->channel_conf0_reg[index] & 3;
    int expire = (s->duty_reg[index] & ((1 << 19) - 1))>>4;
    uint32_t duty_res;

    duty_res = s->duty_res[timer];
   
    int duty_max = 1 << duty_res ;
    int clk=s->freq[timer];
    int64_t timeus = /*duty_max **/ (1000000000l / clk);
    *on_time = expire * timeus / duty_max;
    if (*on_time == 0) {
        *on_time = 1000;
    }
    if (*on_time > timeus - 1000) {
        *on_time = timeus - 1000;
    }
    *off_time = timeus - *on_time;
    //printf("duty time %d %d %d %d %d %d %ld\n",timer, expire,duty_max, *on_time,*off_time, clk, timeus);
}

static void ledc_timer_cb(void *v) {
    long long int index = (long long int)v;
    Esp32C3LEDCState *s = current_state;
    uint32_t on_time, off_time;
    get_duty_time(s, index, &on_time, &off_time);
    if (s->op_val[index] == 0) {
        s->op_val[index] = 1;
        qemu_set_irq(s->func_irq,
                     (73 + index) * 2 + 1 + ((on_time / 1000) << 10));
        timer_mod_anticipate_ns(
            &s->led_timer[index],
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + on_time);
    } else {
        s->op_val[index] = 0;
        qemu_set_irq(s->func_irq, (73 + index) * 2 + ((off_time / 1000) << 10));
        timer_mod_anticipate_ns(
            &s->led_timer[index],
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + off_time);
        uint32_t c1 = s->channel_conf1_reg[index];
        int duty_num = (c1 >> 20) & 0x3ff;
        int duty_cycle = (c1 >> 10) & 0x3ff;
        int duty_scale = (c1 & 0x3ff) * 16;
        int duty_inc = c1 & (1 << 30);
        if (duty_num != 0) {  // fade control
            s->cycle[index]++;
            //printf("fade %d %d %d %d\n",duty_num,duty_cycle,duty_scale/16,s->duty_reg[index]/16);
            if (s->cycle[index] >= duty_cycle) {
                s->cycle[index] = 0;
                if (duty_inc) {
                    s->duty_reg[index] += duty_scale;
                } else {
                    s->duty_reg[index] -= duty_scale;
                }
                if(s->duty_reg[index]<0)
                    s->duty_reg[index]=0;
                duty_num--;
                if (duty_num < 0) duty_num = 0;
                if (duty_num == 0 && (s->int_en & (1 << (index + 4)))) {
                    s->int_raw |= (1 << (index + 4));
                    qemu_irq_raise(s->irq);
                }
                s->channel_conf1_reg[index] =
                    (s->channel_conf1_reg[index] & 0xc00fffff) | duty_num << 20;
            }
        }
    }
}

static uint64_t esp32c3_ledc_read(void *opaque, hwaddr addr, unsigned int size)
{
    Esp32C3LEDCState *s = ESP32C3_LEDC(opaque);
    uint64_t r = 0;
    switch (addr) {
    case A_LEDC_TIMER0_CONF_REG ... A_LEDC_TIMER3_CONF_REG:
        r = s->timer_conf_reg[(addr - A_LEDC_TIMER0_CONF_REG) / 0x8];
        break;

    case A_LEDC_CH0_CONF0_REG:
    case A_LEDC_CH1_CONF0_REG:
    case A_LEDC_CH2_CONF0_REG:
    case A_LEDC_CH3_CONF0_REG:
    case A_LEDC_CH4_CONF0_REG:
    case A_LEDC_CH5_CONF0_REG:
    case A_LEDC_CH6_CONF0_REG:
    case A_LEDC_CH7_CONF0_REG:
        r = s->channel_conf0_reg[(addr - A_LEDC_CH0_CONF0_REG) / 0x14];
        break;
    case A_LEDC_CH0_CONF1_REG:
    case A_LEDC_CH1_CONF1_REG:
    case A_LEDC_CH2_CONF1_REG:
    case A_LEDC_CH3_CONF1_REG:
    case A_LEDC_CH4_CONF1_REG:
    case A_LEDC_CH5_CONF1_REG:
    case A_LEDC_CH6_CONF1_REG:
    case A_LEDC_CH7_CONF1_REG:
        r = s->channel_conf1_reg[(addr - A_LEDC_CH0_CONF1_REG) / 0x14];
        break;
    case A_LEDC_CH0_DUTY_REG:
    case A_LEDC_CH1_DUTY_REG:
    case A_LEDC_CH2_DUTY_REG:
    case A_LEDC_CH3_DUTY_REG:
    case A_LEDC_CH4_DUTY_REG:
    case A_LEDC_CH5_DUTY_REG:
    case A_LEDC_CH6_DUTY_REG:
    case A_LEDC_CH7_DUTY_REG:
        r = s->duty_init_reg[(addr - A_LEDC_CH0_DUTY_REG) / 0x14];
        break;
    case A_LEDC_CH0_DUTY_R_REG:
    case A_LEDC_CH1_DUTY_R_REG:
    case A_LEDC_CH2_DUTY_R_REG:
    case A_LEDC_CH3_DUTY_R_REG:
    case A_LEDC_CH4_DUTY_R_REG:
    case A_LEDC_CH5_DUTY_R_REG:
    case A_LEDC_CH6_DUTY_R_REG:
    case A_LEDC_CH7_DUTY_R_REG:
        r = s->duty_reg[(addr - A_LEDC_CH0_DUTY_R_REG) / 0x14];
        break;
    
    case A_LEDC_INT_RAW:
        r = s->int_raw;
        break;
    case A_LEDC_INT_ST:
        r = s->int_raw & s->int_en;
        break;
    case A_LEDC_INT_ENA:
        r = s->int_en;
        break;
    case A_LEDC_CONF_REG:
        r = s->conf_reg;
        break;
    }
    //printf("ledc read %lx %lx\n",addr,r);
    return r;
}

static uint32_t esp32c3_ledc_get_percent(Esp32C3LEDCState *s, uint32_t value, int  channel)
{
    uint32_t duty_val =  (value >> 4) & ((1 << 14) - 1);
    uint32_t duty_res = 0;
        /* get duty res for the high speed channel from high speed timer */
    duty_res = s->duty_res[(s->channel_conf0_reg[channel] & ((1 << 2) - 1))];

    if(duty_res){
        s->duty[channel] = (100.0 * (value & ((1 << 18) - 1))) / (16.0 * ((2 << (duty_res - 1)) - 1));
    } 
    else{
        s->duty[channel] = 0;
    }

    return duty_res ? (100 * duty_val / ((2 << (duty_res - 1)) - 1)) : 0;
}

static void esp32c3_ledc_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned int size)
{
    Esp32C3LEDCState *s = ESP32C3_LEDC(opaque);
    int index,timer;
    //printf("ledc write %lx %lx\n",addr,value);
    switch (addr) {

    case A_LEDC_CONF_REG:
        s->conf_reg = value;
        break;    

    case A_LEDC_TIMER0_CONF_REG ... A_LEDC_TIMER3_CONF_REG:{
        int chn = (addr - A_LEDC_TIMER0_CONF_REG) / 0x8;
        s->timer_conf_reg[chn] = value;
        /* get duty resolution from timer config */
        int duty_res = s->timer_conf_reg[chn] & 15;
        if (duty_res != 0) {
            s->duty_res[chn] = duty_res;

            int div = (s->timer_conf_reg[chn]  & 0x003FFFF0) >> 4;
            int freq = 0; 

            switch (s->conf_reg & 0x00000003)
            {
            case 1: 
                freq = 80000000L; //APB_CLK
                break;
            case 2:
                freq = 17500000L; //RC_FAST_CLK
                break;
            case 3:
                freq = 40000000L; //XTAL_CLK
                break;    
            default:
                freq = 80000000L; //APB_CLK
                break;
            }
            s->freq[chn] = freq / ((div / 256.0) * (1 << duty_res));
            //printf("timer cfg %d %d %d %d\n",freq,div,duty_res,s->freq[chn]);

        }

        }break;

    case A_LEDC_CH0_CONF0_REG:
    case A_LEDC_CH1_CONF0_REG:
    case A_LEDC_CH2_CONF0_REG:
    case A_LEDC_CH3_CONF0_REG:
    case A_LEDC_CH4_CONF0_REG:
    case A_LEDC_CH5_CONF0_REG:
    case A_LEDC_CH6_CONF0_REG:
    case A_LEDC_CH7_CONF0_REG:
        index=(addr - A_LEDC_CH0_CONF0_REG) / 0x14;
        s->channel_conf0_reg[index] = value;
        timer=s->channel_conf0_reg[index] & 3;
        if(value & 4 && s->freq[timer] !=0) {
            uint32_t on_time, off_time;
            get_duty_time(s, index, &on_time, &off_time);
            led_set_intensity(&s->led[index],
                                esp32c3_ledc_get_percent(s, value, index));
            s->op_val[index] = 1;
            qemu_set_irq(s->func_irq,
                            (73 + index) * 2 + 1 + ((on_time / 1000) << 10));
            s->cycle[index] = 0;
            timer_mod_anticipate_ns(
                &s->led_timer[index],
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + on_time);
        }
        break;
    case A_LEDC_CH0_CONF1_REG:
    case A_LEDC_CH1_CONF1_REG:
    case A_LEDC_CH2_CONF1_REG:
    case A_LEDC_CH3_CONF1_REG:
    case A_LEDC_CH4_CONF1_REG:
    case A_LEDC_CH5_CONF1_REG:
    case A_LEDC_CH6_CONF1_REG:
    case A_LEDC_CH7_CONF1_REG:
        index=(addr - A_LEDC_CH0_CONF1_REG) / 0x14;
        s->channel_conf1_reg[index] = value;
        break;
    case A_LEDC_CH0_DUTY_REG:
    case A_LEDC_CH1_DUTY_REG:
    case A_LEDC_CH2_DUTY_REG:
    case A_LEDC_CH3_DUTY_REG:
    case A_LEDC_CH4_DUTY_REG:
    case A_LEDC_CH5_DUTY_REG:
    case A_LEDC_CH6_DUTY_REG:
    case A_LEDC_CH7_DUTY_REG:
        index = (addr - A_LEDC_CH0_DUTY_REG) / 0x14; 
        s->duty_reg[index] = value;
        s->duty_init_reg[index] = value;
        break;
    case A_LEDC_INT_ENA:
        s->int_en = value;
        if (s->int_en & s->int_raw)
            qemu_irq_raise(s->irq);
        else
            qemu_irq_lower(s->irq);
        break;
    case A_LEDC_INT_CLR:
        s->int_raw &= (~value);
        if ((s->int_raw & s->int_en) == 0) {
            qemu_irq_lower(s->irq);
        }
        break;
        
    }

}

static const MemoryRegionOps esp32c3_ledc_ops = {
        .read =  esp32c3_ledc_read,
        .write = esp32c3_ledc_write,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

static void esp32c3_ledc_realize(DeviceState *dev, Error **errp)
{
    Esp32C3LEDCState *s = ESP32C3_LEDC(dev);
    for (int i = 0; i < ESP32C3_LEDC_CHANNEL_CNT; i++) {
        qdev_realize(DEVICE(&s->led[i]), NULL, &error_fatal);
    }
}

static void esp32c3_ledc_init(Object *obj)
{
    Esp32C3LEDCState *s = ESP32C3_LEDC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &esp32c3_ledc_ops, s,
                          TYPE_ESP32C3_LEDC, ESP32C3_LEDC_REGS_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    for (int i = 0; i < ESP32C3_LEDC_CHANNEL_CNT; i++) {
        object_initialize_child(obj, g_strdup_printf("led%d", i + 1), &s->led[i], TYPE_LED);
        s->led[i].color = (char *)"blue";
    }
    current_state = s;
    for (int i = 0; i < ESP32C3_LEDC_CHANNEL_CNT; i++) {
        timer_init_ns(&s->led_timer[i], QEMU_CLOCK_VIRTUAL, ledc_timer_cb,
                      (void *)(long long)i);
    }
    qdev_init_gpio_out_named(DEVICE(s), &s->func_irq, "func_irq", 1);

}

static void esp32c3_ledc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = esp32c3_ledc_realize;
}

static const TypeInfo esp32c3_ledc_info = {
        .name = TYPE_ESP32C3_LEDC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Esp32C3LEDCState),
        .instance_init = esp32c3_ledc_init,
        .class_init = esp32c3_ledc_class_init
};

static void esp32c3_ledc_register_types(void)
{
    type_register_static(&esp32c3_ledc_info);
}

type_init(esp32c3_ledc_register_types)
