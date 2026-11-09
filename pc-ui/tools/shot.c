// shot.c - 抓屏存 BMP，用于开发期验证界面
// 找到目标窗口时只截该窗口；找不到则截全屏。输出到当前目录 shot.bmp。
#include <windows.h>
#include "findwin.h"

int main(void)
{
    HWND win;
    RECT cr;
    POINT pt;
    int ox = 0, oy = 0, w, h;
    HDC sdc;
    HDC mdc;
    BITMAPINFO bi;
    void* bits = NULL;
    HBITMAP bmp;
    HANDLE f;
    DWORD wrote;
    int stride;
    int y, x;
    unsigned char* row;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;

    SetProcessDPIAware();

    win = find_app_window();
    if (win) {
        // 先置顶再截，否则 BitBlt 抓到的可能是遮挡它的窗口；
        // 截完再取消置顶，避免影响用户。
        SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        Sleep(150);
        GetClientRect(win, &cr);
        pt.x = 0;
        pt.y = 0;
        ClientToScreen(win, &pt);
        ox = pt.x;
        oy = pt.y;
        w = cr.right - cr.left;
        h = cr.bottom - cr.top;
    } else {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    sdc = GetDC(NULL);
    mdc = CreateCompatibleDC(sdc);

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, w, h, sdc, ox, oy, SRCCOPY);

    stride = ((w * 3 + 3) / 4) * 4;

    ZeroMemory(&fh, sizeof(fh));
    ZeroMemory(&ih, sizeof(ih));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)(stride * h);
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = (DWORD)(stride * h);

    f = CreateFileA("shot.bmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 1;
    WriteFile(f, &fh, sizeof(fh), &wrote, NULL);
    WriteFile(f, &ih, sizeof(ih), &wrote, NULL);

    row = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, stride);
    for (y = h - 1; y >= 0; y--) {
        unsigned int* src = (unsigned int*)bits + (size_t)y * w;
        for (x = 0; x < w; x++) {
            unsigned int p = src[x];
            row[x * 3 + 0] = (unsigned char)(p & 0xFF);
            row[x * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
            row[x * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
        }
        WriteFile(f, row, (DWORD)stride, &wrote, NULL);
    }
    CloseHandle(f);
    if (win) SetWindowPos(win, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return 0;
}
