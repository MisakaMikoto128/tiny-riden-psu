/* session.c - 连接生命周期、乐观显示副本与动作入口。
   结果到达后推进 device 计划并立即补发下一帧，无固定节拍。 */
#include "session.h"

#define SESSION_SLAVE 1
#define SESSION_TIMEOUT_MS 600

const Preset PRESETS[PRESET_COUNT] = {
    { L"3.3V 1A",  3300, 10000, {0.055f, 0.647f, 0.635f, 1} },
    { L"5V 2A",    5000, 20000, {0.231f, 0.510f, 0.965f, 1} },
    { L"12V 2A",  12000, 20000, {0.545f, 0.361f, 0.965f, 1} },
    { L"19V 3A",  19000, 30000, {0.925f, 0.282f, 0.600f, 1} },
    { L"24V 2A",  24000, 20000, {0.851f, 0.467f, 0.024f, 1} },
    { L"48V 2A",  48000, 20000, {0.937f, 0.267f, 0.267f, 1} },
};

static int wlen(const wchar_t* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void wcopy(wchar_t* dst, const wchar_t* src)
{
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void wcat(wchar_t* dst, const wchar_t* src)
{
    int i = 0, j = 0;
    while (dst[i]) i++;
    while (src[j]) { dst[i++] = src[j++]; }
    dst[i] = 0;
}

static int wsub(const wchar_t* s, const wchar_t* sub)
{
    int i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; sub[j] && s[i + j] == sub[j]; j++) {}
        if (!sub[j]) return 1;
    }
    return 0;
}

static void msg_set(Session* s, const wchar_t* text)
{
    wcopy(s->msg, text);
}

static void status_msg(Session* s, DevStatus st)
{
    if (st == DEV_BUSY) msg_set(s, L"上一个操作还在进行");
    else if (st == DEV_TIMEOUT) msg_set(s, L"设备响应超时");
    else if (st == DEV_EXCEPTION) msg_set(s, L"设备返回 Modbus 异常码");
    else if (st == DEV_LINK) msg_set(s, L"链路未就绪或设备离线");
    else if (st == DEV_MISMATCH) msg_set(s, L"写后回读不一致，已中止");
    else if (st == DEV_OUTPUT_ON) msg_set(s, L"请先关闭输出再修改设定值");
    else if (st == DEV_OUT_OF_RANGE) msg_set(s, L"目标值超出量程或输入格式错误");
    else if (st == DEV_OVER_OVP) msg_set(s, L"目标电压高于 OVP 阈值");
}

static void fill_inputs(Session* s)
{
    wchar_t b[32];
    dev_fmt_voltage(s->dev.snap.v_set_mv, b);
    wcopy(s->vtext, b);
    s->vlen = wlen(b);
    dev_fmt_current(s->dev.snap.i_set_raw, s->dev.snap.range, b);
    wcopy(s->itext, b);
    s->ilen = wlen(b);
}

static void redraw(Session* s)
{
    InvalidateRect(s->hwnd, NULL, FALSE);
}

void session_init(Session* s, HWND hwnd)
{
    int i;
    s->hwnd = hwnd;
    s->portCount = serial_io_enum(s->ports, 16);
    for (i = 0; i < s->portCount; i++) {
        s->portItems[i].label = s->ports[i];
        s->portItems[i].value = i;
        if (wsub(s->ports[i], L"COM24")) s->portSel = i;
    }
    msg_set(s, L"选择端口后点连接");
}

void session_connect(Session* s)
{
    if (s->portCount == 0) { msg_set(s, L"没有找到串口设备"); return; }
    if (s->client) { mb_destroy(s->client); s->client = 0; }
    serial_io_init(&s->io, &s->sctx, s->ports[s->portSel], 115200);
    s->client = mb_create(&s->io, SESSION_SLAVE, SESSION_TIMEOUT_MS, 1);
    mb_start(s->client);
    dev_init(&s->dev, s->client);
    s->inputsReady = 0;
    s->pending = 0;
    s->optimistic = 0;
    s->display = s->dev.snap;
    dev_tick(&s->dev);
    msg_set(s, L"正在连接并读取状态 ...");
}

void session_start(Session* s)
{
    session_connect(s);
}

void session_disconnect(Session* s)
{
    if (!s->client) return;
    mb_destroy(s->client);
    s->client = 0;
    s->dev.snap.valid = 0;
    s->display = s->dev.snap;
    s->optimistic = 0;
    s->pending = 0;
    s->inputsReady = 0;
    msg_set(s, L"已断开连接");
}

HANDLE session_event(Session* s)
{
    return s->client ? mb_result_event(s->client) : 0;
}

int session_online(const Session* s)
{
    return s->client != 0 && mb_online(s->client);
}

void session_on_result(Session* s)
{
    DWORD now, elapsed;
    s->wasBusy = dev_busy(&s->dev);
    dev_on_result(&s->dev);
    if (s->optimistic) {
        int settled = (s->display.output == s->dev.snap.output &&
                       s->display.v_set_mv == s->dev.snap.v_set_mv &&
                       s->display.i_set_raw == s->dev.snap.i_set_raw);
        if (settled || s->dev.last != DEV_OK ||
            GetTickCount() - s->optimisticAt > 3000) {
            s->optimistic = 0;
            s->display = s->dev.snap;
            if (settled) msg_set(s, L"写入完成，回读值与目标一致");
        }
    } else {
        s->display = s->dev.snap;
    }
    if (s->dev.last != DEV_OK) {
        status_msg(s, s->dev.last);
        s->dev.last = DEV_OK;
    }
    if (s->wasBusy && !dev_busy(&s->dev)) s->refreshCount++;
    if (s->dev.snap.valid && !s->inputsReady) {
        fill_inputs(s);
        s->inputsReady = 1;
        msg_set(s, L"已自动连接，状态持续刷新");
    }
    dev_tick(&s->dev);
    now = GetTickCount();
    elapsed = now - s->rateAt;
    if (elapsed >= 1000) {
        s->rateX10 = (int)((unsigned long)s->refreshCount * 10000UL / elapsed);
        s->refreshCount = 0;
        s->rateAt = now;
    }
    redraw(s);
}

void session_preset(Session* s, int index)
{
    const Preset* p = &PRESETS[index];
    wchar_t b[32];
    int raw = p->micro_a * dev_i_scale(s->dev.snap.range) / 10000;
    dev_fmt_voltage(p->mv, b);
    wcopy(s->vtext, b);
    s->vlen = wlen(b);
    dev_fmt_current(raw, s->dev.snap.range, b);
    wcopy(s->itext, b);
    s->ilen = wlen(b);
    msg_set(s, L"已填入常用组合，请核对后点设定");
    redraw(s);
}

void session_confirm(Session* s)
{
    DevStatus st;
    int kind = s->pending;
    s->pending = 0;
    if (kind == 1) st = dev_set(&s->dev, s->pendV, s->pendI);
    else if (kind == 2 || kind == 3) st = dev_output(&s->dev, kind == 2);
    else { redraw(s); return; }
    if (st != DEV_OK) { status_msg(s, st); redraw(s); return; }
    s->display = s->dev.snap;
    if (kind == 1) {
        s->display.v_set_mv = s->pendV;
        s->display.i_set_raw = s->pendI;
    } else {
        s->display.output = (kind == 2);
    }
    s->optimistic = 1;
    s->optimisticAt = GetTickCount();
    msg_set(s, L"已提交，正在等待设备回读确认");
    redraw(s);
}

void session_set(Session* s)
{
    int mv = 0, raw = 0;
    if (!s->client || !s->dev.snap.valid) { msg_set(s, L"请先连接设备"); redraw(s); return; }
    if (!dev_parse_voltage(s->vtext, &mv) ||
        !dev_parse_current(s->itext, s->dev.snap.range, &raw)) {
        msg_set(s, L"输入格式错误：电压 3 位小数，电流按档位 3/4 位小数");
        redraw(s);
        return;
    }
    if (s->dev.snap.output) { msg_set(s, L"请先关闭输出再修改设定值"); redraw(s); return; }
    if (mv == s->dev.snap.v_set_mv && raw == s->dev.snap.i_set_raw) {
        msg_set(s, L"设定值与当前一致，无需写入");
        redraw(s);
        return;
    }
    s->pendV = mv;
    s->pendI = raw;
    s->pending = 1;
    if (!s->confirmEnabled) { session_confirm(s); return; }
    msg_set(s, L"请核对设定值后点确认");
    redraw(s);
}

void session_output(Session* s, int on)
{
    if (!s->client || !s->dev.snap.valid) { msg_set(s, L"请先连接设备"); redraw(s); return; }
    if (on) {
        if (s->dev.snap.output) { msg_set(s, L"输出已经开启"); redraw(s); return; }
        if (s->dev.snap.protect) { msg_set(s, L"保护已触发，请先在面板清除"); redraw(s); return; }
    } else if (!s->dev.snap.output) {
        msg_set(s, L"输出本来就是关闭状态");
        redraw(s);
        return;
    }
    s->pending = on ? 2 : 3;
    if (!s->confirmEnabled) { session_confirm(s); return; }
    msg_set(s, on ? L"请核对当前设定值后确认上电" : L"请确认关闭输出");
    redraw(s);
}

void session_cancel(Session* s)
{
    s->pending = 0;
    msg_set(s, L"已取消");
    redraw(s);
}
