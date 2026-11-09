/* app.c - RD6012P 上位机窗口：立即模式绘制、输入分发与消息循环。
   状态与动作全部由 session 层持有，本文件只读快照并调用动作入口。
   结果事件驱动重绘，不使用固定节拍定时器。 */
#include <windows.h>
#include "ui.h"
#include "session.h"

static Session g;
static Ui* g_ui;
static int g_W, g_H;
static int g_dpi;

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

static int sys_dpi(void)
{
    HDC dc = GetDC(NULL);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return d < 96 ? 96 : d;
}

static int L96(int v)
{
    return MulDiv(v, 96, g_dpi);
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

static void confirm_text(Session* a, wchar_t* out)
{
    wchar_t n[32];
    DevSnapshot* d = &a->dev.snap;
    out[0] = 0;
    if (a->pending == 1) {
        wcat(out, L"确认设为 ");
        dev_fmt_voltage(a->pendV, n); wcat(out, n); wcat(out, L" V / ");
        dev_fmt_current(a->pendI, d->range, n); wcat(out, n); wcat(out, L" A");
    } else if (a->pending == 2) {
        wcat(out, L"确认上电：当前设定 ");
        dev_fmt_voltage(d->v_set_mv, n); wcat(out, n); wcat(out, L" V / ");
        dev_fmt_current(d->i_set_raw, d->range, n); wcat(out, n); wcat(out, L" A");
    } else {
        wcat(out, L"确认关闭输出");
    }
}

static void draw(Session* a)
{
    Ui* u = g_ui;
    int W = g_W, H = g_H;
    int colw, iw, x2, x3, bw, i, online;
    wchar_t b[64], c[64], ask[160];
    DevSnapshot* d = &a->display;
    online = session_online(a);

    if (!ui_begin(u)) return;
    ui_fill(u, R(0, 0, W, H), COL_BG);

    ui_text(u, R(16, 10, 320, 30), L"RD6012P 上位机", COL_INK, 20, 0, 0);
    ui_text(u, R(16, 38, 420, 18), L"Modbus RTU · 115200 8N1 · 从站地址 1", COL_DIM, 12, 0, 0);
    if (d->valid) {
        fmt_int(d->model_id, b);
        wcopy(c, L"ID "); wcat(c, b); wcat(c, L"   FW ");
        dev_fmt_fixed((unsigned long)d->firmware, 2, b);
        wcat(c, b);
        ui_text(u, R(W - 260, 18, 244, 20), c, COL_DIM, 12, 2, 0);
    }

    ui_combo(u, 1, R(16, 58, 140, 30), a->portItems, a->portCount, &a->portSel);
    if (ui_button_col(u, 2, R(164, 58, 80, 30), L"连接", BT_CONNECT, COL_WHITE)) session_connect(a);
    if (ui_button_col(u, 3, R(252, 58, 80, 30), L"断开", BT_DROP, COL_WHITE)) session_disconnect(a);
    if (!a->client) wcopy(b, L"未连接");
    else if (online) { wcopy(b, L"已连接 "); wcat(b, a->ports[a->portSel]); wcat(b, L" · 115200"); }
    else wcopy(b, L"端口打开失败，正在重试");
    ui_text(u, R(344, 58, W - 360, 30), b, online ? COL_OK : COL_DIM, 13, 0, 0);

    ui_fill_round(u, R(12, 100, W - 24, 236), 8, COL_PANEL);
    ui_stroke(u, R(12, 100, W - 24, 236), 8, COL_LINE, 1.0f);
    colw = (W - 48) / 4;
    if (d->valid) {
        dev_fmt_voltage(d->v_out_mv, b);
        dev_fmt_current(d->i_out_raw, d->range, c);
        dev_fmt_fixed(dev_power_centiw(d), 2, ask);
    } else {
        wcopy(b, L"--"); wcopy(c, L"--"); wcopy(ask, L"--");
    }
    big_cell(u, 24, 112, colw, L"实测电压 (V)", b, L"V", COL_INK);
    big_cell(u, 24 + colw, 112, colw, L"实测电流 (A)", c, L"A", COL_INK);
    big_cell(u, 24 + colw * 2, 112, colw, L"输出功率 (W)", ask, L"W", COL_INK);
    big_cell(u, 24 + colw * 3, 112, colw, L"输出状态",
             d->valid ? (d->output ? L"开启" : L"关闭") : L"--",
             L"", d->output ? COL_WARN : COL_OK);

    ui_hline(u, 24, 182, W - 48, COL_LINE);
    colw = (W - 48) / 3;
    for (i = 0; i < 3; i++) {
        int x = 24 + i * colw;
        int y = 192;
        if (i == 0) {
            dev_fmt_voltage(d->v_set_mv, b); info_cell(u, x, y, colw - 12, L"设定电压 (V)", d->valid ? b : L"--", COL_INK);
            dev_fmt_voltage(d->ovp_mv, b); info_cell(u, x, y + 27, colw - 12, L"OVP (V)", d->valid ? b : L"--", COL_INK);
            dev_fmt_fixed((unsigned long)d->v_in_cv, 2, b); info_cell(u, x, y + 54, colw - 12, L"输入电压 (V)", d->valid ? b : L"--", COL_INK);
            info_cell(u, x, y + 81, colw - 12, L"输出使能", d->valid ? (d->output ? L"开启" : L"关闭") : L"--", d->output ? COL_WARN : COL_OK);
        } else if (i == 1) {
            dev_fmt_current(d->i_set_raw, d->range, b); info_cell(u, x, y, colw - 12, L"设定电流 (A)", d->valid ? b : L"--", COL_INK);
            dev_fmt_current(d->ocp_raw, d->range, b); info_cell(u, x, y + 27, colw - 12, L"OCP (A)", d->valid ? b : L"--", COL_INK);
            fmt_int(d->temp_c, b); wcat(b, L" ℃"); info_cell(u, x, y + 54, colw - 12, L"内部温度", d->valid ? b : L"--", COL_INK);
            info_cell(u, x, y + 81, colw - 12, L"按键锁", d->valid ? (d->lock ? L"锁定" : L"解锁") : L"--", COL_INK);
        } else {
            info_cell(u, x, y, colw - 12, L"工作模式", d->valid ? (d->mode_cc ? L"CC" : L"CV") : L"--", COL_INK);
            info_cell(u, x, y + 27, colw - 12, L"电流档位", d->valid ? (d->range ? L"12A 档" : L"6A 档") : L"--", COL_INK);
            info_cell(u, x, y + 54, colw - 12, L"保护状态",
                      d->valid ? (d->protect == 1 ? L"OVP" : (d->protect == 2 ? L"OCP" : L"正常")) : L"--",
                      d->protect ? COL_WARN : COL_OK);
            fmt_int(d->preset, b); info_cell(u, x, y + 81, colw - 12, L"预设组", d->valid ? b : L"--", COL_INK);
        }
    }

    ui_fill_round(u, R(12, 346, W - 24, 152), 8, COL_PANEL);
    ui_stroke(u, R(12, 346, W - 24, 152), 8, COL_LINE, 1.0f);
    ui_text(u, R(28, 352, 84, 22), L"设定", COL_INK, 15, 0, 0);
    ui_check(u, 50, R(120, 352, 150, 22), L"写入前确认", &a->confirmEnabled);
    dev_fmt_fixed(DEV_V_MAX_MV, 3, b);
    wcopy(c, L"范围 0–"); wcat(c, b); wcat(c, L" V / 0–");
    dev_fmt_current(d->range ? DEV_I_MAX_12A : DEV_I_MAX_6A, d->range, b);
    wcat(c, b); wcat(c, L" A  (");
    wcat(c, d->range ? L"12A 档" : L"6A 档"); wcat(c, L")");
    ui_text(u, R(W - 300, 352, 276, 22), c, COL_DIM, 12, 2, 0);
    iw = (W - 48 - 100 - 48) / 2;
    x2 = 28 + iw + 16;
    x3 = 28 + iw * 2 + 32;
    ui_text(u, R(28, 376, iw, 18), L"设定电压 (V)", COL_DIM, 12, 0, 0);
    ui_text(u, R(x2, 376, iw, 18), L"设定电流 (A)", COL_DIM, 12, 0, 0);
    ui_edit(u, 10, R(28, 396, iw, 30), a->vtext, 32, &a->vlen, 1, 0);
    ui_edit(u, 11, R(x2, 396, iw, 30), a->itext, 32, &a->ilen, 1, 0);
    if (ui_button_col(u, 13, R(x3, 396, 100, 30), L"设定", BT_SET, COL_DARK)) session_set(a);
    if (a->pending) {
        confirm_text(a, ask);
        ui_fill_round(u, R(24, 430, W - 48, 48), 6, COL_ASK_BG);
        ui_text(u, R(34, 434, W - 262, 32), ask, COL_INK, 13, 0, 0);
        if (ui_button_col(u, 31, R(W - 216, 438, 96, 28), L"确认", BT_CONFIRM, COL_WHITE)) session_confirm(a);
        if (ui_button_col(u, 32, R(W - 112, 438, 88, 28), L"取消", BT_CANCEL, COL_WHITE)) session_cancel(a);
    } else {
        int pw = (W - 48 - 40) / PRESET_COUNT;
        ui_text(u, R(28, 430, W - 56, 18),
                a->confirmEnabled ? L"常用组合：点击填入，仍需点设定并确认"
                                  : L"常用组合：点击填入，点设定即写入",
                COL_DIM, 12, 0, 0);
        for (i = 0; i < PRESET_COUNT; i++) {
            if (ui_button_col(u, 40 + i, R(24 + i * (pw + 8), 450, pw, 28),
                              PRESETS[i].label, PRESETS[i].color, COL_WHITE))
                session_preset(a, i);
        }
    }

    ui_fill_round(u, R(12, 506, W - 24, 62), 8, COL_PANEL);
    ui_stroke(u, R(12, 506, W - 24, 62), 8, COL_LINE, 1.0f);
    bw = (W - 48 - 8) / 2;
    if (ui_button_col(u, 20, R(24, 516, bw, 42), L"开输出", BT_ON, COL_WHITE)) session_output(a, 1);
    if (ui_button_col(u, 21, R(24 + bw + 8, 516, bw, 42), L"关输出", BT_OFF, COL_WHITE)) session_output(a, 0);

    ui_hline(u, 0, H - 62, W, COL_LINE);
    ui_fill(u, R(0, H - 61, W, 61), COL_PANEL);
    ui_text(u, R(16, H - 52, W - 32, 22), a->msg, COL_INK, 13, 0, 0);
    dev_fmt_fixed((unsigned long)a->rateX10, 1, b);
    wcopy(c, L"高速轮询 "); wcat(c, b);
    wcat(c, L" 次/秒 · 写入后回读校验 · 校准区不可达");
    ui_text(u, R(16, H - 28, W - 32, 18), c, COL_DIM, 12, 0, 0);

    ui_end(u);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    Session* a = &g;
    switch (m) {
    case WM_CREATE:
        a->hwnd = h;
        g_ui = ui_create(h);
        return 0;

    case WM_SIZE:
        g_dpi = sys_dpi();
        g_W = L96(LOWORD(lp));
        g_H = L96(HIWORD(lp));
        ui_resize(g_ui, LOWORD(lp), HIWORD(lp));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = MulDiv(640, g_dpi, 96);
        mm->ptMinTrackSize.y = MulDiv(640, g_dpi, 96);
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
        ui_mouse_move(g_ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(h);
        ui_mouse_down(g_ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        ui_mouse_up(g_ui, L96((short)LOWORD(lp)), L96((short)HIWORD(lp)));
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_CHAR:
        ui_char(g_ui, (wchar_t)wp);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_KEYDOWN:
        ui_key(g_ui, (int)wp);
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        ui_destroy(g_ui);
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
    g_dpi = sys_dpi();
    session_init(&g, NULL);

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
    rc.right = MulDiv(780, g_dpi, 96);
    rc.bottom = MulDiv(640, g_dpi, 96);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g.hwnd = CreateWindowExW(0, L"TinyApp", L"RD6012P 上位机", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g.hwnd) return 0;
    ShowWindow(g.hwnd, SW_SHOW);
    session_start(&g);

    for (;;) {
        HANDLE ev = session_event(&g);
        DWORD w = ev ? MsgWaitForMultipleObjects(1, &ev, FALSE, INFINITE, QS_ALLINPUT)
                     : MsgWaitForMultipleObjects(0, NULL, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_FAILED) continue;
        if (ev && w == WAIT_OBJECT_0) { session_on_result(&g); continue; }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:
    session_disconnect(&g);
    ExitProcess(0);
}
