#include "freeze.h"

#include <mkb.h>
#include "patch.h"
#include "pref.h"

namespace freeze {

static patch::Tramp<decltype(&mkb::event_info_tick)> s_event_info_tick_tramp;

void init() {
    patch::hook_function(s_event_info_tick_tramp, mkb::event_info_tick, []() {
        s_event_info_tick_tramp.dest();
        if (pref::get_freeze_timer()) {
            mkb::mode_info.stage_time_frames_remaining = mkb::mode_info.stage_time_limit;
        }
    });
}

}  // namespace freeze