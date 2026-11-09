/* serial_io.c - Win32 serial byte-stream backend.
   ReadFile returns as soon as bytes are available, or after a short wait.
   Every call here runs on the transport worker. */
#include "serial_io.h"

static int s_open(void* p)
{
    SerialCtx* c = (SerialCtx*)p;
    wchar_t path[48];
    DCB dcb;
    COMMTIMEOUTS to;
    int i;

    path[0] = L'\\'; path[1] = L'\\'; path[2] = L'.'; path[3] = L'\\';
    for (i = 0; c->port[i] && i < 31; i++) path[4 + i] = c->port[i];
    path[4 + i] = 0;

    c->h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (c->h == INVALID_HANDLE_VALUE) { c->h = 0; return 0; }

    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(c->h, &dcb)) { CloseHandle(c->h); c->h = 0; return 0; }
    dcb.BaudRate = c->baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(c->h, &dcb)) { CloseHandle(c->h); c->h = 0; return 0; }

    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 3;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(c->h, &to);
    PurgeComm(c->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return 1;
}

static int s_read(void* p, unsigned char* buf, int cap)
{
    SerialCtx* c = (SerialCtx*)p;
    DWORD got = 0;
    if (!c->h) return -1;
    if (!ReadFile(c->h, buf, (DWORD)cap, &got, 0)) return -1;
    return (int)got;
}

static int s_write(void* p, const unsigned char* buf, int len)
{
    SerialCtx* c = (SerialCtx*)p;
    DWORD put = 0;
    if (!c->h) return 0;
    if (!WriteFile(c->h, buf, (DWORD)len, &put, 0)) return -1;
    return (int)put;
}

static void s_purge(void* p)
{
    SerialCtx* c = (SerialCtx*)p;
    if (c->h) PurgeComm(c->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

static void s_close(void* p)
{
    SerialCtx* c = (SerialCtx*)p;
    if (c->h) { CloseHandle(c->h); c->h = 0; }
}

int serial_io_enum(wchar_t ports[][16], int cap)
{
    wchar_t name[16], target[256];
    int i, k, p, v, n = 0;
    for (i = 1; i <= 64 && n < cap; i++) {
        wchar_t tmp[8];
        v = i;
        p = 0;
        name[0] = L'C'; name[1] = L'O'; name[2] = L'M';
        while (v > 0) { tmp[p++] = (wchar_t)(L'0' + v % 10); v /= 10; }
        for (k = 0; k < p; k++) name[3 + k] = tmp[p - 1 - k];
        name[3 + p] = 0;
        if (QueryDosDeviceW(name, target, 256) != 0) {
            for (k = 0; k <= 3 + p; k++) ports[n][k] = name[k];
            n++;
        }
    }
    return n;
}

void serial_io_init(MbIo* io, SerialCtx* ctx, const wchar_t* port, unsigned int baud)
{
    int i;
    for (i = 0; port[i] && i < 31; i++) ctx->port[i] = port[i];
    ctx->port[i] = 0;
    ctx->baud = baud;
    ctx->h = 0;
    io->ctx = ctx;
    io->open = s_open;
    io->read = s_read;
    io->write = s_write;
    io->purge = s_purge;
    io->close = s_close;
}
