# Checks assertion sampling, occurrence diagnostics, and synthesis-mode omission.
# SPDX-License-Identifier: Apache-2.0
import subprocess
from support import run

def assertion_bench(direct):
    """One executable selects passing or intentionally failing edge scenarios."""
    names = ("clock", "reset", "guard", "condition", "unnamed_guard", "unnamed_condition",
             "sample_guard", "next_state", "expected_state")
    lines = ["module assertion_tb;", "integer scenario = 0;", "logic armed = 0;",
             "logic first_reset_base = 1;", "logic late_reset = 0;"]
    for side in ("first", "second"):
        for name in names:
            if side == "first" and name == "reset":
                lines.append("wire first_reset = scenario == 6 ? (first_reset_base | late_reset) : "
                             "scenario == 7 ? (first_reset_base & ~late_reset) : first_reset_base;")
            else:
                lines.append(f"logic {side}_{name} = 0;")
        lines.append(f"wire {side}_state;")
    connections = [f".{side}_{name}({side}_{name})" for side in ("first", "second") for name in names + ("state",)]
    lines.append(f"AssertionChecks dut({', '.join(connections)});")
    if direct:
        lines.append("AssertionNames names(.check(first_clock), .reset(first_reset));")
    lines += ["always @(posedge first_clock) if (armed) late_reset <= 1;",
              "initial begin", '  if ($value$plusargs("CASE=%d", scenario)) begin end']
    lines += ["  first_reset_base = 1; second_reset = 1;",
              "  first_guard = 1; second_guard = 1;",
              "  first_unnamed_guard = 1; second_unnamed_guard = 1;",
              "  first_sample_guard = 1; second_sample_guard = 1;",
              "  first_expected_state = 1; second_expected_state = 1;",
              "  first_next_state = 1; second_next_state = 1;",
              "  #2; first_clock = 1; second_clock = 1; #1;",
              '  if (first_state !== 0 || second_state !== 0) $fatal(1, "reset state");',
              "  first_clock = 0; second_clock = 0; #1;",
              # First child's checks are guarded off; second's are reset off.
              "  first_reset_base = 0; first_guard = 0; first_unnamed_guard = 0; first_sample_guard = 0;",
              "  #1; first_clock = 1; #1; second_clock = 1; #1;",
              '  if (first_state !== 1 || second_state !== 0) $fatal(1, "independent state");',
              "  first_clock = 0; second_clock = 0; #1;",
              "  second_reset = 0; first_guard = 1; first_unnamed_guard = 1; first_sample_guard = 1;",
              "  first_condition = 1; second_condition = 1;",
              "  first_unnamed_condition = 1; second_unnamed_condition = 1;",
              "  first_expected_state = 1; second_expected_state = 0;",
              "  first_next_state = 0; second_next_state = 1;",
              # Assertions see old state even though both registers change.
              "  #1; first_clock = 1; #1; second_clock = 1; #1;",
              '  if (first_state !== 0 || second_state !== 1) $fatal(1, "updated state");',
              # False conditions while high and at falling edges must not fire.
              "  first_condition = 0; second_condition = 0;",
              "  first_unnamed_condition = 0; second_unnamed_condition = 0;",
              "  #1; first_clock = 0; second_clock = 0; #1;",
              "  first_condition = 1; second_condition = 1;",
              "  first_unnamed_condition = 1; second_unnamed_condition = 1;",
              "  first_expected_state = 0; second_expected_state = 1;",
              "  first_next_state = 1; second_next_state = 0;",
              "  case (scenario)",
              "    1: first_condition = 0;",
              "    2: second_condition = 0;",
              "    3: first_unnamed_condition = 0;",
              "    4: first_expected_state = 1;",
              "    5: second_unnamed_condition = 0;",
              "    6: first_condition = 0;",
              "    7: begin first_reset_base = 1; first_condition = 0; end",
              "    default: begin end", "  endcase",
              "  armed = 1;", '  $display("ARM assertion case %0d", scenario);',
              "  #1; first_clock = 1; second_clock = 1; #1;",
              '  $display("PASS assertion case %0d", scenario);', "  $finish;", "end", "endmodule"]
    return "\n".join(lines) + "\n"


def simulate_assertions(work, name, source, direct, verilator):
    """A failure must occur after arming, in the intended occurrence/check.
    Synthesis builds must omit collateral without affecting functional state.
    """
    sv, tb = work / f"{name}-assertions.sv", work / f"{name}-assertions_tb.sv"
    sv.write_text(source)
    tb.write_text(assertion_bench(direct))
    for synthesis in (False, True):
        mode = "synthesis" if synthesis else "checked"
        build = work / f"{name}-assertions-{mode}"
        run([verilator, "--binary", "--timing", "--assert", "--build-jobs", "2",
             *(["-DSYNTHESIS"] if synthesis else []), "--top-module", "assertion_tb",
             "--Mdir", build, "-o", "sim", sv, tb], work / f"{name}-assertions-{mode}-build.log")
        for scenario in range(8):
            result = subprocess.run([str(build / "sim"), f"+CASE={scenario}"], cwd=work,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            output = result.stdout
            log = work / f"{name}-assertions-{mode}-{scenario}.log"
            log.write_text(output)
            passing = synthesis or scenario in (0, 7)
            if passing:
                valid = result.returncode == 0 and f"PASS assertion case {scenario}" in output
            else:
                side = "second" if scenario in (2, 5) else "first"
                label = "sample_check" if scenario == 4 else "named_check" if scenario in (1, 2, 6) else None
                valid = (result.returncode != 0 and f"ARM assertion case {scenario}" in output
                         and "PASS assertion case" not in output and "assertion failed" in output.lower()
                         and f"dut.{side}" in output and (label is None or label in output))
            if not valid:
                raise RuntimeError(f"unexpected assertion outcome; see {log}")
    print(f"{name}: assertion checks passed (2 passing, 6 expected failures, 8 synthesis runs)", flush=True)
