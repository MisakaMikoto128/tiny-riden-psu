// ui.cpp - 立即模式 UI 内核实现（Direct2D + DirectWrite）
// 仅本文件为 C++，因为 dwrite.h 只提供 C++ 接口；对外导出的是 ui.h 里的纯 C 符号。
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include "ui.h"

struct Ui {
    HWND hwnd;
    ID2D1Factory* fac;
    ID2D1HwndRenderTarget* rt;
    IDWriteFactory* dw;
    ID2D1SolidColorBrush* br;
    UINT32 tw, th;          // 渲染目标像素尺寸，用于设备丢失后重建

    IDWriteTextFormat* fs12;
    IDWriteTextFormat* fs13;
    IDWriteTextFormat* fs15;
    IDWriteTextFormat* fs20;
    IDWriteTextFormat* fm12;
    IDWriteTextFormat* fm13;
    IDWriteTextFormat* fm20;

    int mx, my;
    int mdown, mpressed, mreleased;
    int wheel;

    int key;
    wchar_t ch[64];
    int chCount;

    int hot;
    int active;

    int popId;
    Rect popAnchor;
    const UiItem* popItems;
    int popCount;
    int popSel;
};

static D2D1_COLOR_F DC(Col c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
static D2D1_RECT_F RF(Rect r) { return D2D1::RectF((FLOAT)r.x, (FLOAT)r.y, (FLOAT)(r.x + r.w), (FLOAT)(r.y + r.h)); }

static IDWriteTextFormat* mkfmt(Ui* u, const wchar_t* fam, int size)
{
    IDWriteTextFormat* f = NULL;
    u->dw->CreateTextFormat(fam, NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, (FLOAT)size, L"zh-cn", &f);
    if (f) {
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return f;
}

Ui* ui_create(HWND hwnd)
{
    Ui* u = (Ui*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Ui));
    if (!u) return NULL;
    u->hwnd = hwnd;
    u->active = -1;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), NULL, (void**)&u->fac);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&u->dw);

    u->fs12 = mkfmt(u, L"Segoe UI", 12);
    u->fs13 = mkfmt(u, L"Segoe UI", 13);
    u->fs15 = mkfmt(u, L"Segoe UI", 15);
    u->fs20 = mkfmt(u, L"Segoe UI", 20);
    u->fm12 = mkfmt(u, L"Consolas", 12);
    u->fm13 = mkfmt(u, L"Consolas", 13);
    u->fm20 = mkfmt(u, L"Consolas", 20);
    return u;
}

void ui_destroy(Ui* u)
{
    if (!u) return;
    if (u->br) u->br->Release();
    if (u->rt) u->rt->Release();
    if (u->fac) u->fac->Release();
    if (u->dw) u->dw->Release();
}

static void drop_target(Ui* u)
{
    if (u->br) { u->br->Release(); u->br = NULL; }
    if (u->rt) { u->rt->Release(); u->rt = NULL; }
}

// 按当前尺寸建立渲染目标。设备丢失后由 ui_begin 再次调用以恢复。
static void make_target(Ui* u)
{
    D2D1_SIZE_U sz;
    D2D1_RENDER_TARGET_PROPERTIES rtp;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hp;
    HDC dc;
    int dpi;

    if (u->rt || !u->tw || !u->th) return;
    sz = D2D1::SizeU(u->tw, u->th);
    rtp = D2D1::RenderTargetProperties();
    hp = D2D1::HwndRenderTargetProperties(u->hwnd, sz);
    u->fac->CreateHwndRenderTarget(rtp, hp, &u->rt);
    if (!u->rt) return;
    u->rt->CreateSolidColorBrush(D2D1::ColorF(0, 0.0f), &u->br);

    // 按实际 DPI 缩放：布局用 96 DPI 逻辑坐标，文字按物理像素栅格化
    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    if (dpi < 96) dpi = 96;
    u->rt->SetDpi((FLOAT)dpi, (FLOAT)dpi);
}

void ui_resize(Ui* u, int w, int h)
{
    u->tw = (UINT32)(w < 1 ? 1 : w);
    u->th = (UINT32)(h < 1 ? 1 : h);
    if (!u->rt) {
        make_target(u);
    } else {
        D2D1_SIZE_U sz = D2D1::SizeU(u->tw, u->th);
        u->rt->Resize(sz);
    }
}

int ui_begin(Ui* u)
{
    u->hot = 0;
    if (!u->rt) make_target(u);
    if (!u->rt || !u->br) return 0;
    u->rt->BeginDraw();
    return 1;
}

void ui_end(Ui* u)
{
    // 下拉弹层统一在最上层绘制
    if (u->popId && u->popItems) {
        Rect pr;
        int i;
        pr.x = u->popAnchor.x;
        pr.y = u->popAnchor.y + u->popAnchor.h + 2;
        pr.w = u->popAnchor.w;
        pr.h = u->popCount * 26 + 8;

        Col white = {1, 1, 1, 1};
        Col edge = {0.86f, 0.878f, 0.902f, 1};
        ui_fill_round(u, pr, 8, white);
        ui_stroke(u, pr, 8, edge, 1.0f);
        for (i = 0; i < u->popCount; i++) {
            Rect ir;
            ir.x = pr.x + 4;
            ir.y = pr.y + 4 + i * 26;
            ir.w = pr.w - 8;
            ir.h = 26;
            int hover = (u->mx >= ir.x && u->mx < ir.x + ir.w && u->my >= ir.y && u->my < ir.y + ir.h);
            if (hover) {
                Col hb = {0.945f, 0.960f, 0.988f, 1};
                ui_fill_round(u, ir, 5, hb);
            }
            if (i == u->popSel) {
                Col sb = {0.925f, 0.945f, 0.985f, 1};
                ui_fill_round(u, ir, 5, sb);
            }
            Col tc = (i == u->popSel) ? Col{0.184f, 0.435f, 0.922f, 1} : Col{0.16f, 0.18f, 0.21f, 1};
            ui_text(u, ir, u->popItems[i].label, tc, 13, 0, 0);
        }
    }

    {
        HRESULT hr = u->rt->EndDraw();
        // 设备丢失（切换显示器、驱动重置、休眠唤醒）后必须重建，
        // 否则会一直画进失效的目标，表现为窗口永久空白。
        if (hr == D2DERR_RECREATE_TARGET) {
            drop_target(u);
            InvalidateRect(u->hwnd, NULL, FALSE);
        }
    }
    u->mpressed = 0;
    u->mreleased = 0;
    u->wheel = 0;
    u->key = 0;
    u->chCount = 0;
}

void ui_fill(Ui* u, Rect r, Col c)
{
    D2D1_RECT_F f = RF(r);
    u->br->SetColor(DC(c));
    u->rt->FillRectangle(f, u->br);
}

void ui_fill_round(Ui* u, Rect r, int rad, Col c)
{
    D2D1_ROUNDED_RECT rr = {RF(r), (FLOAT)rad, (FLOAT)rad};
    u->br->SetColor(DC(c));
    u->rt->FillRoundedRectangle(rr, u->br);
}

void ui_stroke(Ui* u, Rect r, int rad, Col c, float w)
{
    D2D1_ROUNDED_RECT rr = {RF(r), (FLOAT)rad, (FLOAT)rad};
    u->br->SetColor(DC(c));
    u->rt->DrawRoundedRectangle(rr, u->br, w, NULL);
}

void ui_hline(Ui* u, int x, int y, int w, Col c)
{
    Rect r;
    r.x = x; r.y = y; r.w = w; r.h = 1;
    ui_fill(u, r, c);
}

static IDWriteTextFormat* pick(Ui* u, int size, int mono)
{
    if (mono) return size >= 20 ? u->fm20 : (size >= 13 ? u->fm13 : u->fm12);
    if (size >= 20) return u->fs20;
    if (size >= 15) return u->fs15;
    if (size >= 13) return u->fs13;
    return u->fs12;
}

void ui_textn(Ui* u, Rect r, const wchar_t* s, int len, Col c, int size, int align, int mono)
{
    IDWriteTextFormat* f = pick(u, size, mono);
    D2D1_RECT_F fr = RF(r);
    if (!f || !s || len <= 0) return;
    f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER :
                        align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING :
                                     DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    u->br->SetColor(DC(c));
    u->rt->DrawText(s, (UINT32)len, f, fr, u->br,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

void ui_text(Ui* u, Rect r, const wchar_t* s, Col c, int size, int align, int mono)
{
    int n = 0;
    while (s && s[n]) n++;
    ui_textn(u, r, s, n, c, size, align, mono);
}

static int can_hit(Ui* u, int id, Rect r)
{
    if (u->popId && u->popId != id) return 0;
    if (!u->rt) return 0;
    return u->mx >= r.x && u->mx < r.x + r.w && u->my >= r.y && u->my < r.y + r.h;
}

static Col mix(Col a, Col b, float t)
{
    Col c;
    c.r = a.r + (b.r - a.r) * t;
    c.g = a.g + (b.g - a.g) * t;
    c.b = a.b + (b.b - a.b) * t;
    c.a = 1.0f;
    return c;
}

int ui_button(Ui* u, int id, Rect r, const wchar_t* label, int kind)
{
    int hit = can_hit(u, id, r);
    int clicked = 0;

    if (hit) u->hot = id;
    if (hit && u->mpressed) u->active = id;
    if (hit && u->mreleased && u->active == id) clicked = 1;
    if (u->mreleased && u->active == id) u->active = -1;

    Col accent = {0.184f, 0.435f, 0.922f, 1};
    Col white = {1, 1, 1, 1};
    Col ink = {0.122f, 0.137f, 0.157f, 1};
    Col line = {0.878f, 0.894f, 0.918f, 1};

    if (kind == 1) {
        Col base = accent;
        if (hit && u->active == id) base = mix(accent, ink, 0.22f);
        else if (hit) base = mix(accent, white, 0.10f);
        ui_fill_round(u, r, 6, base);
        ui_text(u, r, label, white, 13, 1, 0);
    } else if (kind == 2) {
        Col hb = {0.945f, 0.953f, 0.969f, 1};
        if (hit) ui_fill_round(u, r, 6, hb);
        ui_text(u, r, label, hit ? ink : Col{0.42f, 0.45f, 0.49f, 1}, 13, 1, 0);
    } else {
        Col base = white;
        if (hit && u->active == id) base = Col{0.91f, 0.925f, 0.945f, 1};
        else if (hit) base = Col{0.965f, 0.973f, 0.984f, 1};
        ui_fill_round(u, r, 6, base);
        ui_stroke(u, r, 6, line, 1.0f);
        ui_text(u, r, label, ink, 13, 1, 0);
    }
    return clicked ? UI_CLICKED : 0;
}

int ui_button_col(Ui* u, int id, Rect r, const wchar_t* label, Col bg, Col fg)
{
    int hit = can_hit(u, id, r);
    int clicked = 0;
    Col ink = {0.10f, 0.11f, 0.13f, 1};
    Col white = {1, 1, 1, 1};
    Col base = bg;

    if (hit) u->hot = id;
    if (hit && u->mpressed) u->active = id;
    if (hit && u->mreleased && u->active == id) clicked = 1;
    if (u->mreleased && u->active == id) u->active = -1;

    if (hit && u->active == id) base = mix(bg, ink, 0.22f);
    else if (hit) base = mix(bg, white, 0.14f);
    ui_fill_round(u, r, 6, base);
    ui_stroke(u, r, 6, mix(bg, ink, 0.16f), 1.0f);
    ui_text(u, r, label, fg, 13, 1, 0);
    return clicked ? UI_CLICKED : 0;
}

int ui_check(Ui* u, int id, Rect r, const wchar_t* label, int* value)
{
    int hit = can_hit(u, id, r);
    int changed = 0;
    Rect box;
    box.x = r.x;
    box.y = r.y + (r.h - 16) / 2;
    box.w = 16;
    box.h = 16;

    if (hit && u->mpressed) u->active = id;
    if (hit && u->mreleased && u->active == id) { *value = !*value; changed = 1; }
    if (u->mreleased && u->active == id) u->active = -1;

    Col accent = {0.184f, 0.435f, 0.922f, 1};
    Col line = {0.75f, 0.78f, 0.82f, 1};

    if (*value) {
        ui_fill_round(u, box, 4, accent);
        Rect tick;
        tick.x = box.x + 4;
        tick.y = box.y + 4;
        tick.w = 8;
        tick.h = 8;
        ui_fill(u, tick, Col{1, 1, 1, 1});
    } else {
        ui_fill_round(u, box, 4, hit ? Col{0.955f, 0.965f, 0.98f, 1} : Col{1, 1, 1, 1});
        ui_stroke(u, box, 4, line, 1.0f);
    }
    {
        Rect tr;
        tr.x = box.x + 24;
        tr.y = r.y;
        tr.w = r.w - 24;
        tr.h = r.h;
        ui_text(u, tr, label, Col{0.24f, 0.26f, 0.29f, 1}, 13, 0, 0);
    }
    return changed ? UI_CHANGED : 0;
}

int ui_combo(Ui* u, int id, Rect r, const UiItem* items, int count, int* sel)
{
    int hit = can_hit(u, id, r);
    int changed = 0;

    if (hit && u->mpressed) u->active = id;
    if (hit && u->mreleased && u->active == id) {
        if (u->popId == id) u->popId = 0;
        else {
            u->popId = id;
            u->popAnchor = r;
            u->popItems = items;
            u->popCount = count;
            u->popSel = *sel;
        }
    }
    if (u->mreleased && u->active == id) u->active = -1;

    Col line = {0.878f, 0.894f, 0.918f, 1};
    Col bg = (u->popId == id) ? Col{0.955f, 0.965f, 0.985f, 1} : Col{1, 1, 1, 1};
    if (hit && u->popId != id) bg = Col{0.975f, 0.980f, 0.988f, 1};
    ui_fill_round(u, r, 6, bg);
    ui_stroke(u, r, 6, line, 1.0f);

    if (count > 0 && *sel >= 0 && *sel < count) {
        Rect tr;
        tr.x = r.x + 10;
        tr.y = r.y;
        tr.w = r.w - 26;
        tr.h = r.h;
        ui_text(u, tr, items[*sel].label, Col{0.122f, 0.137f, 0.157f, 1}, 13, 0, 0);
    }
    {
        Rect ar;
        ar.x = r.x + r.w - 18;
        ar.y = r.y + r.h / 2 - 2;
        ar.w = 8;
        ar.h = 8;
        ui_text(u, ar, L"\x25BE", Col{0.45f, 0.48f, 0.52f, 1}, 12, 1, 0);
    }

    if (u->popId == id) {
        Rect pr;
        int i;
        pr.x = r.x;
        pr.y = r.y + r.h + 2;
        pr.w = r.w;
        pr.h = count * 26 + 8;
        for (i = 0; i < count; i++) {
            Rect ir;
            ir.x = pr.x + 4;
            ir.y = pr.y + 4 + i * 26;
            ir.w = pr.w - 8;
            ir.h = 26;
            if (u->mx >= ir.x && u->mx < ir.x + ir.w && u->my >= ir.y && u->my < ir.y + ir.h) {
                if (u->mreleased) { *sel = i; u->popId = 0; changed = 1; }
            }
        }
        if (u->mreleased) {
            int inside = u->mx >= pr.x && u->mx < pr.x + pr.w && u->my >= pr.y && u->my < pr.y + pr.h;
            if (!inside) u->popId = 0;
        }
    }
    return changed ? UI_CHANGED : 0;
}

int ui_edit(Ui* u, int id, Rect r, wchar_t* buf, int cap, int* len, int mono, int align)
{
    int hit = can_hit(u, id, r);
    int i;
    int ret = 0;
    Col line = {0.878f, 0.894f, 0.918f, 1};
    int cw = mono ? 8 : 7;
    int pad = 10;

    if (hit && u->mpressed) { u->active = id; u->popId = 0; }
    if (!hit && u->mpressed && u->active == id) u->active = -1;

    if (u->active == id) {
        for (i = 0; i < u->chCount; i++) {
            wchar_t c = u->ch[i];
            if (c < 32 || c == 127) continue;
            if (*len < cap - 1) { buf[*len] = c; (*len)++; buf[*len] = 0; }
        }
        if (u->key == 8 && *len > 0) { (*len)--; buf[*len] = 0; }
        if (u->key == 13) ret = UI_ENTER;
    }

    ui_fill_round(u, r, 6, Col{1, 1, 1, 1});
    ui_stroke(u, r, 6, (u->active == id) ? Col{0.184f, 0.435f, 0.922f, 1} : line, 1.0f);

    {
        int vis = (r.w - pad * 2) / cw;
        int start = 0;
        if (vis < 1) vis = 1;
        if (*len > vis) start = *len - vis;
        {
            Rect tr;
            tr.x = r.x + pad;
            tr.y = r.y;
            tr.w = r.w - pad * 2;
            tr.h = r.h;
            ui_textn(u, tr, buf + start, *len - start, Col{0.122f, 0.137f, 0.157f, 1}, 13, align, mono);
        }
        if (u->active == id) {
            Rect caret;
            caret.x = r.x + pad + (*len - start) * cw;
            caret.y = r.y + 8;
            caret.w = 1;
            caret.h = r.h - 16;
            ui_fill(u, caret, Col{0.184f, 0.435f, 0.922f, 1});
        }
    }
    return ret;
}

void ui_mouse_move(Ui* u, int x, int y) { u->mx = x; u->my = y; }
void ui_mouse_down(Ui* u, int x, int y) { u->mx = x; u->my = y; u->mdown = 1; u->mpressed = 1; }
void ui_mouse_up(Ui* u, int x, int y) { u->mx = x; u->my = y; u->mdown = 0; u->mreleased = 1; }
void ui_wheel(Ui* u, int delta) { u->wheel += delta; }
void ui_char(Ui* u, wchar_t c) { if (c >= 32 && u->chCount < 60) u->ch[u->chCount++] = c; }
void ui_key(Ui* u, int vk)
{
    if (vk == VK_BACK) u->key = 8;
    else if (vk == VK_RETURN) u->key = 13;
}
