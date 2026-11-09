/* device.c - RD6012P 动作计划执行器与量纲换算。
   计划是有序步骤数组；状态刷新、批量写、输出开关共用同一个执行器。
   所有数值保持整数原始单位，显示时才切分小数位。 */
#include "device.h"

#define DEV_SLAVE 1

static void apply_status(Device* d, const unsigned short* v)
{
    DevSnapshot* s = &d->snap;
    s->model_id = v[0];
    s->firmware = v[3];
    s->temp_c = v[4] ? -(int)v[5] : (int)v[5];
    s->v_set_mv = v[8];
    s->i_set_raw = v[9];
    s->v_out_mv = v[10];
    s->i_out_raw = v[11];
    s->v_in_cv = v[14];
    s->lock = v[15];
    s->protect = v[16];
    s->mode_cc = v[17];
    s->output = v[18];
    s->preset = v[19];
    s->range = v[20];
    s->valid = 1;
}

static int dev_submit_step(Device* d, const DevStep* st)
{
    MbRequest q = {0};
    int i;
    q.slave = DEV_SLAVE;
    q.func = st->func;
    q.addr = st->addr;
    q.count = st->count;
    q.nvalues = st->nvalues;
    for (i = 0; i < st->nvalues; i++) q.values[i] = st->values[i];
    return mb_submit(d->mb, &q, &d->seq);
}

static void dev_finish(Device* d)
{
    d->plan.count = 0;
    if (d->hasQueued) {
        d->plan = d->queued;
        d->plan.cursor = 0;
        d->hasQueued = 0;
        if (!dev_submit_step(d, &d->plan.steps[0])) d->plan.count = 0;
    }
}

static DevStatus dev_begin(Device* d, const DevPlan* p)
{
    if (dev_busy(d)) {
        if (d->hasQueued) return DEV_BUSY;
        d->queued = *p;
        d->hasQueued = 1;
        return DEV_OK;
    }
    d->plan = *p;
    d->plan.cursor = 0;
    if (!dev_submit_step(d, &d->plan.steps[0])) {
        d->plan.count = 0;
        return DEV_BUSY;
    }
    return DEV_OK;
}

void dev_init(Device* d, MbClient* mb)
{
    int i;
    unsigned char* p = (unsigned char*)d;
    for (i = 0; i < (int)sizeof(Device); i++) p[i] = 0;
    d->mb = mb;
}

int dev_busy(const Device* d)
{
    return d->plan.count > 0;
}

void dev_tick(Device* d)
{
    DevPlan p = {0};
    if (dev_busy(d) || d->hasQueued) return;
    p.count = 1;
    p.steps[0].func = 3;
    if (!d->snap.valid || d->protectRead) {
        p.steps[0].addr = 0;
        p.steps[0].count = 21;
    } else {
        p.steps[0].addr = 80;
        p.steps[0].count = 4;
    }
    dev_begin(d, &p);
}

int dev_on_result(Device* d)
{
    MbResult r;
    unsigned int seq;
    DevStep* st;
    int i;
    if (!mb_take(d->mb, &r, &seq)) return 0;
    if (seq != d->seq || !d->plan.count) return 1;
    st = &d->plan.steps[d->plan.cursor];

    if (r.status == MB_ERR_TIMEOUT) { d->last = DEV_TIMEOUT; dev_finish(d); return 1; }
    if (r.status == MB_ERR_EXCEPTION) { d->last = DEV_EXCEPTION; dev_finish(d); return 1; }
    if (r.status != MB_STAT_OK) { d->last = DEV_LINK; dev_finish(d); return 1; }
    if (r.func != st->func) { d->last = DEV_MISMATCH; dev_finish(d); return 1; }

    if (st->func == 3 && st->addr == 0) {
        if (r.count != 21) { d->last = DEV_MISMATCH; dev_finish(d); return 1; }
        apply_status(d, r.values);
        for (i = 0; i < d->plan.verifyCount; i++) {
            if (r.values[d->plan.verifyReg[i]] != d->plan.verifyVal[i]) {
                d->last = DEV_MISMATCH;
                dev_finish(d);
                return 1;
            }
        }
    } else if (st->func == 3 && st->addr == 80) {
        if (r.count != 4) { d->last = DEV_MISMATCH; dev_finish(d); return 1; }
        d->snap.ovp_mv = r.values[2];
        d->snap.ocp_raw = r.values[3];
        d->protectRead = 1;
    }

    d->plan.cursor++;
    if (d->plan.cursor >= d->plan.count) {
        d->last = DEV_OK;
        dev_finish(d);
    } else if (!dev_submit_step(d, &d->plan.steps[d->plan.cursor])) {
        d->plan.count = 0;
    }
    return 1;
}

DevStatus dev_status(Device* d)
{
    DevPlan p = {0};
    p.count = 1;
    p.steps[0].func = 3;
    p.steps[0].addr = 0;
    p.steps[0].count = 21;
    return dev_begin(d, &p);
}

DevStatus dev_protect(Device* d)
{
    DevPlan p = {0};
    p.count = 1;
    p.steps[0].func = 3;
    p.steps[0].addr = 80;
    p.steps[0].count = 4;
    return dev_begin(d, &p);
}

DevStatus dev_set(Device* d, int v_mv, int i_raw)
{
    DevPlan p = {0};
    DevStep* w;
    int max_i;
    if (!d->snap.valid) return DEV_LINK;
    if (d->snap.output) return DEV_OUTPUT_ON;
    if (v_mv < 0 || v_mv > DEV_V_MAX_MV) return DEV_OUT_OF_RANGE;
    if (d->protectRead && v_mv > d->snap.ovp_mv) return DEV_OVER_OVP;
    max_i = d->snap.range ? DEV_I_MAX_12A : DEV_I_MAX_6A;
    if (i_raw <= 0 || i_raw > max_i) return DEV_OUT_OF_RANGE;

    w = &p.steps[0];
    w->func = 0x10;
    if (v_mv != d->snap.v_set_mv && i_raw != d->snap.i_set_raw) {
        w->addr = 8;
        w->nvalues = 2;
        w->values[0] = (unsigned short)v_mv;
        w->values[1] = (unsigned short)i_raw;
        p.verifyReg[0] = 8; p.verifyVal[0] = (unsigned short)v_mv;
        p.verifyReg[1] = 9; p.verifyVal[1] = (unsigned short)i_raw;
        p.verifyCount = 2;
    } else if (v_mv != d->snap.v_set_mv) {
        w->addr = 8;
        w->nvalues = 1;
        w->values[0] = (unsigned short)v_mv;
        p.verifyReg[0] = 8; p.verifyVal[0] = (unsigned short)v_mv;
        p.verifyCount = 1;
    } else {
        w->addr = 9;
        w->nvalues = 1;
        w->values[0] = (unsigned short)i_raw;
        p.verifyReg[0] = 9; p.verifyVal[0] = (unsigned short)i_raw;
        p.verifyCount = 1;
    }
    p.steps[1].func = 3;
    p.steps[1].addr = 0;
    p.steps[1].count = 21;
    p.count = 2;
    return dev_begin(d, &p);
}

DevStatus dev_output(Device* d, int on)
{
    DevPlan p = {0};
    if (!d->snap.valid) return DEV_LINK;
    p.steps[0].func = 6;
    p.steps[0].addr = 18;
    p.steps[0].count = (unsigned short)on;
    p.steps[1].func = 3;
    p.steps[1].addr = 0;
    p.steps[1].count = 21;
    p.verifyReg[0] = 18;
    p.verifyVal[0] = (unsigned short)on;
    p.verifyCount = 1;
    p.count = 2;
    return dev_begin(d, &p);
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
