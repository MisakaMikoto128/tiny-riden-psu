// keys.c - 向目标窗口注入按键，用于验证输入行为
// 用法: keys.exe "abc{enter}{ctrl+a}{bs}"
// 文本走 SendInput 的 UNICODE 路径，不经过输入法；输入法组合过程需人工测试。
#include <windows.h>
#include <shellapi.h>
#include "findwin.h"

static void send_vk(WORD vk, int down)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

static void send_char(wchar_t c)
{
    INPUT in[2];
    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wScan = c;
    in[0].ki.dwFlags = KEYEVENTF_UNICODE;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static int key_from_name(const wchar_t* s, int n)
{
    struct { const wchar_t* name; int vk; } map[] = {
        {L"enter", VK_RETURN}, {L"tab", VK_TAB}, {L"esc", VK_ESCAPE},
        {L"bs", VK_BACK}, {L"del", VK_DELETE}, {L"home", VK_HOME},
        {L"end", VK_END}, {L"up", VK_UP}, {L"down", VK_DOWN},
        {L"left", VK_LEFT}, {L"right", VK_RIGHT},
        {L"pgup", VK_PRIOR}, {L"pgdn", VK_NEXT}, {L"space", VK_SPACE},
    };
    int i;
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++) {
        int k = 0;
        while (k < n && map[i].name[k] && s[k] == map[i].name[k]) k++;
        if (k == n && map[i].name[k] == 0) return map[i].vk;
    }
    if (n == 1) {
        wchar_t c = s[0];
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
        if (c >= L'A' && c <= L'Z') return c;
        if (c >= L'0' && c <= L'9') return c;
    }
    return 0;
}

// 处理 {ctrl+a} 这类组合
static void send_token(const wchar_t* s, int n)
{
    int ctrl = 0, shift = 0, alt = 0;
    int start = 0;
    int i, vk;

    for (i = 0; i <= n; i++) {
        if (i == n || s[i] == L'+') {
            int len = i - start;
            if (len == 4 && s[start] == L'c' && s[start + 1] == L't' && s[start + 2] == L'r') ctrl = 1;
            else if (len == 5 && s[start] == L's' && s[start + 1] == L'h') shift = 1;
            else if (len == 3 && s[start] == L'a' && s[start + 1] == L'l') alt = 1;
            else break;
            start = i + 1;
        }
    }

    vk = key_from_name(s + start, n - start);
    if (!vk) return;

    if (ctrl) send_vk(VK_CONTROL, 1);
    if (shift) send_vk(VK_SHIFT, 1);
    if (alt) send_vk(VK_MENU, 1);
    send_vk((WORD)vk, 1);
    send_vk((WORD)vk, 0);
    if (alt) send_vk(VK_MENU, 0);
    if (shift) send_vk(VK_SHIFT, 0);
    if (ctrl) send_vk(VK_CONTROL, 0);
}

int main(void)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    HWND h;
    const wchar_t* s;
    int i;

    if (argc < 2) return 2;
    SetProcessDPIAware();

    h = find_app_window();
    if (!h) return 1;

    SetForegroundWindow(h);
    ShowWindow(h, SW_SHOW);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    Sleep(120);

    s = argv[1];
    i = 0;
    while (s[i]) {
        if (s[i] == L'{') {
            int j = i + 1;
            while (s[j] && s[j] != L'}') j++;
            if (s[j] == L'}') {
                send_token(s + i + 1, j - i - 1);
                i = j + 1;
                Sleep(30);
                continue;
            }
        }
        send_char(s[i]);
        i++;
        Sleep(15);
    }
    Sleep(120);
    return 0;
}
