#include "scratch.h"

#include <mkb.h>

namespace scratch {

void init() {}

void tick() {
    mkb::OSReport("cameras: %d, %d, %d, %d, %d\n",
                  mkb::cameras[0].g_smth_with_standstill_counter,
                  mkb::cameras[1].g_smth_with_standstill_counter,
                  mkb::cameras[2].g_smth_with_standstill_counter,
                  mkb::cameras[3].g_smth_with_standstill_counter,
                  mkb::cameras[4].g_smth_with_standstill_counter);
}

void disp() {}

}  // namespace scratch