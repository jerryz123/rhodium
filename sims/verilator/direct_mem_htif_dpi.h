// Declares the stable C ABI between generated simulation RTL and Verilator.
#pragma once

extern "C" int rhodium_htif_tick(unsigned char reset,
                              unsigned char target_xlen,
                              long long boot_address_register,
                              unsigned char request_ready,
                              unsigned char response_valid,
                              long long response_data,
                              unsigned char response_status,
                              unsigned char* request_valid,
                              unsigned char* request_write,
                              long long* request_address,
                              long long* request_data,
                              unsigned char* request_length,
                              unsigned char* response_ready);
