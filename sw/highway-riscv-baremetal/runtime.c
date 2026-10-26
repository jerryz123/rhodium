/* Supplies Highway's Newlib heap/syscall boundary and single-owner HTIF console/completion. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/times.h>

volatile uint64_t tohost __attribute__((section(".tohost"), aligned(8)));
volatile uint64_t fromhost __attribute__((section(".tohost"), aligned(8)));
static volatile uint64_t console_request[8] __attribute__((aligned(64)));
extern char __heap_start[], __heap_end[];
static char *heap_break;

int _write(int fd, const void *data, size_t length)
{
    if (fd != 1 && fd != 2) {
        errno = EBADF;
        return -1;
    }
    console_request[0] = 64;
    console_request[1] = fd;
    console_request[2] = (uintptr_t)data;
    console_request[3] = length;
    __asm__ volatile("fence rw, rw" ::: "memory");
    tohost = (uintptr_t)console_request;
    while (fromhost == 0)
        ;
    fromhost = 0;
    __asm__ volatile("fence rw, rw" ::: "memory");
    return (int)console_request[0];
}

void *_sbrk(ptrdiff_t increment)
{
    if (!heap_break)
        heap_break = __heap_start;
    if ((increment >= 0 && (uintptr_t)increment > (uintptr_t)(__heap_end - heap_break)) ||
        (increment < 0 && (uintptr_t)(-(increment + 1)) + 1 > (uintptr_t)(heap_break - __heap_start))) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *previous = heap_break;
    heap_break += increment;
    return previous;
}

int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _read(int fd, void *data, size_t length)
{
    (void)fd; (void)data; (void)length;
    errno = EBADF;
    return -1;
}
long _lseek(int fd, long offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    errno = ESPIPE;
    return -1;
}
int _fstat(int fd, struct stat *status)
{
    if (fd != 1 && fd != 2) { errno = EBADF; return -1; }
    status->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) { return fd == 1 || fd == 2; }
int _getpid(void) { return 1; }
int _kill(int pid, int signal) { (void)pid; (void)signal; errno = EINVAL; return -1; }
clock_t _times(struct tms *status) { (void)status; errno = ENOSYS; return (clock_t)-1; }

void __attribute__((noreturn)) _exit(int status)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
    tohost = ((uint64_t)(uint32_t)status << 1) | 1;
    while (1)
        ;
}

void __attribute__((noreturn)) highway_exit(int status)
{
    if (fflush(NULL) != 0)
        status = 1;
    exit(status);
}

void __attribute__((noreturn)) highway_trap(uintptr_t cause, uintptr_t pc)
{
    char message[96];
    int length = snprintf(message, sizeof(message), "Highway trap cause=%lu pc=0x%lx\n",
                          (unsigned long)cause, (unsigned long)pc);
    if (length > 0 && (size_t)length < sizeof(message))
        _write(2, message, (size_t)length);
    _exit(1);
}

/* Newlib's constructor runner calls the target's optional initialization hook. */
void _init(void) {}
void *__dso_handle;
