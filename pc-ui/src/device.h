/* device.h - RD6012P 寄存器换算与动作计划执行器。
   真实快照与当前计划由本层持有，调用方只读快照、只提交计划。
   一个计划内步骤严格串行，同一时刻只有一个未完成请求。 */
#ifndef DEVICE_H
#define DEVICE_H

#include "../transport/modbus_rtu.h"

#define DEV_V_MAX_MV   60000
#define DEV_I_MAX_6A   62000
#define DEV_I_MAX_12A  12000
#define DEV_MAX_STEPS  3
#define DEV_MAX_VALUES 2

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
    unsigned char func;
    unsigned short addr;
    unsigned short count;
    unsigned short values[DEV_MAX_VALUES];
    unsigned char nvalues;
} DevStep;

typedef struct {
    DevStep steps[DEV_MAX_STEPS];
    int count;
    int cursor;
    unsigned char verifyCount;
    unsigned short verifyReg[DEV_MAX_VALUES];
    unsigned short verifyVal[DEV_MAX_VALUES];
} DevPlan;

typedef struct {
    MbClient* mb;
    DevSnapshot snap;
    DevPlan plan;
    DevPlan queued;
    int hasQueued;
    int protectRead;
    unsigned int seq;
    DevStatus last;
} Device;

void dev_init(Device* d, MbClient* mb);
int  dev_busy(const Device* d);
void dev_tick(Device* d);
int  dev_on_result(Device* d);
DevStatus dev_status(Device* d);
DevStatus dev_protect(Device* d);
DevStatus dev_set(Device* d, int v_mv, int i_raw);
DevStatus dev_output(Device* d, int on);

int  dev_i_scale(int range);
unsigned long dev_power_centiw(const DevSnapshot* s);
void dev_fmt_fixed(unsigned long v, int decimals, wchar_t* out);
void dev_fmt_voltage(int mv, wchar_t* out);
void dev_fmt_current(int raw, int range, wchar_t* out);
int  dev_parse_voltage(const wchar_t* text, int* mv);
int  dev_parse_current(const wchar_t* text, int range, int* raw);

#endif
