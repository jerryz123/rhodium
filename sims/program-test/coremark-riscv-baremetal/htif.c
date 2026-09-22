/* Provides freestanding CoreMark formatting, FESVR console output, and completion. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

volatile uint64_t tohost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t fromhost __attribute__((section(".tohost"), aligned(8)));

enum
{
    CONSOLE_BUFFER_SIZE = 256
};

static volatile uint64_t console_request[8] __attribute__((aligned(64)));
static char console_buffer[CONSOLE_BUFFER_SIZE] __attribute__((aligned(64)));
static size_t console_buffer_length;

static void memory_barrier(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static void write_console(const char *data, size_t length)
{
    console_request[0] = 64;
    console_request[1] = 1;
    console_request[2] = (uintptr_t)data;
    console_request[3] = length;
    memory_barrier();
    tohost = (uintptr_t)console_request;
    while (fromhost == 0)
        ;
    fromhost = 0;
    memory_barrier();
}

static void flush_console(void)
{
    if (console_buffer_length == 0)
        return;
    write_console(console_buffer, console_buffer_length);
    console_buffer_length = 0;
}

static void emit_character(char character)
{
    console_buffer[console_buffer_length++] = character;
    if (console_buffer_length == CONSOLE_BUFFER_SIZE)
        flush_console();
}

static int emit_string(const char *text)
{
    int count = 0;
    while (*text)
    {
        emit_character(*text++);
        count++;
    }
    return count;
}

static int emit_unsigned(uint64_t value, unsigned base, unsigned width, char padding)
{
    char digits[32];
    unsigned length = 0;
    int count = 0;
    do
    {
        unsigned digit = value % base;
        digits[length++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
        value /= base;
    } while (value);
    while (width > length)
    {
        emit_character(padding);
        width--;
        count++;
    }
    while (length)
    {
        emit_character(digits[--length]);
        count++;
    }
    return count;
}

int ee_printf(const char *format, ...)
{
    va_list arguments;
    int count = 0;
    va_start(arguments, format);
    while (*format)
    {
        if (*format != '%')
        {
            emit_character(*format++);
            count++;
            continue;
        }
        format++;
        char padding = ' ';
        unsigned width = 0;
        unsigned longs = 0;
        if (*format == '0')
        {
            padding = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (unsigned)(*format++ - '0');
        while (*format == 'l')
        {
            longs++;
            format++;
        }
        switch (*format++)
        {
        case '%':
            emit_character('%');
            count++;
            break;
        case 'c':
            emit_character((char)va_arg(arguments, int));
            count++;
            break;
        case 's':
            count += emit_string(va_arg(arguments, const char *));
            break;
        case 'd':
        {
            int64_t value = longs ? va_arg(arguments, long) : va_arg(arguments, int);
            if (value < 0)
            {
                emit_character('-');
                count++;
                value = -value;
            }
            count += emit_unsigned((uint64_t)value, 10, width, padding);
            break;
        }
        case 'u':
            count += emit_unsigned(longs ? va_arg(arguments, unsigned long)
                                        : va_arg(arguments, unsigned int),
                                  10, width, padding);
            break;
        case 'x':
            count += emit_unsigned(longs ? va_arg(arguments, unsigned long)
                                        : va_arg(arguments, unsigned int),
                                  16, width, padding);
            break;
        default:
            emit_character('?');
            count++;
            break;
        }
    }
    va_end(arguments);
    flush_console();
    return count;
}

void __attribute__((noreturn)) rhodium_exit(int status)
{
    memory_barrier();
    tohost = ((uint64_t)(uint32_t)status << 1) | 1;
    while (1)
        ;
}

void __attribute__((noreturn)) rhodium_trap(uint64_t cause, uint64_t program_counter)
{
    ee_printf("CoreMark trap: mcause=0x%lx mepc=0x%lx\n", cause, program_counter);
    rhodium_exit(1);
}
