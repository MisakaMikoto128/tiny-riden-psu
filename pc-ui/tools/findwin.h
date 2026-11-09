// findwin.h - 按类名或标题子串查找顶层窗口（开发期工具共用）
#pragma once
#include <windows.h>

static int wsub(const wchar_t* s, const wchar_t* sub)
{
    int i, j;
    if (!sub[0]) return 1;
    for (i = 0; s[i]; i++) {
        for (j = 0; sub[j] && s[i + j] == sub[j]; j++) {}
        if (!sub[j]) return 1;
    }
    return 0;
}

typedef struct {
    const wchar_t* needle;
    HWND found;
} FindCtx;

static BOOL CALLBACK find_cb(HWND h, LPARAM lp)
{
    FindCtx* c = (FindCtx*)lp;
    wchar_t buf[256];
    if (!IsWindowVisible(h)) return TRUE;
    GetClassNameW(h, buf, 256);
    if (wsub(buf, c->needle)) { c->found = h; return FALSE; }
    GetWindowTextW(h, buf, 256);
    if (wsub(buf, c->needle)) { c->found = h; return FALSE; }
    return TRUE;
}

// 环境变量 TINY_WINDOW 可覆盖匹配串，默认 "Tiny"
static HWND find_app_window(void)
{
    wchar_t needle[128];
    FindCtx c;
    DWORD n = GetEnvironmentVariableW(L"TINY_WINDOW", needle, 128);
    if (n == 0) { needle[0] = L'T'; needle[1] = L'i'; needle[2] = L'n'; needle[3] = L'y'; needle[4] = 0; }
    c.needle = needle;
    c.found = NULL;
    EnumWindows(find_cb, (LPARAM)&c);
    return c.found;
}
