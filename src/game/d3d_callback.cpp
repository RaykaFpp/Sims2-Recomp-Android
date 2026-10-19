// D3DDevice_InsertCallback(Flags, pCallback, Context) at 0x275750.
//
// On the console this writes a callback token (0x00081D8C, with the 0x00040100 /
// 0x00040110 fence words) into the push buffer; the D3D interrupt handler runs
// pCallback(Context) once the GPU reaches it. The SDK renderer has no
// push-buffer callback path - it walks the buffer for draws only and drops
// those methods ("Walk drops method 1D8C / 0110 / 0100") - so the callback
// never ran. The render pump's kick task (sub_001FC0E0 op 3) inserts the
// callback sub_001FA8C0 this way, and the engine thread then waits forever on
// the semaphore that callback releases: one movie frame presented, then every
// guest thread idle-waits and no further task is ever enqueued.
//
// The host renderer has no GPU timeline the guest can be behind, so the
// callback is run immediately: by the time this returns, everything queued
// before it has already been handed to the render thread.
#include "xdk/hle.h"

HLE_STDCALL(D3DDevice_InsertCallback, 3) {
    const uint32_t callback = a[1], context = a[2];
    if (callback != 0) ::kernel::CallGuest(ctx, callback, {context});
    return 0;
}
