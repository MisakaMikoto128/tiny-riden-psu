/* app.c - RD6012P 上位机界面。
   立即模式绘制；状态快照由 device 层持有，本文件只读快照并发起动作。
   所有写操作走同一个动作队列与确认条，由 device 层统一执行护栏与回读。 */
#include <windows.h>
#include "ui.h"
#include "device.h"
#include "../transport/serial_io.h"

typedef struct {
    int reg, val;
} Action;

typedef struct {
    Ui* ui;
    HWND hwnd;
    int W, H, dpi;

    MbClient* client;
    MbIo io;
    SerialCtx sctx;
    DevSession dev;

    wchar_t ports[16][16];
    UiItem portItems[16];
    int portCount, portSel;

    wchar_t vtext[32], itext[32];
    int vlen, ilen;
    int inputsReady;

    int pending;
    int pendV, pendI;
    Action queue[4];
    int qCount, qIndex;

    int confirmEnabled;
    DevSnapshot display;
    int optimistic;
    DWORD optimisticAt;
    int wasBusy, refreshCount, rateX10;
    DWORD rateAt;
    wchar_t msg[192];
} App;

static App g;

static const Col COL_BG     = {0.949f, 0.961f, 0.976f, 1};
static const Col COL_PANEL  = {1.000f, 1.000f, 1.000f, 1};
static const Col COL_LINE   = {0.867f, 0.886f, 0.910f, 1};
static const Col COL_INK    = {0.106f, 0.129f, 0.165f, 1};
static const Col COL_DIM    = {0.416f, 0.451f, 0.502f, 1};
static const Col COL_WHITE  = {1.000f, 1.000f, 1.000f, 1};
static const Col COL_OK     = {0.063f, 0.600f, 0.420f, 1};
static const Col COL_WARN   = {0.870f, 0.220f, 0.220f, 1};
static const Col COL_ASK_BG = {1.000f, 0.973f, 0.878f, 1};
static const Col COL_DARK   = {0.290f, 0.170f, 0.020f, 1};

static const Col BT_CONNECT = {0.184f, 0.435f, 0.922f, 1};
static const Col BT_DROP    = {0.545f, 0.361f, 0.965f, 1};
static const Col BT_SET     = {0.961f, 0.620f, 0.043f, 1};
static const Col BT_CONFIRM = {0.925f, 0.282f, 0.600f, 1};
static const Col BT_CANCEL  = {0.392f, 0.455f, 0.545f, 1};
static const Col BT_ON      = {0.937f, 0.267f, 0.267f, 1};
static const Col BT_OFF     = {0.063f, 0.725f, 0.506f, 1};

static Rect R(int x, int y, int w, int h)
{
    Rect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

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

static void fmt_int(int v, wchar_t* out)
{
    wchar_t rev[16];
    int n = 0, k = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) rev[n++] = L'0';
    while (v > 0) { rev[n++] = (wchar_t)(L'0' + v % 10); v /= 10; }
    if (neg) out[k++] = L'-';
    while (n > 0) out[k++] = rev[--n];
    out[k] = 0;
}

static void msg_set(App* a, const wchar_t* s)
{
    wcopy(a->msg, s);
}

static void status_msg(App* a, DevStatus st)
{
    if (st == DEV_BUSY) msg_set(a, L"上一个操作还在进行");
    else if (st == DEV_TIMEOUT) msg_set(a, L"设备响应超时，已重试 3 次");
    else if (st == DEV_EXCEPTION) msg_set(a, L"设备返回 Modbus 异常码");
    else if (st == DEV_LINK) msg_set(a, L"链路未就绪或设备离线");
    else if (st == DEV_MISMATCH) msg_set(a, L"写后回读不一致，已中止");
    else if (st == DEV_OUTPUT_ON) msg_set(a, L"请先关闭输出再修改设定值");
    else if (st == DEV_OUT_OF_RANGE) msg_set(a, L"目标值超出量程或输入格式错误");
    else if (st == DEV_OVER_OVP) msg_set(a, L"目标电压高于 OVP 阈值");
}

static int sys_dpi(void)
{
    HDC dc = GetDC(NULL);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return d < 96 ? 96 : d;
}

static int L96(int v)
{
    return MulDiv(v, 96, g.dpi);
}

static void queue_reset(App* a)
{
    a->qCount = 0;
    a->qIndex = 0;
}

static void queue_add(App* a, int reg, int val)
{
    if (a->qCount < 4) {
        a->queue[a->qCount].reg = reg;
        a->queue[a->qCount].val = val;
        a->qCount++;
    }
}

static void queue_pump(App* a)
{
    DevStatus st;
    if (!a->client || a->qIndex >= a->qCount || dev_busy(&a->dev)) return;
    st = dev_write(&a->dev, a->queue[a->qIndex].reg, a->queue[a->qIndex].val);
    a->qIndex++;
    if (st != DEV_OK) {
        queue_reset(a);
        status_msg(a, st);
        a->optimistic = 0;
        a->display = a->dev.snap;
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }
    if (a->qIndex >= a->qCount) queue_reset(a);
}

static void fill_inputs(App* a)
{
    wchar_t b[32];
    dev_fmt_voltage(a->dev.snap.v_set_mv, b);
    wcopy(a->vtext, b);
    a->vlen = wlen(b);
    dev_fmt_current(a->dev.snap.i_set_raw, a->dev.snap.range, b);
    wcopy(a->itext, b);
    a->ilen = wlen(b);
}

static void connect_port(App* a)
{
    if (a->portCount == 0) { msg_set(a, L"没有找到串口设备"); return; }
    if (a->client) { mb_destroy(a->client); a->client = 0; }
    serial_io_init(&a->io, &a->sctx, a->ports[a->portSel], 115200);
    a->client = mb_create(&a->io, DEV_SLAVE, 600, 1);
    mb_start(a->client);
    dev_init(&a->dev, a->client);
    a->inputsReady = 0;
    a->pending = 0;
    a->optimistic = 0;
    queue_reset(a);
    dev_refresh(&a->dev);
    msg_set(a, L"正在连接并读取状态 ...");
}

static void disconnect_port(App* a)
{
    if (!a->client) return;
    mb_destroy(a->client);
    a->client = 0;
    a->dev.snap.valid = 0;
    a->display = a->dev.snap;
    a->optimistic = 0;
    a->pending = 0;
    a->inputsReady = 0;
    queue_reset(a);
    msg_set(a, L"已断开连接");
}

typedef struct {
    const wchar_t* label;
    int mv;
    int micro_a;    /* 0.0001 A，与电流档位无关 */
    Col color;
} Preset;

static const Preset PRESETS[] = {
    { L"3.3V 1A",  3300, 10000, {0.055f, 0.647f, 0.635f, 1} },
    { L"5V 2A",    5000, 20000, {0.231f, 0.510f, 0.965f, 1} },
    { L"12V 2A",  12000, 20000, {0.545f, 0.361f, 0.965f, 1} },
    { L"19V 3A",  19000, 30000, {0.925f, 0.282f, 0.600f, 1} },
    { L"24V 2A",  24000, 20000, {0.851f, 0.467f, 0.024f, 1} },
    { L"48V 2A",  48000, 20000, {0.937f, 0.267f, 0.267f, 1} },
};
#define PRESET_COUNT ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

static void preset_fill(App* a, const Preset* p)
{
    wchar_t b[32];
    int raw = p->micro_a * dev_i_scale(a->dev.snap.range) / 10000;
    dev_fmt_voltage(p->mv, b);
    wcopy(a->vtext, b);
    a->vlen = wlen(b);
    dev_fmt_current(raw, a->dev.snap.range, b);
    wcopy(a->itext, b);
    a->ilen = wlen(b);
    msg_set(a, L"已填入常用组合，请核对后点设定");
    InvalidateRect(a->hwnd, NULL, FALSE);
}

static void on_confirm(App* a)
{
    int kind = a->pending;
    queue_reset(a);
    if (kind == 1) {
        if (a->pendV != a->dev.snap.v_set_mv) queue_add(a, DEV_REG_V_SET, a->pendV);
        if (a->pendI != a->dev.snap.i_set_raw) queue_add(a, DEV_REG_I_SET, a->pendI);
    } else if (kind == 2) {
        queue_add(a, DEV_REG_OUTPUT, 1);
    } else if (kind == 3) {
        queue_add(a, DEV_REG_OUTPUT, 0);
    }
    a->pending = 0;
    if (a->qCount == 0) {
        msg_set(a, L"设定值与当前一致，无需写入");
        InvalidateRect(a->hwnd, NULL, FALSE);
        return;
    }
    a->display = a->dev.snap;
    if (kind == 1) {
        a->display.v_set_mv = a->pendV;
        a->display.i_set_raw = a->pendI;
    } else {
        a->display.output = (kind == 2);
    }
    a->optimistic = 1;
    a->optimisticAt = GetTickCount();
    msg_set(a, L"已提交，正在等待设备回读确认");
    queue_pump(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}

static void start_action(App* a, int kind)
{
    a->pending = kind;
    if (!a->confirmEnabled) { on_confirm(a); return; }
    if (kind == 1) msg_set(a, L"请核对设定值后点确认");
    else if (kind == 2) msg_set(a, L"请核对当前设定值后确认上电");
    else msg_set(a, L"请确认关闭输出");
}

static void confirm_text(App* a, wchar_t* out)
{
    wchar_t n[32];
    DevSnapshot* s = &a->dev.snap;
    out[0] = 0;
    if (a->pending == 1) {
        wcat(out, L"确认设为 ");
        dev_fmt_voltage(a->pendV, n); wcat(out, n); wcat(out, L" V / ");
        dev_fmt_current(a->pendI, s->range, n); wcat(out, n); wcat(out, L" A");
    } else if (a->pending == 2) {
        wcat(out, L"确认上电：当前设定 ");
        dev_fmt_voltage(s->v_set_mv, n); wcat(out, n); wcat(out, L" V / ");
        dev_fmt_current(s->i_set_raw, s->range, n); wcat(out, n); wcat(out, L" A");
    } else if (a->pending == 3) {
        wcat(out, L"确认关闭输出");
    }
}

static void big_cell(Ui* u, int x, int y, int w, const wchar_t* label,
                     const wchar_t* num, const wchar_t* unit, Col c)
{
    wchar_t line[48];
    ui_text(u, R(x, y, w, 18), label, COL_DIM, 12, 0, 0);
    wcopy(line, num);
    if (unit[0]) { wcat(line, L" "); wcat(line, unit); }
    ui_text(u, R(x, y + 20, w - 8, 34), line, c, 20, 0, 1);
}

static void info_cell(Ui* u, int x, int y, int w, const wchar_t* label,
                      const wchar_t* value, Col vc)
{
    ui_text(u, R(x, y, w * 45 / 100, 22), label, COL_DIM, 12, 0, 0);
    ui_text(u, R(x + w * 45 / 100, y, w * 55 / 100, 22), value, vc, 13, 2, 1);
}

static void draw(App* a)
{
    Ui* u = a->ui;
    int W = a->W, H = a->H;
    int colw, iw, x2, x3, bw, i, online;
    wchar_t b[64], c[64], ask[160];
    DevSnapshot* s = &a->display;
    online = a->client != 0 && mb_online(a->client);

    if (!ui_begin(u)) return;
    ui_fill(u, R(0, 0, W, H), COL_BG);

    ui_text(u, R(16, 10, 320, 30), L"RD6012P 上位机", COL_INK, 20, 0, 0);
    ui_text(u, R(16, 38, 420, 18), L"Modbus RTU · 115200 8N1 · 从站地址 1", COL_DIM, 12, 0, 0);
    if (s->valid) {
        fmt_int(s->model_id, b);
        wcopy(c, L"ID "); wcat(c, b); wcat(c, L"   FW ");
        dev_fmt_fixed((unsigned long)s->firmware, 2, b);
        wcat(c, b);
        ui_text(u, R(W - 260, 18, 244, 20), c, COL_DIM, 12, 2, 0);
    }

    ui_combo(u, 1, R(16, 58, 140, 30), a->portItems, a->portCount, &a->portSel);
    if (ui_button_col(u, 2, R(164, 58, 80, 30), L"连接", BT_CONNECT, COL_WHITE)) connect_port(a);
    if (ui_button_col(u, 3, R(252, 58, 80, 30), L"断开", BT_DROP, COL_WHITE)) disconnect_port(a);
    if (!a->client) wcopy(b, L"未连接");
    else if (online) { wcopy(b, L"已连接 "); wcat(b, a->ports[a->portSel]); wcat(b, L" · 115200"); }
    else wcopy(b, L"端口打开失败，正在重试");
    ui_text(u, R(344, 58, W - 360, 30), b, online ? COL_OK : COL_DIM, 13, 0, 0);

    ui_fill_round(u, R(12, 100, W - 24, 236), 8, COL_PANEL);
    ui_stroke(u, R(12, 100, W - 24, 236), 8, COL_LINE, 1.0f);
    colw = (W - 48) / 4;
    if (s->valid) {
        dev_fmt_voltage(s->v_out_mv, b);
        dev_fmt_current(s->i_out_raw, s->range, c);
        dev_fmt_fixed(dev_power_centiw(s), 2, ask);
    } else {
        wcopy(b, L"--"); wcopy(c, L"--"); wcopy(ask, L"--");
    }
    big_cell(u, 24, 112, colw, L"实测电压 (V)", b, L"V", COL_INK);
    big_cell(u, 24 + colw, 112, colw, L"实测电流 (A)", c, L"A", COL_INK);
    big_cell(u, 24 + colw * 2, 112, colw, L"输出功率 (W)", ask, L"W", COL_INK);
    big_cell(u, 24 + colw * 3, 112, colw, L"输出状态",
             s->valid ? (s->output ? L"开启" : L"关闭") : L"--",
             L"", s->output ? COL_WARN : COL_OK);

    ui_hline(u, 24, 182, W - 48, COL_LINE);
    colw = (W - 48) / 3;
    for (i = 0; i < 3; i++) {
        int x = 24 + i * colw;
        int y = 192;
        if (i == 0) {
            dev_fmt_voltage(s->v_set_mv, b); info_cell(u, x, y, colw - 12, L"设定电压 (V)", s->valid ? b : L"--", COL_INK);
            dev_fmt_voltage(s->ovp_mv, b); info_cell(u, x, y + 27, colw - 12, L"OVP (V)", s->valid ? b : L"--", COL_INK);
            dev_fmt_fixed((unsigned long)s->v_in_cv, 2, b); info_cell(u, x, y + 54, colw - 12, L"输入电压 (V)", s->valid ? b : L"--", COL_INK);
            info_cell(u, x, y + 81, colw - 12, L"输出使能", s->valid ? (s->output ? L"开启" : L"关闭") : L"--", s->output ? COL_WARN : COL_OK);
        } else if (i == 1) {
            dev_fmt_current(s->i_set_raw, s->range, b); info_cell(u, x, y, colw - 12, L"设定电流 (A)", s->valid ? b : L"--", COL_INK);
            dev_fmt_current(s->ocp_raw, s->range, b); info_cell(u, x, y + 27, colw - 12, L"OCP (A)", s->valid ? b : L"--", COL_INK);
            fmt_int(s->temp_c, b); wcat(b, L" ℃"); info_cell(u, x, y + 54, colw - 12, L"内部温度", s->valid ? b : L"--", COL_INK);
            info_cell(u, x, y + 81, colw - 12, L"按键锁", s->valid ? (s->lock ? L"锁定" : L"解锁") : L"--", COL_INK);
        } else {
            info_cell(u, x, y, colw - 12, L"工作模式", s->valid ? (s->mode_cc ? L"CC" : L"CV") : L"--", COL_INK);
            info_cell(u, x, y + 27, colw - 12, L"电流档位", s->valid ? (s->range ? L"12A 档" : L"6A 档") : L"--", COL_INK);
            info_cell(u, x, y + 54, colw - 12, L"保护状态",
                      s->valid ? (s->protect == 1 ? L"OVP" : (s->protect == 2 ? L"OCP" : L"正常")) : L"--",
                      s->protect ? COL_WARN : COL_OK);
            fmt_int(s->preset, b); info_cell(u, x, y + 81, colw - 12, L"预设组", s->valid ? b : L"--", COL_INK);
        }
    }

    ui_fill_round(u, R(12, 346, W - 24, 152), 8, COL_PANEL);
    ui_stroke(u, R(12, 346, W - 24, 152), 8, COL_LINE, 1.0f);
    ui_text(u, R(28, 352, 84, 22), L"设定", COL_INK, 15, 0, 0);
    ui_check(u, 50, R(120, 352, 150, 22), L"写入前确认", &a->confirmEnabled);
    dev_fmt_fixed(DEV_V_MAX_MV, 3, b);
    wcopy(c, L"范围 0–"); wcat(c, b); wcat(c, L" V / 0–");
    dev_fmt_current(s->range ? DEV_I_MAX_12A : DEV_I_MAX_6A, s->range, b);
    wcat(c, b); wcat(c, L" A  (");
    wcat(c, s->range ? L"12A 档" : L"6A 档"); wcat(c, L")");
    ui_text(u, R(W - 300, 352, 276, 22), c, COL_DIM, 12, 2, 0);
    iw = (W - 48 - 100 - 48) / 2;
    x2 = 28 + iw + 16;
    x3 = 28 + iw * 2 + 32;
    ui_text(u, R(28, 376, iw, 18), L"设定电压 (V)", COL_DIM, 12, 0, 0);
    ui_text(u, R(x2, 376, iw, 18), L"设定电流 (A)", COL_DIM, 12, 0, 0);
    ui_edit(u, 10, R(28, 396, iw, 30), a->vtext, 32, &a->vlen, 1, 0);
    ui_edit(u, 11, R(x2, 396, iw, 30), a->itext, 32, &a->ilen, 1, 0);
    if (ui_button_col(u, 13, R(x3, 396, 100, 30), L"设定", BT_SET, COL_DARK)) {
        int mv = 0, raw = 0;
        if (!a->client || !s->valid) msg_set(a, L"请先连接设备");
        else if (!dev_parse_voltage(a->vtext, &mv) ||
                 !dev_parse_current(a->itext, s->range, &raw)) {
            msg_set(a, L"输入格式错误：电压 3 位小数，电流按档位 3/4 位小数");
        } else if (s->output) {
            msg_set(a, L"请先关闭输出再修改设定值");
        } else {
            a->pendV = mv; a->pendI = raw;
            start_action(a, 1);
        }
    }
    if (a->pending) {
        confirm_text(a, ask);
        ui_fill_round(u, R(24, 430, W - 48, 48), 6, COL_ASK_BG);
        ui_text(u, R(34, 434, W - 262, 32), ask, COL_INK, 13, 0, 0);
        if (ui_button_col(u, 31, R(W - 216, 438, 96, 28), L"确认", BT_CONFIRM, COL_WHITE)) on_confirm(a);
        if (ui_button_col(u, 32, R(W - 112, 438, 88, 28), L"取消", BT_CANCEL, COL_WHITE)) {
            a->pending = 0;
            msg_set(a, L"已取消");
            InvalidateRect(a->hwnd, NULL, FALSE);
        }
    } else {
        int pw = (W - 48 - 40) / PRESET_COUNT;
        ui_text(u, R(28, 430, W - 56, 18),
                a->confirmEnabled ? L"常用组合：点击填入，仍需点设定并确认"
                                  : L"常用组合：点击填入，点设定即写入",
                COL_DIM, 12, 0, 0);
        for (i = 0; i < PRESET_COUNT; i++) {
            if (ui_button_col(u, 40 + i, R(24 + i * (pw + 8), 450, pw, 28),
                              PRESETS[i].label, PRESETS[i].color, COL_WHITE))
                preset_fill(a, &PRESETS[i]);
        }
    }

    ui_fill_round(u, R(12, 506, W - 24, 62), 8, COL_PANEL);
    ui_stroke(u, R(12, 506, W - 24, 62), 8, COL_LINE, 1.0f);
    bw = (W - 48 - 8) / 2;
    if (ui_button_col(u, 20, R(24, 516, bw, 42), L"开输出", BT_ON, COL_WHITE)) {
        if (!a->client || !s->valid) msg_set(a, L"请先连接设备");
        else if (s->output) msg_set(a, L"输出已经开启");
        else if (s->protect) msg_set(a, L"保护已触发，请先在面板清除");
        else start_action(a, 2);
    }
    if (ui_button_col(u, 21, R(24 + bw + 8, 516, bw, 42), L"关输出", BT_OFF, COL_WHITE)) {
        if (!a->client || !s->valid) msg_set(a, L"请先连接设备");
        else if (!s->output) msg_set(a, L"输出本来就是关闭状态");
        else start_action(a, 3);
    }

    ui_hline(u, 0, H - 62, W, COL_LINE);
    ui_fill(u, R(0, H - 61, W, 61), COL_PANEL);
    ui_text(u, R(16, H - 52, W - 32, 22), a->msg, COL_INK, 13, 0, 0);
    dev_fmt_fixed((unsigned long)a->rateX10, 1, b);
    wcopy(c, L"高速轮询 "); wcat(c, b);
    wcat(c, L" 次/秒 · 写入后回读校验 · 校准区不可达");
    ui_text(u, R(16, H - 28, W - 32, 18), c, COL_DIM, 12, 0, 0);

    ui_end(u);
}

static void on_result(App* a)
{
    DWORD now, elapsed;
    a->wasBusy = dev_busy(&a->dev);
    dev_poll(&a->dev);
    if (a->optimistic) {
        int settled = (a->display.output == a->dev.snap.output &&
                       a->display.v_set_mv == a->dev.snap.v_set_mv &&
                       a->display.i_set_raw == a->dev.snap.i_set_raw);
        if (settled || a->dev.last != DEV_OK ||
            GetTickCount() - a->optimisticAt > 3000) {
            a->optimistic = 0;
            a->display = a->dev.snap;
            if (settled) msg_set(a, L"写入完成，回读值与目标一致");
        }
    } else {
        a->display = a->dev.snap;
    }
    if (a->dev.last != DEV_OK) {
        status_msg(a, a->dev.last);
        a->dev.last = DEV_OK;
    }
    if (a->wasBusy && !dev_busy(&a->dev)) a->refreshCount++;
    queue_pump(a);
    if (a->dev.snap.valid && !a->inputsReady) {
        fill_inputs(a);
        a->inputsReady = 1;
        msg_set(a, L"已自动连接，状态持续刷新");
    }
    if (!dev_busy(&a->dev) && a->qCount == 0) dev_refresh(&a->dev);
    now = GetTickCount();
    elapsed = now - a->rateAt;
    if (elapsed >= 1000) {
        a->rateX10 = (int)((unsigned long)a->refreshCount * 10000UL / elapsed);
        a->refreshCount = 0;
        a->rateAt = now;
    }
    InvalidateRect(a->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    App* a = &g;
    switch (m) {
    case WM_CREATE:
        a->hwnd = h;
        a->ui = ui_create(h);
        return 0;

    case WM_SIZE:
        a->dpi = sys_dpi();
        a->W = L96(LOWORD(lp));
        a->H = L96(HIWORD(lp));
        ui_resize(a->ui, LOWORD(lp), HIWORD(lp));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = MulDiv(640, g.dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(640, g.dpi, 96);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        draw(a);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        ui_mouse_move(a->ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(h);
        ui_mouse_down(a->ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        ui_mouse_up(a->ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_CHAR:
        ui_char(a->ui, (wchar_t)wp);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_KEYDOWN:
        ui_key(a->ui, (int)wp);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        ui_destroy(a->ui);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WinMainCRTStartup(void)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT rc;
    int i;
    unsigned char* p = (unsigned char*)&g;

    for (i = 0; i < (int)sizeof(g); i++) p[i] = 0;

    SetProcessDPIAware();
    g.dpi = sys_dpi();

    g.portCount = serial_io_enum(g.ports, 16);
    for (i = 0; i < g.portCount; i++) {
        g.portItems[i].label = g.ports[i];
        g.portItems[i].value = i;
        if (wsub(g.ports[i], L"COM24")) g.portSel = i;
    }
    msg_set(&g, L"选择端口后点连接");

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hIcon = NULL;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"TinyApp";
    wc.hIconSm = NULL;
    RegisterClassExW(&wc);

    rc.left = 0; rc.top = 0;
    rc.right = MulDiv(780, g.dpi, 96);
    rc.bottom = MulDiv(640, g.dpi, 96);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g.hwnd = CreateWindowExW(0, L"TinyApp", L"RD6012P 上位机", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g.hwnd) return 0;
    ShowWindow(g.hwnd, SW_SHOW);
    connect_port(&g);

    for (;;) {
        HANDLE ev = g.client ? mb_result_event(g.client) : 0;
        DWORD w = ev ? MsgWaitForMultipleObjects(1, &ev, FALSE, INFINITE, QS_ALLINPUT)
                     : MsgWaitForMultipleObjects(0, NULL, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_FAILED) continue;
        if (ev && w == WAIT_OBJECT_0) { on_result(&g); continue; }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    if (g.client) mb_destroy(g.client);
    ExitProcess(0);
}
