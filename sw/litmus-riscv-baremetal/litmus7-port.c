// Runs litmus7's fixed thread pool on the selected physical harts without an OS.
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

#ifndef LITMUS_HARTS
#error LITMUS_HARTS must name the fixed litmus7 worker count
#endif
#ifndef LITMUS_CLOCK_HZ
#error LITMUS_CLOCK_HZ must name the target clock frequency
#endif

extern void tohost_exit(uintptr_t code);
extern void printstr(const char *text);
extern volatile uint64_t tohost;
extern volatile uint64_t fromhost;

typedef struct {
  f_t *function;
  void *argument;
  void *result;
  volatile unsigned ready;
  volatile unsigned done;
  char padding[32];
} worker_t;

static worker_t workers[LITMUS_HARTS] __attribute__((aligned(64)));
static unsigned launched;

typedef struct {
  void *identity;
  unsigned participants;
  volatile uint64_t generation;
  volatile uint64_t arrived[LITMUS_HARTS];
} barrier_t;

static barrier_t barriers[LITMUS_HARTS + 1] __attribute__((aligned(64)));
static unsigned barrier_count;

void litmus_baremetal_barrier_init(void *identity, int participants)
{
  if (participants < 1 || participants > LITMUS_HARTS || barrier_count >= LITMUS_HARTS + 1)
    fatal("invalid litmus7 barrier topology");
  barrier_t *barrier = &barriers[barrier_count++];
  barrier->identity = identity;
  barrier->participants = participants;
  barrier->generation = 0;
  for (unsigned hart = 0; hart < LITMUS_HARTS; ++hart)
    barrier->arrived[hart] = 0;
}

void litmus_baremetal_barrier_wait(void *identity)
{
  unsigned hart;
  __asm__ volatile("csrr %0, mhartid" : "=r"(hart));
  barrier_t *barrier = 0;
  for (unsigned index = 0; index < barrier_count; ++index)
    if (barriers[index].identity == identity) {
      barrier = &barriers[index];
      break;
    }
  if (!barrier || hart >= LITMUS_HARTS)
    fatal("unknown litmus7 barrier");
  uint64_t next = __atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE) + 1;
  __atomic_store_n(&barrier->arrived[hart], next, __ATOMIC_RELEASE);
  for (;;) {
    unsigned count = 0;
    for (unsigned other = 0; other < LITMUS_HARTS; ++other)
      count += __atomic_load_n(&barrier->arrived[other], __ATOMIC_ACQUIRE) == next;
    if (count >= barrier->participants) {
      __atomic_store_n(&barrier->generation, next, __ATOMIC_RELEASE);
      return;
    }
    if (__atomic_load_n(&barrier->generation, __ATOMIC_ACQUIRE) >= next)
      return;
  }
}

void thread_entry(int hart, int harts)
{
  if (harts != LITMUS_HARTS || hart >= LITMUS_HARTS)
    tohost_exit(2);
  if (hart == 0)
    return;
  while (!__atomic_load_n(&workers[hart].ready, __ATOMIC_ACQUIRE))
    ;
  workers[hart].result = workers[hart].function(workers[hart].argument);
  __atomic_store_n(&workers[hart].done, 1, __ATOMIC_RELEASE);
  for (;;)
    ;
}

void launch(pthread_t *thread, f_t *function, void *argument)
{
  unsigned hart = launched++;
  if (hart >= LITMUS_HARTS)
    fatal("litmus7 launched too many workers");
  workers[hart].function = function;
  workers[hart].argument = argument;
  *thread = hart;
  if (hart != 0)
    __atomic_store_n(&workers[hart].ready, 1, __ATOMIC_RELEASE);
}

void *join(pthread_t *thread)
{
  unsigned hart = *thread;
  if (hart >= launched || hart >= LITMUS_HARTS)
    fatal("litmus7 joined an unknown worker");
  if (hart == 0 && !workers[0].done) {
    workers[0].result = workers[0].function(workers[0].argument);
    workers[0].done = 1;
  }
  while (!__atomic_load_n(&workers[hart].done, __ATOMIC_ACQUIRE))
    ;
  return workers[hart].result;
}

void fatal(const char *message)
{
  printstr("LITMUS_HARNESS_ERROR ");
  printstr(message);
  printstr("\n");
  tohost_exit(2);
}

void errexit(const char *message, int error)
{
  (void)error;
  fatal(message);
}

int max(int left, int right) { return left > right ? left : right; }

void *do_align(void *pointer, size_t alignment)
{
  uintptr_t value = (uintptr_t)pointer;
  return (void *)((value + alignment - 1) / alignment * alignment);
}

tsc_t timeofday(void)
{
  tsc_t value;
  __asm__ volatile("csrr %0, mcycle" : "=r"(value));
  return value * 1000000 / LITMUS_CLOCK_HZ;
}

double tsc_ratio(tsc_t left, tsc_t right) { return (double)left / (double)right; }
double tsc_millions(tsc_t value) { return (double)value / 1000000.0; }

char **parse_opt(int argc, char **argv, opt_t *defaults, opt_t *options)
{
  *options = *defaults;
  if (argc != 1 || !argv || !argv[0])
    fatal("litmus7 command-line options are unavailable on bare metal");
  return argv + 1;
}

void parse_param(char *program, parse_param_t *params, int count, char **argv)
{
  (void)program;
  (void)params;
  (void)count;
  if (*argv)
    fatal("litmus7 parameters are unavailable on bare metal");
}

void interval_init(int *values, size_t count)
{
  for (size_t index = 0; index < count; ++index)
    values[index] = index;
}

void interval_shuffle(st_t *seed, int *values, size_t count)
{
  for (size_t index = 0; index + 1 < count; ++index) {
    size_t other = index + rand_k(seed, count - index);
    int temp = values[index];
    values[index] = values[other];
    values[other] = temp;
  }
}

static char output_line[1024];
static unsigned output_length;
static unsigned histogram_lines;
static int saw_histogram;
static int output_finished;

static int parse_histogram_header(const char *line, unsigned *count)
{
  static const char prefix[] = "Histogram (";
  if (strncmp(line, prefix, sizeof(prefix) - 1) != 0)
    return 0;
  const char *cursor = line + sizeof(prefix) - 1;
  if (*cursor < '0' || *cursor > '9')
    return 0;
  unsigned value = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    unsigned digit = (unsigned)(*cursor++ - '0');
    if (value > (UINT_MAX - digit) / 10)
      return 0;
    value = value * 10 + digit;
  }
  if (strcmp(cursor, " states)") != 0)
    return 0;
  *count = value;
  return 1;
}

static void emit_buffer(const char *buffer, unsigned length)
{
  volatile uint64_t request[8] __attribute__((aligned(64))) = {
    64, 1, (uintptr_t)buffer, length
  };
  __sync_synchronize();
  tohost = (uintptr_t)request;
  while (fromhost == 0)
    ;
  fromhost = 0;
  __sync_synchronize();
  if (request[0] != length)
    fatal("litmus7 host output failed");
}

static void emit_line(void)
{
  output_line[output_length] = 0;
  int keep = 0;
  if (!saw_histogram && strncmp(output_line, "Test ", 5) == 0)
    keep = 1;
  else if (!saw_histogram && parse_histogram_header(output_line, &histogram_lines)) {
    keep = 1;
    saw_histogram = 1;
    if (!histogram_lines)
      output_finished = 1;
  } else if (saw_histogram && histogram_lines) {
    keep = 1;
    if (!--histogram_lines)
      output_finished = 1;
  }
  if (keep) {
    output_line[output_length] = '\n';
    emit_buffer(output_line, output_length + 1);
  }
  output_length = 0;
}

void emit_char(FILE *stream, char character)
{
  (void)stream;
  if (output_finished)
    return;
  if (character == '\n') {
    emit_line();
    return;
  }
  if (output_length + 1 >= sizeof(output_line))
    fatal("litmus7 output line exceeds bare-metal buffer");
  output_line[output_length++] = character;
}

int fprintf(FILE *stream, const char *format, ...)
{
  (void)stream;
  (void)format;
  fatal("unexpected litmus7 diagnostic");
  return -1;
}
