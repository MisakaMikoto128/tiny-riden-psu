/* plan_test.c - 验证 device 计划执行器的事务帧数。写入当前设定值，幂等只读。 */
#include <stdio.h>
#include "../src/device.h"
#include "../transport/serial_io.h"

int main(void)
{
    MbIo io;
    SerialCtx ctx;
    MbClient* c;
    Device d;
    HANDLE ev;
    DWORD t0, elapsed;
    int frames = 0, done = 0;

    serial_io_init(&io, &ctx, L"COM24", 115200);
    c = mb_create(&io, 1, 600, 1);
    ev = mb_result_event(c);
    mb_start(c);
    dev_init(&d, c);
    dev_tick(&d);

    t0 = GetTickCount();
    while (GetTickCount() - t0 < 4000) {
        WaitForSingleObject(ev, 50);
        while (dev_on_result(&d)) {}
        if (d.snap.valid && d.protectRead) break;
        dev_tick(&d);
    }
    if (!d.snap.valid || !d.protectRead) {
        printf("warmup failed valid=%d protect=%d\n", d.snap.valid, d.protectRead);
        mb_destroy(c);
        return 1;
    }

    dev_set(&d, d.snap.v_set_mv, d.snap.i_set_raw);
    t0 = GetTickCount();
    while (GetTickCount() - t0 < 4000) {
        WaitForSingleObject(ev, 50);
        while (dev_on_result(&d)) frames++;
        if (!dev_busy(&d)) { done = 1; break; }
    }
    elapsed = GetTickCount() - t0;
    printf("set-plan frames=%d elapsed=%lu ms done=%d status=%d\n",
           frames, elapsed, done, d.last);
    printf("reg8=%d reg9=%d output=%d\n",
           d.snap.v_set_mv, d.snap.i_set_raw, d.snap.output);
    mb_destroy(c);
    return (done && frames == 2 && d.last == DEV_OK) ? 0 : 1;
}
