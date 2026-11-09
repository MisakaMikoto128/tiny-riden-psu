// ui.h - 立即模式 UI 内核（纯 C 接口）
// 实现层用 Direct2D + DirectWrite，两者是系统自带组件，不进入 exe 体积。
#pragma once

#include <windows.h>

typedef struct { int x, y, w, h; } Rect;
typedef struct { float r, g, b, a; } Col;
typedef struct Ui Ui;

#ifdef __cplusplus
extern "C" {
#endif

#define UI_CLICKED 1
#define UI_CHANGED 2
#define UI_ENTER   4

typedef struct {
    const wchar_t* label;
    int value;
} UiItem;

Ui*  ui_create(HWND hwnd);
void ui_destroy(Ui* u);
void ui_resize(Ui* u, int w, int h);
int  ui_begin(Ui* u);
void ui_end(Ui* u);

void ui_fill(Ui* u, Rect r, Col c);
void ui_fill_round(Ui* u, Rect r, int rad, Col c);
void ui_stroke(Ui* u, Rect r, int rad, Col c, float w);
void ui_hline(Ui* u, int x, int y, int w, Col c);
void ui_text(Ui* u, Rect r, const wchar_t* s, Col c, int size, int align, int mono);
void ui_textn(Ui* u, Rect r, const wchar_t* s, int len, Col c, int size, int align, int mono);

int ui_button(Ui* u, int id, Rect r, const wchar_t* label, int kind);
int ui_button_col(Ui* u, int id, Rect r, const wchar_t* label, Col bg, Col fg);
int ui_check(Ui* u, int id, Rect r, const wchar_t* label, int* value);
int ui_combo(Ui* u, int id, Rect r, const UiItem* items, int count, int* sel);
int ui_edit(Ui* u, int id, Rect r, wchar_t* buf, int cap, int* len, int mono, int align);

// 输入注入，由窗口过程调用
void ui_mouse_move(Ui* u, int x, int y);
void ui_mouse_down(Ui* u, int x, int y);
void ui_mouse_up(Ui* u, int x, int y);
void ui_wheel(Ui* u, int delta);
void ui_char(Ui* u, wchar_t c);
void ui_key(Ui* u, int vk);

#ifdef __cplusplus
}
#endif
