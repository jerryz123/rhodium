// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void step() {
  tick_model();
  CHECK(test_cycles < 400);
}
template <class Predicate> void until(Predicate predicate) {
  eval();
  for (int i = 0; i < 40 && !predicate(); ++i)
    step();
  CHECK(predicate());
}
template <class Input, class Output>
void send(Input &input, const Output &output) {
  input.pvalid = 1;
  until([&] { return output.pready; });
  step();
  input.pvalid = 0;
  eval();
}
int main() {
  return run_test([] {
    reset = 1;
    time_egress_0_in.pready = time_egress_1_in.pready = 1;
    interrupt_egress_0_in.pready = external_interrupt_egress_1_in.pready = 1;
    for (int i = 0; i < 3; ++i)
      step();
    reset = 0;
    interrupt_ingress_in.pbits.pindex = 1;
    interrupt_ingress_in.pbits.pvalue.pmachine_usoftware = 1;
    send(interrupt_ingress_in, interrupt_ingress_out);
    until([] { return interrupt_egress_1_out.pvalid; });
    CHECK(interrupt_egress_1_out.pbits.pmachine_usoftware &&
          !interrupt_egress_1_out.pbits.pmachine_utimer);
    CHECK(!interrupt_egress_0_out.pvalid);
    interrupt_egress_1_in.pready = 1;
    step();
    external_interrupt_ingress_in.pbits.pindex = 0;
    external_interrupt_ingress_in.pbits.pvalue.pmachine_uexternal = 1;
    external_interrupt_ingress_in.pbits.pvalue.psupervisor_uexternal = 1;
    send(external_interrupt_ingress_in, external_interrupt_ingress_out);
    until([] { return external_interrupt_egress_0_out.pvalid; });
    CHECK(external_interrupt_egress_0_out.pbits.pmachine_uexternal &&
          external_interrupt_egress_0_out.pbits.psupervisor_uexternal);
    CHECK(!external_interrupt_egress_1_out.pvalid);
    // A blocked PLIC path must leave the independently routed ACLINT path live.
    interrupt_ingress_in.pbits.pvalue.pmachine_utimer = 1;
    send(interrupt_ingress_in, interrupt_ingress_out);
    until([] { return interrupt_egress_1_out.pvalid; });
    CHECK(interrupt_egress_1_out.pbits.pmachine_usoftware &&
          interrupt_egress_1_out.pbits.pmachine_utimer);
    for (int i = 0; i < 3; ++i) {
      step();
      CHECK(external_interrupt_egress_0_out.pvalid &&
            external_interrupt_egress_0_out.pbits.pmachine_uexternal &&
            external_interrupt_egress_0_out.pbits.psupervisor_uexternal);
    }
    external_interrupt_egress_0_in.pready = 1;
    step();
    time_egress_0_in.pready = time_egress_1_in.pready = 0;
    time_ingress_in.pbits.phart = 0;
    time_ingress_in.pbits.pflit.pfirst = 1;
    time_ingress_in.pbits.pflit.plast = 0;
    time_ingress_in.pbits.pflit.ppayload = 0xcafe;
    send(time_ingress_in, time_ingress_out);
    until([] { return time_egress_0_out.pvalid; });
    CHECK(time_egress_0_out.pbits.pfirst && !time_egress_0_out.pbits.plast &&
          time_egress_0_out.pbits.ppayload == 0xcafe);
    CHECK(!time_egress_1_out.pvalid);
    time_egress_0_in.pready = time_egress_1_in.pready = 1;
    step();
    interrupt_egress_1_in.pready = 0;
    interrupt_ingress_in.pbits.pvalue.pmachine_usoftware = 0;
    send(interrupt_ingress_in, interrupt_ingress_out);
    until([] { return interrupt_egress_1_out.pvalid; });
    for (int i = 0; i < 4; ++i) {
      CHECK(interrupt_egress_1_out.pvalid &&
            !interrupt_egress_1_out.pbits.pmachine_usoftware &&
            interrupt_egress_1_out.pbits.pmachine_utimer);
      step();
    }
    interrupt_egress_1_in.pready = 1;
    step();
  });
}
