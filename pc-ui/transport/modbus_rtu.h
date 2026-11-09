/* modbus_rtu.h - Modbus RTU master transport over a byte stream.
   Frame parsing, one pending request per link, timeout/retry and link reset.
   The byte-stream backend is supplied by the caller; this unit is application-independent. */
#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MB_FRAME_MAX 256
#define MB_VALUES_MAX 123
#define MB_QUEUE_MAX 8

typedef enum {
    MB_STAT_OK = 0,
    MB_ERR_TIMEOUT,
    MB_ERR_CRC,
    MB_ERR_EXCEPTION,
    MB_ERR_IO,
    MB_ERR_FULL
} MbStatus;

typedef struct {
    unsigned char slave;
    unsigned char func;
    unsigned short addr;
    unsigned short count;   /* fc 03: register count; fc 06: value */
} MbRequest;

typedef struct {
    MbStatus status;
    unsigned char func;
    unsigned char exception;
    unsigned short count;
    unsigned short values[MB_VALUES_MAX];
} MbResult;

/* read: >0 bytes, 0 no data, <0 link error. write: >0 success, <=0 link error. */
typedef struct {
    void* ctx;
    int (*open)(void* ctx);
    int (*read)(void* ctx, unsigned char* buf, int cap);
    int (*write)(void* ctx, const unsigned char* buf, int len);
    void (*purge)(void* ctx);
    void (*close)(void* ctx);
} MbIo;

typedef struct MbClient MbClient;

unsigned short mb_crc16(const unsigned char* data, int len);
int  mb_build(const MbRequest* req, unsigned char* out, int cap);

/* Consumes one frame from buf. Returns 1 with a result, 0 when more bytes are
   needed, -1 on bad arguments. Invalid heads are dropped one byte at a time. */
int  mb_parse(unsigned char* buf, int* len, unsigned char slave, MbResult* out);

MbClient* mb_create(const MbIo* io, unsigned char slave, unsigned int timeout_ms, unsigned int retries);
int  mb_start(MbClient* c);
void mb_destroy(MbClient* c);

int  mb_submit(MbClient* c, const MbRequest* req, unsigned int* seq);
int  mb_take(MbClient* c, MbResult* out, unsigned int* seq);
void mb_feed(MbClient* c, const unsigned char* data, int len);
HANDLE mb_result_event(MbClient* c);
int  mb_online(const MbClient* c);
void mb_reopen(MbClient* c);

#ifdef __cplusplus
}
#endif
#endif
