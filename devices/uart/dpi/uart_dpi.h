/* Declares the UART DPI byte transport, selectable host output, and model observations. */
// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Selects output-only logging before a model opens its host transport. PATH is
   truncated on open; "-" duplicates stdout. Returns 1 for invalid/repeated
   configuration or 2 for open failure. Unconfigured models use a raw PTY. */
extern "C" int uart_set_log_file(int model_id, const char* path);

/* Polls one model and transfers bytes only outside reset. Ready consumes at most
   one queued PTY byte through the output pointers; received UART bytes, including
   framing errors, reach the selected output. Log mode supplies no input.
   Returns zero on success or 1--4 for host errors. */
extern "C" char uart_pty_tick(
    int model_id,
    unsigned char reset,
    unsigned char uart_to_pty_valid,
    char uart_to_pty_byte,
    unsigned char uart_to_pty_framing_error,
    unsigned char pty_to_uart_ready,
    unsigned char* pty_to_uart_valid,
    char* pty_to_uart_byte);

/* Opens a PTY on first use; returns its stable path, or null in log mode/on error. */
extern "C" const char* uart_pty_path(int model_id);
/* Observes accumulated framing errors; creates model state but does not open its PTY. */
extern "C" int uart_pty_framing_errors(int model_id);
