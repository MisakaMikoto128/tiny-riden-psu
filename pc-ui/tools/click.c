// click.c - 按逻辑坐标点击目标窗口客户区，用于自动化验证控件
// 用法: click.exe <逻辑x> <逻辑y>   或   click.exe 0 0  （只把窗口提到前台，不点击）
#include <windows.h>
#include "findwin.h"

static int num_from(const char* s, int a, int b)
{
    int v = 0;
    while (a < b && s[a] >= '0' && s[a] <= '9') v = v * 10 + (s[a++] - '0');
    return v;
}

// 从命令行末尾取两个数字参数，避免被路径中的空格干扰
static void last_two(const char* cmd, int* px, int* py)
{
    int len = 0;
    int i, endB, startB, endA, startA;
    while (cmd[len]) len++;
    i = len - 1;
    while (i >= 0 && cmd[i] == ' ') i--;
    endB = i + 1;
    while (i >= 0 && cmd[i] != ' ') i--;
    startB = i + 1;
    while (i >= 0 && cmd[i] == ' ') i--;
    endA = i + 1;
    while (i >= 0 && cmd[i] != ' ') i--;
    startA = i + 1;
    *px = num_from(cmd, startA, endA);
    *py = num_from(cmd, startB, endB);
}

int main(void)
{
    HWND h;
    POINT p;
    HDC dc;
    int dpi;
    int x = 0, y = 0;

    SetProcessDPIAware();
    h = find_app_window();
    if (!h) return 1;

    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);

    last_two(GetCommandLineA(), &x, &y);

    SetForegroundWindow(h);
    ShowWindow(h, SW_SHOW);
    // SetForegroundWindow 常被前台锁拦下，用 TOPMOST 再取消才能真正抬到最前
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Sleep(120);
    if (x == 0 && y == 0) return 0;

    p.x = MulDiv(x, dpi, 96);
    p.y = MulDiv(y, dpi, 96);
    ClientToScreen(h, &p);

    SetCursorPos(p.x, p.y);
    Sleep(80);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    Sleep(80);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    return 0;
}
