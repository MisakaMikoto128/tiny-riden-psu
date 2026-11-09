/* rate_test.c - 测量连续请求的帧率。完成一帧立即发下一帧，无固定节拍。只读。 */
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
    HANDLE ev;
    DWORD t0, elapsed;
    int frames = 0, errors = 0;

    serial_io_init(&io, &ctx, L"COM24", 115200);
    c = mb_create(&io, 1, 600, 1);
    ev = mb_result_event(c);
    mb_start(c);

    q.slave = 1; q.func = 3; q.addr = 0; q.count = 21;
    if (!mb_submit(c, &q, &seq)) { printf("submit failed\n"); mb_destroy(c); return 1; }

    t0 = GetTickCount();
    while (GetTickCount() - t0 < 5000) {
        WaitForSingleObject(ev, 50);
        while (mb_take(c, &r, &seq)) {
            if (r.status == MB_STAT_OK && r.count == 21) frames++;
            else errors++;
            mb_submit(c, &q, &seq);
        }
    }
    elapsed = GetTickCount() - t0;
    {
        int rate = frames * 100000 / (int)elapsed;
        printf("frames=%d errors=%d elapsed=%lu ms rate=%d.%02d Hz\n",
               frames, errors, elapsed, rate / 100, rate % 100);
    }
    printf("avg frame time=%lu ms\n", elapsed / (frames ? frames : 1));
    mb_destroy(c);
    return frames > 0 ? 0 : 1;
}
