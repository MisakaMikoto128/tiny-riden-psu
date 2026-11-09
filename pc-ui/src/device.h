/* device.h - RD6012P 寄存器映射、量纲换算与受护栏的写操作。
   状态快照与事务阶段由本层持有，UI 只读快照并发起动作。
   允许写入的寄存器只有设定电压、设定电流、输出使能。 */
#ifndef DEVICE_H
#define DEVICE_H

#include "../transport/modbus_rtu.h"

#define DEV_SLAVE 1
#define DEV_REG_V_SET 8
#define DEV_REG_I_SET 9
#define DEV_REG_OUTPUT 18
#define DEV_REG_M0_OVP 82

#define DEV_V_MAX_MV   60000
#define DEV_I_MAX_6A   62000
#define DEV_I_MAX_12A  12000

typedef enum {
    DEV_OK = 0,
    DEV_BUSY,
    DEV_TIMEOUT,
    DEV_EXCEPTION,
    DEV_LINK,
    DEV_MISMATCH,
    DEV_OUTPUT_ON,
    DEV_OUT_OF_RANGE,
    DEV_OVER_OVP
} DevStatus;

typedef struct {
    int valid;
    int model_id, firmware;
    int temp_c, protect, mode_cc, output, lock, range, preset;
    int v_set_mv, v_out_mv, v_in_cv;
    int i_set_raw, i_out_raw;
    int ovp_mv, ocp_raw;
} DevSnapshot;

typedef struct {
    MbClient* mb;
    DevSnapshot snap;
    int stage;
    int protectRead;
    unsigned int seq;
    unsigned short wreg, wval;
    DevStatus last;
} DevSession;

void dev_init(DevSession* s, MbClient* mb);
int  dev_busy(const DevSession* s);
void dev_poll(DevSession* s);
DevStatus dev_refresh(DevSession* s);
DevStatus dev_write(DevSession* s, int reg, int value);

int  dev_i_scale(int range);
unsigned long dev_power_centiw(const DevSnapshot* s);
void dev_fmt_fixed(unsigned long v, int decimals, wchar_t* out);
void dev_fmt_voltage(int mv, wchar_t* out);
void dev_fmt_current(int raw, int range, wchar_t* out);
int  dev_parse_voltage(const wchar_t* text, int* mv);
int  dev_parse_current(const wchar_t* text, int range, int* raw);

#endif
