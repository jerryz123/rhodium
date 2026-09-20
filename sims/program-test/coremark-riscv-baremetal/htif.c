/* Provides freestanding CoreMark formatting, FESVR console output, and completion. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

volatile uint64_t tohost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t fromhost __attribute__((section(".tohost"), aligned(8)));

static void memory_barrier(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static void put_character(char character)
{
    volatile uint64_t request[8] __attribute__((aligned(64)));
    request[0] = 64;
    request[1] = 1;
    request[2] = (uintptr_t)&character;
    request[3] = 1;
    memory_barrier();
    tohost = (uintptr_t)request;
    while (fromhost == 0)
        ;
    fromhost = 0;
    memory_barrier();
}

static int put_string(const char *text)
{
    int count = 0;
    while (*text)
    {
        put_character(*text++);
        count++;
    }
    return count;
}

static int put_unsigned(uint64_t value, unsigned base, unsigned width, char padding)
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
        put_character(padding);
        width--;
        count++;
    }
    while (length)
    {
        put_character(digits[--length]);
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
            put_character(*format++);
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
            put_character('%');
            count++;
            break;
        case 'c':
            put_character((char)va_arg(arguments, int));
            count++;
            break;
        case 's':
            count += put_string(va_arg(arguments, const char *));
            break;
        case 'd':
        {
            int64_t value = longs ? va_arg(arguments, long) : va_arg(arguments, int);
            if (value < 0)
            {
                put_character('-');
                count++;
                value = -value;
            }
            count += put_unsigned((uint64_t)value, 10, width, padding);
            break;
        }
        case 'u':
            count += put_unsigned(longs ? va_arg(arguments, unsigned long)
                                        : va_arg(arguments, unsigned int),
                                  10, width, padding);
            break;
        case 'x':
            count += put_unsigned(longs ? va_arg(arguments, unsigned long)
                                        : va_arg(arguments, unsigned int),
                                  16, width, padding);
            break;
        default:
            put_character('?');
            count++;
            break;
        }
    }
    va_end(arguments);
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
