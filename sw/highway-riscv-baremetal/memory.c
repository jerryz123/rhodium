/* Supplies aligned RVV word-wide bulk memory operations with safe byte heads and tails. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>
#include <riscv_vector.h>

void *memset(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    const unsigned char byte = (unsigned char)value;
    if (count < 64) {
        while (count) {
            size_t vl = __riscv_vsetvl_e8m8(count);
            vuint8m8_t bytes = __riscv_vmv_v_x_u8m8(byte, vl);
            __riscv_vse8_v_u8m8(output, bytes, vl);
            output += vl;
            count -= vl;
        }
        return destination;
    }
    while (count && ((uintptr_t)output & 7)) {
        *output++ = byte;
        --count;
    }
    const uint64_t word = (uint64_t)byte * UINT64_C(0x0101010101010101);
    while (count >= 8) {
        size_t vl = __riscv_vsetvl_e64m8(count / 8);
        vuint64m8_t words = __riscv_vmv_v_x_u64m8(word, vl);
        __riscv_vse64_v_u64m8((uint64_t *)output, words, vl);
        output += vl * 8;
        count -= vl * 8;
    }
    while (count--) *output++ = byte;
    return destination;
}

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;
    if (count < 64 || (((uintptr_t)output ^ (uintptr_t)input) & 7)) {
        while (count) {
            size_t vl = __riscv_vsetvl_e8m8(count);
            vuint8m8_t bytes = __riscv_vle8_v_u8m8(input, vl);
            __riscv_vse8_v_u8m8(output, bytes, vl);
            input += vl;
            output += vl;
            count -= vl;
        }
        return destination;
    }
    while (count && ((uintptr_t)output & 7)) {
        *output++ = *input++;
        --count;
    }
    while (count >= 8) {
        size_t vl = __riscv_vsetvl_e64m8(count / 8);
        vuint64m8_t words = __riscv_vle64_v_u64m8((const uint64_t *)input, vl);
        __riscv_vse64_v_u64m8((uint64_t *)output, words, vl);
        input += vl * 8;
        output += vl * 8;
        count -= vl * 8;
    }
    while (count--) *output++ = *input++;
    return destination;
}
