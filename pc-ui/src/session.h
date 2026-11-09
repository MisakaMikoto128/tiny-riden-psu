/* session.h - 上位机会话：连接生命周期、显示副本与动作入口。
   真实快照由 device 层持有；本层只持有乐观显示副本与界面输入状态。
   UI 通过本层提交动作，不访问物理连接，也不持动作队列。 */
#ifndef SESSION_H
#define SESSION_H

#include <windows.h>
#include "ui.h"
#include "device.h"
#include "../transport/serial_io.h"

#define PRESET_COUNT 6

typedef struct {
    const wchar_t* label;
    int mv;
    int micro_a;
    Col color;
} Preset;

extern const Preset PRESETS[PRESET_COUNT];

typedef struct {
    HWND hwnd;
    MbClient* client;
    MbIo io;
    SerialCtx sctx;
    Device dev;

    DevSnapshot display;
    int optimistic;
    DWORD optimisticAt;

    wchar_t ports[16][16];
    UiItem portItems[16];
    int portCount, portSel;

    wchar_t vtext[32], itext[32];
    int vlen, ilen, inputsReady;

    int confirmEnabled, pending, pendV, pendI;
    int wasBusy, refreshCount, rateX10;
    DWORD rateAt;
    wchar_t msg[192];
} Session;

void session_init(Session* s, HWND hwnd);
void session_start(Session* s);
void session_connect(Session* s);
void session_disconnect(Session* s);
HANDLE session_event(Session* s);
int  session_online(const Session* s);
void session_on_result(Session* s);

void session_preset(Session* s, int index);
void session_set(Session* s);
void session_output(Session* s, int on);
void session_confirm(Session* s);
void session_cancel(Session* s);

#endif
