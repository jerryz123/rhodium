/* Reports that Linux reached PID 1 in userspace, then requests SBI-backed poweroff. */
/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <sys/reboot.h>
#include <unistd.h>

/* Complete short writes without stdio buffering, including interrupted syscalls. */
static int write_all(int fd, const char *text, unsigned long size)
{
    while (size) {
        ssize_t written = write(fd, text, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return -1;
        text += written;
        size -= (unsigned long)written;
    }
    return 0;
}

int main(void)
{
    static const char success[] = "RHODIUM-LINUX: userspace boot complete; powering off\n";
    static const char failure[] = "RHODIUM-LINUX: FAIL poweroff returned\n";
    int console = open("/dev/console", O_WRONLY | O_NOCTTY);
    if (console < 0 || getpid() != 1 ||
        write_all(console, success, sizeof(success) - 1))
        return 1; /* Exiting PID 1 produces a kernel panic, never a passing smoke. */
    reboot(RB_POWER_OFF);
    write_all(console, failure, sizeof(failure) - 1);
    return 1;
}
