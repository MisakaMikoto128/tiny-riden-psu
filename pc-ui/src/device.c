/* device.c - RD6012P 寄存器映射、量纲换算与受护栏的写操作。
   保护阈值只在连接后读一次，状态按单帧连续轮询；写操作回读校验。
   所有数值保持整数原始单位，显示时才切分小数位。 */
#include "device.h"

#define ST_IDLE    0
#define ST_PROTECT 1
#define ST_STATUS  2
#define ST_WRITE   3
#define ST_VERIFY  4

static int send_req(DevSession* s, int func, int addr, int count, int stage)
{
    MbRequest q;
    q.slave = DEV_SLAVE;
    q.func = (unsigned char)func;
    q.addr = (unsigned short)addr;
    q.count = (unsigned short)count;
    if (!mb_submit(s->mb, &q, &s->seq)) return 0;
    s->stage = stage;
    return 1;
}

static void apply_status(DevSession* s, const unsigned short* v)
{
    DevSnapshot* d = &s->snap;
    d->model_id = v[0];
    d->firmware = v[3];
    d->temp_c = v[4] ? -(int)v[5] : (int)v[5];
    d->v_set_mv = v[8];
    d->i_set_raw = v[9];
    d->v_out_mv = v[10];
    d->i_out_raw = v[11];
    d->v_in_cv = v[14];
    d->lock = v[15];
    d->protect = v[16];
    d->mode_cc = v[17];
    d->output = v[18];
    d->preset = v[19];
    d->range = v[20];
    d->valid = 1;
}

void dev_init(DevSession* s, MbClient* mb)
{
    int i;
    unsigned char* p = (unsigned char*)s;
    for (i = 0; i < (int)sizeof(DevSession); i++) p[i] = 0;
    s->mb = mb;
}

int dev_busy(const DevSession* s)
{
    return s->stage != ST_IDLE;
}

DevStatus dev_refresh(DevSession* s)
{
    if (s->stage != ST_IDLE) return DEV_BUSY;
    if (!s->snap.valid) {
        if (!send_req(s, 3, 0, 21, ST_STATUS)) return DEV_BUSY;
    } else if (!s->protectRead) {
        if (!send_req(s, 3, 80, 4, ST_PROTECT)) return DEV_BUSY;
    } else {
        if (!send_req(s, 3, 0, 21, ST_STATUS)) return DEV_BUSY;
    }
    return DEV_OK;
}

DevStatus dev_write(DevSession* s, int reg, int value)
{
    int max_i;
    if (s->stage != ST_IDLE) return DEV_BUSY;
    if (!s->snap.valid) return DEV_LINK;

    if (reg == DEV_REG_V_SET) {
        if (value < 0 || value > DEV_V_MAX_MV) return DEV_OUT_OF_RANGE;
        if (s->protectRead && value > s->snap.ovp_mv) return DEV_OVER_OVP;
        if (s->snap.output) return DEV_OUTPUT_ON;
    } else if (reg == DEV_REG_I_SET) {
        max_i = (s->snap.range == 1) ? DEV_I_MAX_12A : DEV_I_MAX_6A;
        if (value <= 0 || value > max_i) return DEV_OUT_OF_RANGE;
        if (s->snap.output) return DEV_OUTPUT_ON;
    } else if (reg == DEV_REG_OUTPUT) {
        if (value != 0 && value != 1) return DEV_OUT_OF_RANGE;
    } else {
        return DEV_OUT_OF_RANGE;
    }

    s->wreg = (unsigned short)reg;
    s->wval = (unsigned short)value;
    if (!send_req(s, 6, reg, value, ST_WRITE)) return DEV_BUSY;
    return DEV_OK;
}

void dev_poll(DevSession* s)
{
    MbResult r;
    unsigned int seq;
    int stage;
    if (!mb_take(s->mb, &r, &seq)) return;
    if (seq != s->seq) return;
    stage = s->stage;
    s->stage = ST_IDLE;

    if (r.status == MB_ERR_TIMEOUT) { s->last = DEV_TIMEOUT; return; }
    if (r.status == MB_ERR_EXCEPTION) { s->last = DEV_EXCEPTION; return; }
    if (r.status != MB_STAT_OK) { s->last = DEV_LINK; return; }

    if (stage == ST_PROTECT) {
        if (r.count != 4) { s->last = DEV_MISMATCH; return; }
        s->snap.ovp_mv = r.values[2];
        s->snap.ocp_raw = r.values[3];
        s->protectRead = 1;
        s->last = DEV_OK;
        return;
    }
    if (stage == ST_STATUS || stage == ST_VERIFY) {
        if (r.count != 21) { s->last = DEV_MISMATCH; return; }
        apply_status(s, r.values);
        if (stage == ST_VERIFY && r.values[s->wreg] != s->wval) {
            s->last = DEV_MISMATCH;
            return;
        }
        s->last = DEV_OK;
        return;
    }
    if (stage == ST_WRITE) {
        if (r.func != 6 || r.values[0] != s->wval) { s->last = DEV_MISMATCH; return; }
        if (!send_req(s, 3, 0, 21, ST_VERIFY)) s->last = DEV_BUSY;
    }
}

int dev_i_scale(int range)
{
    return range ? 1000 : 10000;
}

unsigned long dev_power_centiw(const DevSnapshot* s)
{
    unsigned long denom = (unsigned long)(10 * dev_i_scale(s->range));
    return (unsigned long)s->v_out_mv * (unsigned long)s->i_out_raw / denom;
}

void dev_fmt_fixed(unsigned long v, int decimals, wchar_t* out)
{
    wchar_t rev[24];
    unsigned long scale = 1, ip, fp;
    int i, n = 0, k = 0;
    for (i = 0; i < decimals; i++) scale *= 10;
    ip = v / scale;
    fp = v % scale;
    if (ip == 0) rev[n++] = L'0';
    while (ip > 0) { rev[n++] = (wchar_t)(L'0' + ip % 10); ip /= 10; }
    while (n > 0) out[k++] = rev[--n];
    if (decimals > 0) {
        out[k++] = L'.';
        n = 0;
        for (i = 0; i < decimals; i++) { rev[n++] = (wchar_t)(L'0' + fp % 10); fp /= 10; }
        while (n > 0) out[k++] = rev[--n];
    }
    out[k] = 0;
}

void dev_fmt_voltage(int mv, wchar_t* out)
{
    dev_fmt_fixed((unsigned long)mv, 3, out);
}

void dev_fmt_current(int raw, int range, wchar_t* out)
{
    dev_fmt_fixed((unsigned long)raw, range ? 3 : 4, out);
}

static int parse_fixed(const wchar_t* text, int decimals, int* out)
{
    unsigned long v = 0;
    int i = 0, d = 0, any = 0;
    while (text[i] >= L'0' && text[i] <= L'9') {
        v = v * 10 + (unsigned long)(text[i] - L'0');
        i++;
        any = 1;
    }
    if (text[i] == L'.') {
        i++;
        while (text[i] >= L'0' && text[i] <= L'9' && d < decimals) {
            v = v * 10 + (unsigned long)(text[i] - L'0');
            i++;
            d++;
            any = 1;
        }
        while (text[i] >= L'0' && text[i] <= L'9') i++;
    }
    while (d < decimals) { v *= 10; d++; }
    if (!any || text[i] != 0 || v > 0x7FFFFFFFUL) return 0;
    *out = (int)v;
    return 1;
}

int dev_parse_voltage(const wchar_t* text, int* mv)
{
    return parse_fixed(text, 3, mv);
}

int dev_parse_current(const wchar_t* text, int range, int* raw)
{
    return parse_fixed(text, range ? 3 : 4, raw);
}
