/* serial_io.h - Win32 serial byte-stream backend.
   The caller owns the context; the transport worker owns opening and closing.
   Port enumeration, init and I/O are application-independent. */
#ifndef SERIAL_IO_H
#define SERIAL_IO_H

#include "modbus_rtu.h"

typedef struct {
    wchar_t port[32];
    unsigned int baud;
    HANDLE h;
} SerialCtx;

void serial_io_init(MbIo* io, SerialCtx* ctx, const wchar_t* port, unsigned int baud);
int  serial_io_enum(wchar_t ports[][16], int cap);

#endif
