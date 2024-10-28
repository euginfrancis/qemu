#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"

#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"
#include "ui/input.h"
#include <math.h>


struct MPU6050State {
    I2CSlave i2c;
    QemuConsole *con;
    uint32_t *data;
    int16_t accel_data[3];  // Accelerometer data (x, y, z)
    int16_t gyro_data[3];   // Gyroscope data (x, y, z)
    uint8_t regs[128];      // Register map
    uint8_t selected_reg;   // Register currently selected by the I2C transaction
    int pitch, yaw;         // Rotation state
    int delta_pitch, delta_yaw;
    bool redraw;
};

#define TYPE_MPU6050 "mpu6050"
OBJECT_DECLARE_SIMPLE_TYPE(MPU6050State, MPU6050)

// Draw a simple representation of the MPU6050 (or system it's attached to)
static void mpu6050_draw(MPU6050State *s)
{
    QemuConsole *con = s->con;
    DisplaySurface *surface = qemu_console_surface(con);

    // Clear screen
  //  pixman_fill(qemu_console_surface(con), 0, 0, surface_width(surface), surface_height(surface), 0xFFFFFF);

    // Draw a simple representation of the board (as a rectangle)
    int cx = surface_width(surface) / 2;
    int cy = surface_height(surface) / 2;
    int size = 100;
    int offset_x = size * sin(s->yaw * M_PI / 180);
    int offset_y = size * cos(s->pitch * M_PI / 180);



    s->data[(cy+offset_y)*surface_width(surface)+cx+offset_x]=0xffffffff;

    // Draw the board (red rectangle)
  //  pixman_fill(surface, cx - offset_x, cy - offset_y, size, size, 0xFF0000); // Red rectangle
  //  dpy_gfx_update(con,0,0,);
//    qemu_console_refresh(con);
}

// Update the rotation and accelerometer/gyroscope data
static void mpu6050_update_rotation(MPU6050State *s)
{


    // Limit the pitch and yaw
    if (s->pitch > 360) s->pitch -= 360;
    if (s->yaw > 360) s->yaw -= 360;

    // Update accelerometer and gyroscope data
    s->accel_data[0] = (int16_t)(sin(s->pitch * M_PI / 180) * 16384);
    s->accel_data[1] = (int16_t)(sin(s->yaw * M_PI / 180) * 16384);
    s->accel_data[2] = (int16_t)(cos(s->pitch * M_PI / 180) * 16384);

    s->gyro_data[0] = s->delta_pitch * 131;  // Simulate gyroscope pitch
    s->gyro_data[1] = s->delta_yaw * 131;    // Simulate gyroscope yaw
    s->gyro_data[2] = 0;                  // No roll in this simple simulation

    // Redraw the MPU6050 board representation
    mpu6050_draw(s);
}

// Handle mouse movement events for rotating the device
static void mpu6050_mouse_event(DeviceState *dev, QemuConsole *con, InputEvent *evt)
{
    MPU6050State *s = MPU6050(dev);
    printf("Event %d %x\n",evt->type,evt->u.abs.data->axis);
    if (evt->type == INPUT_EVENT_KIND_ABS) {
            InputMoveEvent *move = evt->u.abs.data;
            if (move->axis == 0) {
                s->delta_pitch=s->pitch-move->value;
                s->pitch = move->value;
            }
            if (move->axis == 1) {
                s->delta_yaw=s->yaw-move->value;
                s->yaw = move->value;
            }
            mpu6050_update_rotation(s);
        
    }
}


// I2C send: register write operation (single byte)
static int mpu6050_i2c_send(I2CSlave *i2c, uint8_t data)
{
    MPU6050State *s = (MPU6050State *)i2c;

    
    // If this is the first byte, it's the register address
    if (s->selected_reg == 0) {
        s->selected_reg = data;
    } else {
        // Subsequent bytes are written to the selected register
        s->regs[s->selected_reg++] = data;

    }
 //   printf("send %x\n",data);
    return 0;
}

// I2C recv: register read operation (single byte)
static uint8_t mpu6050_i2c_recv(I2CSlave *i2c)
{
    MPU6050State *s = (MPU6050State *)i2c;
    uint8_t data = 0;
    switch (s->selected_reg) {
        case 0x3B: // ACCEL_XOUT_H
            data = (s->accel_data[0] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x3C: // ACCEL_XOUT_L
            data = s->accel_data[0] & 0xFF;
            s->selected_reg++;
            break;
        case 0x3D: // ACCEL_YOUT_H
            data = (s->accel_data[1] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x3E: // ACCEL_YOUT_L
            data = s->accel_data[1] & 0xFF;
            s->selected_reg++;
            break;
        case 0x3F: // ACCEL_ZOUT_H
            data = (s->accel_data[2] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x40: // ACCEL_ZOUT_L
            data = s->accel_data[2] & 0xFF;
            s->selected_reg++;
            break;
        case 0x43: // GYRO_XOUT_H
            data = (s->gyro_data[0] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x44: // GYRO_XOUT_L
            data = s->gyro_data[0] & 0xFF;
            s->selected_reg++;
            break;
        case 0x45: // GYRO_YOUT_H
            data = (s->gyro_data[1] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x46: // GYRO_YOUT_L
            data = s->gyro_data[1] & 0xFF;
            s->selected_reg++;
            break;
        case 0x47: // GYRO_ZOUT_H
            data = (s->gyro_data[2] >> 8) & 0xFF;
            s->selected_reg++;
            break;
        case 0x48: // GYRO_ZOUT_L
            data = s->gyro_data[2] & 0xFF;
            s->selected_reg++;
            break;
        default:
            data = s->regs[s->selected_reg++];
            break;
    }
 //   printf("recv %x\n",data);


    return data;
}
static void mpu6050_update_display(void *opaque) {
    MPU6050State *s = MPU6050(opaque);
    if (!s->redraw) return;
    s->redraw = 0;
    mpu6050_draw(s);
    dpy_gfx_update(s->con, 0, 0, 128, 128);
}

static void mpu6050_invalidate_display(void *opaque) {
    MPU6050State *s = MPU6050(opaque);
    s->redraw = 1;
}

static const GraphicHwOps mpu6050_ops = {
    .invalidate = mpu6050_invalidate_display,
    .gfx_update = mpu6050_update_display,
};
static QemuInputHandler event_handler = {
    .name  = "MPU6050 events",
    .mask  = INPUT_EVENT_MASK_KEY/* | INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS*/,
    .event = mpu6050_mouse_event,
};

static void mpu6050_reset(DeviceState *dev)
{
    printf("mpu6050 reset\n");
    QemuInputHandlerState *is=qemu_input_handler_register(dev, &event_handler);
    qemu_input_handler_bind(is,"mpu6050",0,0);

}
static int mpu6050_event(I2CSlave *i2c, enum i2c_event event)
{
    MPU6050State *s = MPU6050(i2c);
//    printf("mpu6050 event %x %x\n",s->pitch, event);
    s->selected_reg=0;
    return 0;
}
/*
static void mpu6050_mouse_event(void *opaque,
                int x, int y, int z, int buttons_state)
{
    MPU6050State *s = opaque;
    printf("Event %d %d %d %d\n",x, y, z,buttons_state );
    
    int p = s->pressure;

    if (buttons_state) {
        s->x = x;
        s->y = y;
    }
    s->pressure = !!buttons_state;


}

*/

static void vnc_dpy_switch(DisplayChangeListener *dcl,
                           DisplaySurface *surface) {
                        printf("dpy_switch\n");
}

static void vnc_mouse_set(DisplayChangeListener *dcl,
                          int x, int y, int visible)
{
    printf("mouse %d %d %d\n",x,y,visible);
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name             = "mpu6050",
 //   .dpy_refresh          = vnc_refresh,
 //   .dpy_gfx_update       = vnc_dpy_update,
    .dpy_gfx_switch       = vnc_dpy_switch,
 //   .dpy_gfx_check_format = qemu_pixman_check_format,
    .dpy_mouse_set        = vnc_mouse_set,
  //  .dpy_cursor_define    = vnc_dpy_cursor_define,
};

static  DisplayChangeListener dcl = {
    .ops=&dcl_ops
};

static void mpu6050_realize(DeviceState *dev, Error **errp) {
    printf("mpu6050_realize\n");
    I2CSlave *i2c = I2C_SLAVE(dev);
    MPU6050State *s = MPU6050(i2c);
    QemuInputHandlerState *is=qemu_input_handler_register(dev, &event_handler);
    s->con=graphic_console_init(dev, 0, &mpu6050_ops, s);
    qemu_console_resize(s->con,128, 128);
    s->data=surface_data(qemu_console_surface(s->con));
    qemu_input_handler_bind(is,"mpu6050",0,0);
    dcl.con=s->con;

    register_displaychangelistener(&dcl);
}
// MPU6050 class initialization function
static void mpu6050_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = mpu6050_reset;
    sc->event = mpu6050_event;
    sc->send = mpu6050_i2c_send;
    sc->recv = mpu6050_i2c_recv;
    dc->realize = mpu6050_realize;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    //set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

// Register the MPU6050 device type
static const TypeInfo mpu6050_info = {
    .name          = "mpu6050",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MPU6050State),
   // .instance_init = mpu6050_initfn,
    .class_init    = mpu6050_class_init,
};

static void mpu6050_register_types(void)
{
    type_register_static(&mpu6050_info);
}

type_init(mpu6050_register_types);
