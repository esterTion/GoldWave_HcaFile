#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

int32_t askTrackNo(int32_t* valueOut, int32_t trackCount, int32_t* durations);
int32_t askCriKey(uint32_t* valueOut1, uint32_t* valueOut2, uint32_t prevK1, uint32_t prevK2);

#ifdef __cplusplus
}
#endif