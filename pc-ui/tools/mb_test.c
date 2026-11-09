/* mb_test.c - 传输层验证：帧解析与客户端状态机。
   用内存字节流后端驱动，不占用真实串口，结果可重复。 */
#include <stdio.h>
#include <string.h>
#include "../transport/modbus_rtu.h"

static int passed, failed;

#define CHECK(cond, name) do { \
    if (cond) { passed++; printf("  [ok]   %s\n", name); } \
    else { failed++; printf("  [FAIL] %s\n", name); } \
} while (0)

typedef struct {
    unsigned char pending[64];
    int len;
    unsigned char last[8];
    int writes;
    int silent;
    int split;
    int fail_open;
    int opens;
} FakeCtx;

static int fake_open(void* p)
{
    FakeCtx* f = (FakeCtx*)p;
    f->opens++;
    return !f->fail_open;
}

static int fake_read(void* p, unsigned char* buf, int cap)
{
    FakeCtx* f = (FakeCtx*)p;
    int i, n = f->len;
    if (n <= 0) { Sleep(1); return 0; }
    if (f->split && n > 1) n = 1;
    if (n > cap) n = cap;
    for (i = 0; i < n; i++) buf[i] = f->pending[i];
    for (i = n; i < f->len; i++) f->pending[i - n] = f->pending[i];
    f->len -= n;
    return n;
}

static int fake_write(void* p, const unsigned char* buf, int len)
{
    FakeCtx* f = (FakeCtx*)p;
    unsigned char resp[8];
    unsigned short crc, val;
    int i;
    for (i = 0; i < len && i < 8; i++) f->last[i] = buf[i];
    f->writes++;
    if (f->silent) return len;
    if (buf[1] == 3) {
        val = (unsigned short)((buf[2] << 8 | buf[3]) + 1000);
        resp[0] = buf[0]; resp[1] = 3; resp[2] = 2;
        resp[3] = (unsigned char)(val >> 8); resp[4] = (unsigned char)val;
        crc = mb_crc16(resp, 5);
        resp[5] = (unsigned char)crc; resp[6] = (unsigned char)(crc >> 8);
        for (i = 0; i < 7; i++) f->pending[i] = resp[i];
        f->len = 7;
    } else if (buf[1] == 6) {
        for (i = 0; i < 8; i++) f->pending[i] = buf[i];
        f->len = 8;
    } else if (buf[1] == 0x10) {
        resp[0] = buf[0]; resp[1] = 0x10;
        resp[2] = buf[2]; resp[3] = buf[3];
        resp[4] = buf[4]; resp[5] = buf[5];
        crc = mb_crc16(resp, 6);
        resp[6] = (unsigned char)crc; resp[7] = (unsigned char)(crc >> 8);
        for (i = 0; i < 8; i++) f->pending[i] = resp[i];
        f->len = 8;
    }
    return len;
}

static void fake_purge(void* p) { ((FakeCtx*)p)->len = 0; }
static void fake_close(void* p) { ((FakeCtx*)p)->len = 0; }

static void fake_init(MbIo* io, FakeCtx* f)
{
    int i;
    for (i = 0; i < (int)sizeof(FakeCtx); i++) ((unsigned char*)f)[i] = 0;
    io->ctx = f;
    io->open = fake_open;
    io->read = fake_read;
    io->write = fake_write;
    io->purge = fake_purge;
    io->close = fake_close;
}

static int wait_result(MbClient* c, int ms, MbResult* r, unsigned int* seq)
{
    int t = 0;
    while (t < ms) {
        if (mb_take(c, r, seq)) return 1;
        Sleep(10);
        t += 10;
    }
    return 0;
}

static void make_read_frame(unsigned char* out, unsigned char slave, unsigned short val, int* len)
{
    unsigned short crc;
    out[0] = slave; out[1] = 3; out[2] = 2;
    out[3] = (unsigned char)(val >> 8); out[4] = (unsigned char)val;
    crc = mb_crc16(out, 5);
    out[5] = (unsigned char)crc; out[6] = (unsigned char)(crc >> 8);
    *len = 7;
}

static void test_parse(void)
{
    unsigned char buf[128], frame[16];
    unsigned short crc;
    int n, i, got;
    MbResult r;

    printf("[1] frame parser\n");
    make_read_frame(buf, 1, 60125, &n);
    CHECK(mb_parse(buf, &n, 1, &r) == 1 && r.status == MB_STAT_OK &&
          r.count == 1 && r.values[0] == 60125 && n == 0, "0x03 single register");

    make_read_frame(frame, 1, 48000, &n);
    n = 3;
    for (i = 0; i < 3; i++) buf[i] = frame[i];
    CHECK(mb_parse(buf, &n, 1, &r) == 0 && n == 3, "half frame waits for more bytes");
    for (i = 3; i < 7; i++) buf[n++] = frame[i];
    CHECK(mb_parse(buf, &n, 1, &r) == 1 && r.values[0] == 48000, "half frame completes");

    make_read_frame(frame, 1, 1001, &n);
    for (i = 0; i < 7; i++) buf[i] = frame[i];
    make_read_frame(frame, 1, 1002, &n);
    for (i = 0; i < 7; i++) buf[7 + i] = frame[i];
    n = 14;
    got = 0;
    if (mb_parse(buf, &n, 1, &r) == 1 && r.values[0] == 1001) got++;
    if (mb_parse(buf, &n, 1, &r) == 1 && r.values[0] == 1002) got++;
    CHECK(got == 2 && n == 0, "two frames glued together");

    memset(buf, 0, sizeof(buf));
    buf[0] = 2; buf[1] = 3; buf[2] = 2; buf[3] = 0x12; buf[4] = 0x34;
    crc = mb_crc16(buf, 5);
    buf[5] = (unsigned char)crc; buf[6] = (unsigned char)(crc >> 8);
    make_read_frame(frame, 1, 7777, &n);
    for (i = 0; i < 7; i++) buf[7 + i] = frame[i];
    n = 14;
    got = 0;
    while (n > 0 && got < 20) {
        if (mb_parse(buf, &n, 1, &r) == 1 && r.status == MB_STAT_OK) {
            got = 100;
            break;
        }
        got++;
    }
    CHECK(got == 100 && r.values[0] == 7777, "unsolicited frame skipped by resync");

    make_read_frame(buf, 1, 5555, &n);
    buf[4] ^= 0x01;
    CHECK(mb_parse(buf, &n, 1, &r) == 1 && r.status == MB_ERR_CRC, "bad CRC reported");

    buf[0] = 1; buf[1] = 0x83; buf[2] = 2;
    crc = mb_crc16(buf, 3);
    buf[3] = (unsigned char)crc; buf[4] = (unsigned char)(crc >> 8);
    n = 5;
    CHECK(mb_parse(buf, &n, 1, &r) == 1 && r.status == MB_ERR_EXCEPTION &&
          r.exception == 2, "exception frame");

    {
        MbRequest q;
        q.slave = 1; q.func = 6; q.addr = 18; q.count = 1;
        n = mb_build(&q, buf, 8);
        CHECK(n == 8 && buf[6] == 0xE8 && buf[7] == 0x0F, "0x06 CRC matches field manual");
        CHECK(mb_parse(buf, &n, 1, &r) == 1 && r.status == MB_STAT_OK &&
              r.values[0] == 1, "0x06 echo parsed");
    }
    {
        MbRequest q = {0};
        unsigned char f[32];
        unsigned char resp[8] = {0x01, 0x10, 0x00, 0x08, 0x00, 0x02, 0xC0, 0x0A};
        q.slave = 1; q.func = 0x10; q.addr = 8;
        q.nvalues = 2; q.values[0] = 12000; q.values[1] = 20000;
        n = mb_build(&q, f, sizeof(f));
        CHECK(n == 13 && f[6] == 4 && f[11] == 0xCE && f[12] == 0xAF,
              "0x10 frame matches real device exchange");
        n = 8;
        CHECK(mb_parse(resp, &n, 1, &r) == 1 && r.status == MB_STAT_OK &&
              r.func == 0x10 && r.values[0] == 2, "0x10 response parsed");
    }
}

static void test_client(void)
{
    MbIo io;
    FakeCtx f;
    MbClient* c;
    MbResult r;
    MbRequest q, q2;
    unsigned int seq[4], s;
    unsigned char late[8];
    int i, n;

    printf("[2] client state machine\n");
    fake_init(&io, &f);
    c = mb_create(&io, 1, 150, 1);

    /* 迟到帧在无等待项时必须丢弃 */
    make_read_frame(late, 1, 60125, &n);
    mb_feed(c, late, n);
    CHECK(!mb_take(c, &r, &s), "late frame discarded when no request pending");

    mb_start(c);
    Sleep(30);

    /* 并发请求：一次提交 4 个，验证串行执行与保序 */
    for (i = 0; i < 4; i++) {
        q.slave = 1; q.func = 3; q.addr = (unsigned short)(i * 2); q.count = 1;
        mb_submit(c, &q, &seq[i]);
    }
    for (i = 0; i < 4; i++) {
        int ok = wait_result(c, 2000, &r, &s);
        CHECK(ok && s == seq[i] && r.status == MB_STAT_OK &&
              r.values[0] == (unsigned short)(i * 2 + 1000), "queued request in order");
    }
    CHECK(f.writes == 4, "one outstanding request per link");

    /* 半包：接收侧每次只给 1 字节 */
    f.split = 1;
    q.slave = 1; q.func = 3; q.addr = 9; q.count = 1;
    mb_submit(c, &q, &seq[0]);
    CHECK(wait_result(c, 2000, &r, &s) && r.values[0] == 1009, "byte-by-byte reassembly");
    f.split = 0;

    /* 超时后迟到：先让链路不回应，超时后注入旧响应 */
    f.silent = 1;
    q2.slave = 1; q2.func = 3; q2.addr = 3; q2.count = 1;
    mb_submit(c, &q2, &seq[0]);
    CHECK(wait_result(c, 3000, &r, &s) && s == seq[0] &&
          r.status == MB_ERR_TIMEOUT, "timeout removes waiting item");
    f.silent = 0;
    make_read_frame(late, 1, 60125, &n);
    {
        int k;
        for (k = 0; k < n; k++) f.pending[k] = late[k];
        f.len = n;
    }
    Sleep(150);
    CHECK(!mb_take(c, &r, &s), "late response never delivered to a later request");

    mb_submit(c, &q2, &seq[1]);
    CHECK(wait_result(c, 2000, &r, &s) && s == seq[1] &&
          r.status == MB_STAT_OK && r.values[0] == 1003,
          "next request matches its own response");

    /* 断线重连 */
    f.fail_open = 1;
    mb_reopen(c);
    Sleep(300);
    CHECK(!mb_online(c), "link reported offline during failure");
    f.fail_open = 0;
    Sleep(400);
    CHECK(mb_online(c), "link reopens automatically");
    mb_submit(c, &q2, &seq[2]);
    CHECK(wait_result(c, 2000, &r, &s) && s == seq[2] &&
          r.status == MB_STAT_OK, "request succeeds after reconnect");

    {
        MbRequest m = {0};
        m.slave = 1; m.func = 0x10; m.addr = 8;
        m.nvalues = 2; m.values[0] = 12000; m.values[1] = 20000;
        mb_submit(c, &m, &seq[3]);
        CHECK(wait_result(c, 2000, &r, &s) && s == seq[3] &&
              r.status == MB_STAT_OK && r.func == 0x10 && r.values[0] == 2,
              "0x10 write completes through client");
    }

    mb_destroy(c);
}

int main(void)
{
    test_parse();
    test_client();
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
