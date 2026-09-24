// Runs generated RISC-V litmus threads on physical harts and checks every observed state.
// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include "litmus_case.h"

volatile uint64_t litmus_x __attribute__((aligned(64)));
volatile uint64_t litmus_y __attribute__((aligned(64)));

static volatile uint64_t generation __attribute__((aligned(64)));
static struct { volatile uint64_t phase; uint8_t padding[56]; } reached[LITMUS_HARTS]
    __attribute__((aligned(64)));
static volatile uint64_t observed[LITMUS_OBSERVATIONS] __attribute__((aligned(64)));
static uint64_t histogram[LITMUS_STATES];
static unsigned forbidden;

extern void tohost_exit(uintptr_t code);

static uint64_t cycles(void)
{
  uint64_t value;
  __asm__ volatile("csrr %0, mcycle" : "=r"(value));
  return value;
}

static void synchronize(int hart, unsigned *phase)
{
  unsigned next = *phase + 1;
  __atomic_store_n(&reached[hart].phase, next, __ATOMIC_RELEASE);
  uint64_t started = cycles();
  if (hart == 0) {
    for (unsigned i = 1; i < LITMUS_HARTS; ++i)
      while (__atomic_load_n(&reached[i].phase, __ATOMIC_ACQUIRE) != next)
        if (cycles() - started > 100000)
          goto timeout;
    __atomic_store_n(&generation, next, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&generation, __ATOMIC_ACQUIRE) != next) {
      ;
    }
  }
  *phase = next;
  return;
timeout:
  printf("LITMUS_BARRIER_TIMEOUT phase=%u generation=%lu", next,
         (unsigned long)generation);
  for (unsigned i = 0; i < LITMUS_HARTS; ++i)
    printf(" hart%u=%lu", i, (unsigned long)reached[i].phase);
  printf("\n");
  tohost_exit(2);
}

void thread_entry(int hart, int harts)
{
  if (hart >= LITMUS_HARTS || harts != LITMUS_HARTS)
    for (;;)
      ;
  unsigned phase = 0;
  for (unsigned run = 0; run < LITMUS_RUNS; ++run) {
    synchronize(hart, &phase);
    if (hart == 0) {
      litmus_x = 0;
      litmus_y = 0;
      for (unsigned i = 0; i < LITMUS_OBSERVATIONS; ++i)
        observed[i] = 0;
    }
    synchronize(hart, &phase);
    litmus_threads[hart]((uint64_t *)observed);
    synchronize(hart, &phase);
    if (hart == 0) {
      unsigned state;
      for (state = 0; state < LITMUS_STATES; ++state) {
        unsigned field;
        for (field = 0; field < LITMUS_OBSERVATIONS; ++field)
          if (observed[field] != litmus_allowed[state][field])
            break;
        if (field == LITMUS_OBSERVATIONS) {
          ++histogram[state];
          break;
        }
      }
      if (state == LITMUS_STATES) {
        ++forbidden;
        printf("LITMUS_FORBIDDEN %s run=%u", LITMUS_NAME, run);
        for (unsigned i = 0; i < LITMUS_OBSERVATIONS; ++i)
          printf(" %s=%lu", litmus_fields[i], (unsigned long)observed[i]);
        printf("\n");
      }
    }
    synchronize(hart, &phase);
  }
  if (hart != 0)
    for (;;)
      ;
}

int main(void)
{
  printf("LITMUS_HIST %s", LITMUS_NAME);
  for (unsigned state = 0; state < LITMUS_STATES; ++state)
    if (histogram[state])
      printf(" %u:%lu", state, (unsigned long)histogram[state]);
  printf("\n");
  printf("LITMUS_%s %s runs=%u forbidden=%u\n", forbidden ? "FAIL" : "OK",
         LITMUS_NAME, LITMUS_RUNS, forbidden);
  return forbidden != 0;
}
