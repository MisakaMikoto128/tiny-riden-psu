/* live_test.c - 真串口链路验证：读一次寄存器块并打印原始值。只读。 */
#include <stdio.h>
#include "../transport/modbus_rtu.h"
#include "../transport/serial_io.h"

int main(void)
{
    MbIo io;
    SerialCtx ctx;
    MbClient* c;
    MbRequest q;
    MbResult r;
    unsigned int seq;
    int t, i, ok = 0;

    serial_io_init(&io, &ctx, L"COM24", 115200);
    c = mb_create(&io, 1, 600, 2);
    mb_start(c);

    q.slave = 1; q.func = 3; q.addr = 0; q.count = 21;
    if (!mb_submit(c, &q, &seq)) { printf("submit failed\n"); mb_destroy(c); return 1; }

    t = 0;
    while (t < 4000) {
        if (mb_take(c, &r, &seq)) { ok = 1; break; }
        Sleep(20);
        t += 20;
    }
    if (!ok) {
        printf("NO RESULT within 4000 ms, online=%d\n", mb_online(c));
        mb_destroy(c);
        return 1;
    }
    printf("status=%d func=%u count=%u seq=%u online=%d\n",
           r.status, r.func, r.count, seq, mb_online(c));
    for (i = 0; i < r.count; i++) printf("%u ", r.values[i]);
    printf("\n");
    mb_destroy(c);
    return r.status == MB_STAT_OK ? 0 : 1;
}
