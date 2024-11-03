#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"

#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"
#include "ui/input.h"
#include <math.h>
#include "qemu/timer.h"
#include "mpu6050_image.h"


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
    int downx, downy;
    int down_pitch,down_yaw;
    bool mousepressed;
    QEMUTimer timer;
};

#define TYPE_MPU6050 "mpu6050"
OBJECT_DECLARE_SIMPLE_TYPE(MPU6050State, MPU6050)

#define WIDTH 200
#define HEIGHT 200
#define SQUARE_SIZE 128

// Convert degrees to radians
#define DEG_TO_RAD(angle) ((angle) * M_PI / 180.0)

// Struct for 3D points
typedef struct {
    float x, y, z;
} Vec3;

// Struct for 2D points
typedef struct {
    int x, y;
} Vec2;

// Rotate a 3D point by pitch and yaw
static Vec3 rotate_point(Vec3 point, float pitch, float yaw) {
    // Apply yaw rotation (rotation around the Y-axis)
    float cos_yaw = cos(DEG_TO_RAD(yaw));
    float sin_yaw = sin(DEG_TO_RAD(yaw));
    float x = cos_yaw * point.x - sin_yaw * point.z;
    float z = sin_yaw * point.x + cos_yaw * point.z;

    // Apply pitch rotation (rotation around the X-axis)
    float cos_pitch = cos(DEG_TO_RAD(pitch));
    float sin_pitch = sin(DEG_TO_RAD(pitch));
    float y = cos_pitch * point.y - sin_pitch * z;
    z = sin_pitch * point.y + cos_pitch * z;
    return (Vec3){x, y, z};
}
// Project a 3D point onto the 2D screen
static Vec2 project_point(Vec3 point) {
    // Simple perspective projection
    float fov = 2000.0;  // Field of view scaling factor
    return (Vec2){WIDTH / 2 + fov * point.x / (point.z + fov),
        HEIGHT / 2 - fov * point.y / (point.z + fov)};
}
// Calculate barycentric coordinates
inline static void calculate_barycentric(Vec2 p, Vec2 v0, Vec2 v1, Vec2 v2, float* u, float* v, float* w) {
    float denom = (v1.y - v2.y) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.y - v2.y);
    *u = ((v1.y - v2.y) * (p.x - v2.x) + (v2.x - v1.x) * (p.y - v2.y)) / denom;
    *v = ((v2.y - v0.y) * (p.x - v2.x) + (v0.x - v2.x) * (p.y - v2.y)) / denom;
    *w = 1.0f - *u - *v;
}
// Draw a filled 3D square with texture coordinates onto the 2D buffer
static void draw_filled_square_with_uv(uint32_t *buffer, float pitch, float yaw, uint32_t color) {
    memset(buffer, 0, WIDTH * HEIGHT * sizeof(uint32_t));  // Clear the buffer

    // Define the vertices of the square in 3D space (centered at origin)
    Vec3 vertices[4] = {
        {-SQUARE_SIZE / 2, -SQUARE_SIZE / 2, 0},
        { SQUARE_SIZE / 2, -SQUARE_SIZE / 2, 0},
        { SQUARE_SIZE / 2,  SQUARE_SIZE / 2, 0},
        {-SQUARE_SIZE / 2,  SQUARE_SIZE / 2, 0}
    };

    // Rotate and project each vertex
    Vec2 projected[4];
    for (int i = 0; i < 4; i++) {
        Vec3 rotated = rotate_point(vertices[i], pitch, yaw);
        projected[i] = project_point(rotated);
    }
 

    for (int y = 0; y < HEIGHT ; y++) {
        for (int x = 0; x < WIDTH; x++) {
            Vec2 p = {x,y};
            float u,v,w,final_u=-1,final_v=-1;
            // First triangle (0, 1, 2)
            calculate_barycentric(p, projected[0], projected[1], projected[2], &u, &v, &w);
            if (u >= -1 && v >= -1 && w >= -1) {
                final_u=v+w;
                final_v=w;
              //  in_triangle = 1;
            } else {
                // Second triangle (2, 3, 0)
                calculate_barycentric(p, projected[2], projected[3], projected[0], &u, &v, &w);
                if (u >= 0 && v >= 0 && w >= 0) {
                    final_u=u;
                    final_v=u+v;
                }
            }
            u=final_u;
            v=final_v;
            if(u>=0 && u<=1 && v>=0 && v<=1) {
                int offset=((int)(u*255.0)*256+(int)(v*255.0))*4;
                if(offset>256*256*4) offset=0;
                uint32_t pixel=*(uint32_t *)(mpu6050_image.pixel_data+offset);
                pixel=((pixel & 0xff0000)>>16) | ((pixel & 0xff)<<16) | (pixel & 0xff00ff00);
                if(pixel>>24==0xff)
                    buffer[y * WIDTH + x] = pixel;
                else buffer[y * WIDTH + x] = 0;
            }
        }
    }
    for(int i=0;i<4;i++) {
         buffer[projected[i].y * WIDTH + projected[i].x] = 0xff<<(i*8) | 0xfff00000;
    }
}

// Draw a simple representation of the MPU6050 (or system it's attached to)
static void mpu6050_draw(MPU6050State *s) {
    draw_filled_square_with_uv(s->data,s->pitch,s->yaw,-1);
    s->redraw=1;
}

static int randomnum(void) {
    int r=rand()%65535;
    r=sqrt(r);
    if(rand()%2) r=-r;
    return r;
}

// Update the rotation and accelerometer/gyroscope data
static void mpu6050_update_rotation(MPU6050State *s) {
    // Limit the pitch and yaw
    if (s->pitch > 360) s->pitch -= 360;
    if (s->yaw > 360) s->yaw -= 360;
    if (s->pitch < 0) s->pitch += 360;
    if (s->yaw <0 ) s->yaw += 360;
    // Update accelerometer and gyroscope data
    s->accel_data[0] = (int16_t)(sin(s->pitch * M_PI / 180) * 16384)+randomnum();
    s->accel_data[1] = (int16_t)(sin(s->yaw * M_PI / 180) * 16384)+randomnum();
    s->accel_data[2] = (int16_t)(cos(s->pitch * M_PI / 180) * 16384)+randomnum();

    s->gyro_data[0] = s->delta_pitch * 131;  // Simulate gyroscope pitch
    s->gyro_data[1] = s->delta_yaw * 131;    // Simulate gyroscope yaw
    s->gyro_data[2] = 0;                  // No roll in this simple simulation

    s->delta_pitch = 0;
    s->delta_yaw = 0;

    //printf("%d\n",s->accel_data[0]);
    // Redraw the MPU6050 board representation
    mpu6050_draw(s);
}
/*
static void timer_cb(void *v) {
    MPU6050State *s=(MPU6050State *)v;
    mpu6050_update_rotation(s);
    uint64_t now=qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod_ns(&s->timer, now + 100000000);
}
*/

// Handle mouse movement events for rotating the device
static void mpu6050_mouse_event(DeviceState *dev, QemuConsole *con, InputEvent *evt)
{
    MPU6050State *s = MPU6050(dev);
//    printf("Event %d %x\n",evt->type,evt->u.abs.data->axis);
    if (evt->type == INPUT_EVENT_KIND_BTN) {
        InputBtnEvent *btn=evt->u.btn.data;
        s->mousepressed=btn->down;
        if(btn->down) {
            s->down_pitch=s->pitch;
            s->down_yaw=s->yaw;
            s->downx=-1;
            s->downy=-1;
        }
    }
    if (evt->type == INPUT_EVENT_KIND_ABS && s->mousepressed) {

        InputMoveEvent *move = evt->u.abs.data;
        if (move->axis == 1) {
            s->delta_pitch=s->pitch-move->value;
            if(s->downx==-1) s->downx=move->value;
            s->pitch = s->down_pitch+(360*(s->downx-move->value))/32768;
        }
        if (move->axis == 0) {
            s->delta_yaw=s->yaw-move->value;
            if(s->downy==-1) s->downy=move->value;
            s->yaw =s->down_yaw+ (360*(s->downy-move->value))/32768;
        }
        printf("Event %d %d\n",s->pitch,s->yaw);
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
    //printf("send %x %x\n",data,s->selected_reg);
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
    //printf("recv %x %x\n",s->selected_reg-1,data);
    s->redraw=1;
    mpu6050_update_rotation(s);
    return data;
}
static void mpu6050_update_display(void *opaque) {
    MPU6050State *s = MPU6050(opaque);
    if (!s->redraw) return;
    s->redraw = 0;
    mpu6050_draw(s);
    dpy_gfx_update(s->con, 0, 0, 200, 200);
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
    .mask  = INPUT_EVENT_MASK_KEY | INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = mpu6050_mouse_event,
};

static void mpu6050_reset(DeviceState *dev)
{
    MPU6050State *s = MPU6050(dev);
//    printf("mpu6050 reset\n");
    s->selected_reg=0;
    s->pitch=0;
}
static int mpu6050_event(I2CSlave *i2c, enum i2c_event event)
{
    MPU6050State *s = MPU6050(i2c);
    if(event==I2C_START_SEND)
        s->selected_reg=0;
    return 0;
}

static void mpu6050_realize(DeviceState *dev, Error **errp) {
   // printf("mpu6050_realize\n");
    I2CSlave *i2c = I2C_SLAVE(dev);
    MPU6050State *s = MPU6050(i2c);
    DEVICE(s)->id=(char *)"mpu6050";
    QemuInputHandlerState *is=qemu_input_handler_register(DEVICE(s), &event_handler);
    s->con=graphic_console_init(dev, 0, &mpu6050_ops, s);
    qemu_console_resize(s->con,200, 200);
    s->data=surface_data(qemu_console_surface(s->con));
    qemu_input_handler_bind(is,DEVICE(s)->id,0,errp);
    s->redraw=1;
  //  int64_t now=qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
  //  timer_init_ns(&s->timer,QEMU_CLOCK_VIRTUAL, timer_cb,s);
  //  timer_mod_ns(&s->timer, now + 100000000);
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
}

// Register the MPU6050 device type
static const TypeInfo mpu6050_info = {
    .name          = "mpu6050",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MPU6050State),
    .class_init    = mpu6050_class_init,
};

static void mpu6050_register_types(void)
{
    type_register_static(&mpu6050_info);
}

type_init(mpu6050_register_types);
