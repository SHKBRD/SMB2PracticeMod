#pragma once

#include <cstdint>

namespace mkb
{

struct PoolInfo
{
    uint32_t len;
    uint32_t lowFreeIdx;
    uint32_t upperBound;
    uint8_t *statusList;
};

extern "C"
{

int poolAlloc(PoolInfo *info, uint8_t status);

}

}