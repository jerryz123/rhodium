// Declares the stable wide-data DPI ABI for the CHI memory model.
#pragma once

#include <svdpi.h>

extern "C" unsigned char rhodium_chi_memory_init(
    int model_id, long long capacity, long long base_address);

extern "C" unsigned char rhodium_chi_memory_access(
    int model_id,
    long long capacity,
    unsigned char beat_bytes,
    unsigned char write,
    long long address,
    const svBitVecVal* write_data,
    long long write_mask,
    svBitVecVal* read_data);
