/* Declares the PTY-backed UART DPI ABI and model observations. */
// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Polls one model and transfers bytes only outside reset. Ready consumes at most
   one queued PTY byte through the output pointers; received UART bytes, including
   framing errors, reach the PTY. Returns zero on success or 1--4 for host errors. */
extern "C" char uart_pty_tick(
    int model_id,
    unsigned char reset,
    unsigned char uart_to_pty_valid,
    char uart_to_pty_byte,
    unsigned char uart_to_pty_framing_error,
    unsigned char pty_to_uart_ready,
    unsigned char* pty_to_uart_valid,
    char* pty_to_uart_byte);

/* Opens the model on first use and returns its stable slave path, or null on error. */
extern "C" const char* uart_pty_path(int model_id);
/* Observes accumulated framing errors; creates model state but does not open its PTY. */
extern "C" int uart_pty_framing_errors(int model_id);
