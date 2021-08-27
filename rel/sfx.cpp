#include "sfx.h"

#include <mkb.h>
#include "patch.h"
#include "pref.h"

namespace sfx {

static void (*s_g_fade_track_volume_tramp)(u32 volume, u8 param_2);

void init() {
    //    s_g_fade_track_volume_tramp =
    //        patch::hook_function(mkb::g_fade_track_volume, [](u32 volume, u8 param_2) {
    ////            s_g_fade_track_volume_tramp(volume, param_2);
    ////            mkb::OSReport("g_fade_track_volume(%d, %d)\n", volume, param_2);
    //        });
}

void tick() {
    // bool enabled = pref::get_sfx_only();
}

}  // namespace sfx