# Build and test entry points for Rhodium's Rhombus and CIRCT-based toolchain.
# SPDX-License-Identifier: Apache-2.0

.PHONY: sram-test
.PHONY: setup-verilator
export PATH := $(CURDIR)/.tools/verilator/bin:$(PATH)
.PHONY: event-test
.PHONY: event-runtime-test
.PHONY: check-license-headers
.PHONY: check-license-headers-staged
.PHONY: test host-test host-checks support-annotation-test devicetree-test check-boundaries check-example-verilog check-parameter-annotations parameter-annotation-test racket-cache-test clean-racket-cache install-git-hooks analysis-test frontend-test std-test flow-test diagram-test backend-test formal-test formal-differential-test unit-test lop-test rfpl-test rfpl-unit-test rfpl-circt-test noc-test riscv-test device-test chi-test soc-test hardfloat-test hardfloat-host-test hardfloat-circt-test rv5stage-host-test rv5stage-test riscv-udb-config emacs-test circt-test circt-verify-test verilator-test circt-full-test verilog-golden-test update-verilog-goldens setup-circt print-racket-compile-sources ci-plan-test ci-host-foundation-test ci-host-backend-test ci-host-models-test ci-host-protocols-test ci-host-cores-test ci-host-socs-test ci-host-hygiene-test ci-circt-language-test ci-circt-std-test ci-circt-protocols-test ci-circt-core-components-test ci-circt-core-execution-test ci-circt-core-vector-test ci-circt-core-vector-functional-test ci-circt-core-vector-configurations-test ci-circt-core-memory-test ci-circt-core-cache-test examples examples-rhodium examples-clocking examples-std examples-noc examples-lop examples-rfpl examples-riscv examples-chi examples-cores examples-formal examples-rv5stage
.PHONY: ci-circt-core-vector-functional-1-test ci-circt-core-vector-functional-2-test

RISCV_UDB_CONFIGURATION ?=
RISCV_UDB_OUTPUT ?= /tmp/rhodium-udb/$(RISCV_UDB_CONFIGURATION).yaml

CORE_TESTS := $(sort $(wildcard rhodium/core/tests/*-test.rhm))
ANALYSIS_TESTS := $(sort $(wildcard rhodium/analysis/tests/*-test.rhm))
SUPPORT_ANNOTATION_TESTS := $(sort $(wildcard support/tests/*-test.rhm))
DEVICETREE_TESTS := $(sort $(wildcard devicetree/tests/*-test.rhm))
FRONTEND_TESTS := $(sort $(wildcard rhodium/frontend/tests/*-test.rhm))
STD_TESTS := $(sort $(wildcard rhodium/std/tests/*-test.rhm))
FLOW_TESTS := $(sort $(wildcard flow/tests/*-test.rhm))
EVENT_TESTS := $(sort $(wildcard rhodium/event/tests/*-test.rhm))
DIAGRAM_TESTS := $(sort $(wildcard rhodium/diagram/tests/*-test.rhm))
BACKEND_TESTS := $(sort $(wildcard rhodium/backend/tests/*-test.rhm))
FORMAL_TESTS := rhodium/formal/tests/suite.rkt
LOP_FRONTEND_TESTS := $(sort $(wildcard rhodium/frontend/tests/*equivalence-test.rhm))
LOP_BACKEND_TESTS := $(sort $(wildcard rhodium/backend/tests/*equivalence-test.rhm))
NOC_TESTS := $(sort $(shell find noc/tests noc/rtl/tests -type f -name '*-test.rhm'))
RISCV_TESTS := $(sort $(shell find riscv/tests riscv/rtl/tests -type f -name '*-test.rhm'))
DEVICE_TESTS := $(sort $(wildcard devices/tests/*-test.rhm))
CHI_TESTS := $(sort $(wildcard chi/tests/*-test.rhm))
SOC_TESTS := $(sort $(wildcard socs/tests/*-test.rhm))
HARDFLOAT_TESTS := $(sort $(wildcard hardfloat/tests/*-test.rhm))
PROCESSOR_TESTS := $(sort $(shell find cores/tests cores/riscv/tests cores/rv5stage/tests cores/spike/tests -type f -name '*-test.rhm'))
RFPL_TESTS := $(sort $(wildcard rfpl/tests/*-test.rhm))
RFPL_EXAMPLES := $(sort $(wildcard examples/rfpl/*.rfpl))
RHODIUM_EXAMPLES := $(sort $(shell find examples/rtl -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
CLOCKING_EXAMPLES := $(sort $(shell find examples/clocking -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
STD_EXAMPLES := $(sort $(shell find examples/std -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
NOC_EXAMPLES := $(sort $(shell find examples/noc -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
LOP_EXAMPLES := $(sort $(shell find examples/lop -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
RFPL_LOGICAL_EXAMPLES := $(sort $(shell find examples/rfpl -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
RISCV_EXAMPLES := $(sort $(shell find examples/riscv -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
CHI_EXAMPLES := $(sort $(shell find examples/chi -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
CORE_EXAMPLES := $(sort $(shell find examples/cores -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
FORMAL_EXAMPLES := $(sort $(shell find examples/formal -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
RV5STAGE_EXAMPLES := $(sort $(shell find examples/rv5stage -type f \( -name '*.rhm' -o -name '*.rhdl' \)))
EXAMPLES := $(sort $(shell find examples -path examples/formal -prune -o -type f \( -name '*.rhm' -o -name '*.rhdl' \) -print) $(RFPL_EXAMPLES))
RACKET_COMPILE_SOURCES := $(sort \
  $(SUPPORT_ANNOTATION_TESTS) $(CORE_TESTS) $(ANALYSIS_TESTS) $(FRONTEND_TESTS) \
  $(STD_TESTS) $(FLOW_TESTS) $(EVENT_TESTS) $(DIAGRAM_TESTS) $(BACKEND_TESTS) \
  $(RFPL_TESTS) $(DEVICETREE_TESTS) devicetree/tests/write-fixture.rhm $(NOC_TESTS) $(RISCV_TESTS) \
  $(DEVICE_TESTS) $(CHI_TESTS) $(SOC_TESTS) $(HARDFLOAT_TESTS) $(PROCESSOR_TESTS) $(EXAMPLES) \
  socs/tests/write-device-trees.rhm \
  tools/write-riscv-udb-config.rhm \
  $(shell find . -type f -path '*/tests/circt/emit-*.rhm' -print) \
  $(wildcard sims/tests/*.rhm sims/tests/*.rhdl) \
  $(wildcard sims/emit-*.rhm) \
  $(wildcard sims/program-test/*.rhm) \
  $(wildcard sims/arch-test/*.rhm) \
  tools/testing/circt/load-example.rkt tools/testing/run-negative.rkt \
  noc/tests/language/run-negative.rkt tools/check-parameter-annotations.rkt)

print-racket-compile-sources:
	@printf '%s\n' $(RACKET_COMPILE_SOURCES)

check-boundaries:
	bash tools/check-boundaries.sh
	bash rfpl/check-boundaries.sh
	bash noc/check-boundaries.sh
	bash riscv/check-boundaries.sh
	bash chi/check-boundaries.sh
	bash chi/tests/check-boundaries.sh
	bash hardfloat/check-boundaries.sh
	bash cores/check-boundaries.sh
	bash socs/check-boundaries.sh
	bash socs/tests/check-boundaries.sh

check-example-verilog:
	bash tools/check-example-verilog.sh

check-license-headers:
	bash tools/check-license-headers.sh

check-license-headers-staged:
	bash tools/check-license-headers.sh --cached

check-parameter-annotations:
	tools/run-racket.sh tools/check-parameter-annotations.rkt --named-only --reject-any --files-from tools/parameter-annotation-scope.txt

parameter-annotation-test:
	tools/run-racket-tests.sh tools/check-parameter-annotations.rkt

racket-cache-test:
	bash tools/testing/racket-build-cache-test.sh

clean-racket-cache:
	tools/racket-build-cache.sh clean

install-git-hooks:
	git config core.hooksPath .githooks

support-annotation-test:
	tools/run-racket-tests.sh $(SUPPORT_ANNOTATION_TESTS)

devicetree-test:
	tools/run-racket-tests.sh $(DEVICETREE_TESTS)
	bash devicetree/tests/run-dtc.sh

frontend-test: check-boundaries
	tools/run-racket-tests.sh $(CORE_TESTS) $(ANALYSIS_TESTS) $(FRONTEND_TESTS)
	bash rhodium/frontend/tests/run-negative.sh

analysis-test: check-boundaries
	tools/run-racket-tests.sh $(ANALYSIS_TESTS)

std-test: check-boundaries
	tools/run-racket-tests.sh $(STD_TESTS)

flow-test: check-boundaries
	tools/run-racket-tests.sh $(FLOW_TESTS)

diagram-test: check-boundaries
	tools/run-racket-tests.sh $(DIAGRAM_TESTS)

event-test: check-boundaries
	tools/run-racket-tests.sh $(EVENT_TESTS)

event-runtime-test: check-boundaries
	FIXTURES="event-runtime event-pipeline event-window event-frontend event-home event-subordinate event-fesvr event-feedback event-branching event-partial event-elastic event-queue event-arbiter event-crossbar event-demux event-atomic-fork event-broadcast event-join event-stall event-offer event-offer-register event-parents event-retained" bash tools/testing/circt/run.sh

backend-test: check-boundaries
	tools/run-racket-tests.sh $(BACKEND_TESTS)

formal-test: check-boundaries
	@if ! env PLTCOLLECTS=$(CURDIR): tools/run-racket.sh -e '(require rosette) (unless (sat? (solve (assert #t))) (error '\''formal-test "Rosette solver probe failed"))'; then \
		echo 'formal-test requires Rosette 4.0 and its Z3 4.8.8 solver; see rhodium/formal/README.md' >&2; \
		exit 1; \
	fi; \
	tools/run-racket-tests.sh $(FORMAL_TESTS)

formal-differential-test: check-boundaries
	bash rhodium/formal/tests/run-differential.sh

unit-test: frontend-test std-test flow-test event-test diagram-test backend-test

lop-test: check-boundaries
	tools/run-racket-tests.sh $(LOP_FRONTEND_TESTS) $(LOP_BACKEND_TESTS)

rfpl-unit-test:
	bash rfpl/check-boundaries.sh
	tools/run-racket-tests.sh $(RFPL_TESTS)
	bash rfpl/tests/run-negative.sh

rfpl-test: rfpl-unit-test examples-rfpl

rfpl-circt-test:
	bash rfpl/tests/run-circt.sh

noc-test:
	bash noc/check-boundaries.sh
	tools/run-racket-tests.sh $(NOC_TESTS)
	bash noc/tests/language/run-negative.sh

riscv-test:
	bash riscv/check-boundaries.sh
	tools/run-racket-tests.sh $(RISCV_TESTS)
	python3 -m unittest discover -s riscv/tests -p 'test_*.py'

device-test: check-boundaries
	tools/run-racket-tests.sh $(DEVICE_TESTS)
	bash devices/tests/run-uart-dpi-cpp.sh

chi-test: check-boundaries
	tools/run-racket-tests.sh $(CHI_TESTS)
	bash chi/tests/run-negative.sh

soc-test: check-boundaries
	tools/run-racket-tests.sh $(SOC_TESTS) sims/tests/product-test.rhm
	bash socs/tests/run-device-tree.sh

hardfloat-host-test: check-boundaries
	tools/run-racket-tests.sh $(HARDFLOAT_TESTS)

hardfloat-circt-test: check-boundaries
	bash hardfloat/tests/run-circt.sh

hardfloat-test: hardfloat-host-test hardfloat-circt-test

emacs-test:
	emacs -Q --batch -L tools/emacs -l tools/emacs/tests/rhodium-mode-test.el -f ert-run-tests-batch-and-exit

rv5stage-host-test: check-boundaries
	tools/run-racket-tests.sh $(PROCESSOR_TESTS)

riscv-udb-config:
	@test -n "$(RISCV_UDB_CONFIGURATION)" || (echo "RISCV_UDB_CONFIGURATION requires an explicit shape-core-isa key" >&2; exit 2)
	@set -e; \
	mkdir -p "$(dir $(RISCV_UDB_OUTPUT))"; \
	env PLTCOLLECTS="$(CURDIR)": tools/run-racket.sh -S "$(CURDIR)" \
	  tools/write-riscv-udb-config.rhm "$(RISCV_UDB_CONFIGURATION)" "$(RISCV_UDB_OUTPUT)"

rv5stage-test: rv5stage-host-test
	FIXTURES='rv32i-alu rv64i-alu rv64i-alu-integrated load-store load-store-rv32-word bit-manip bit-manip-rv32 iterative-multiplier iterative-divider scoreboard riscv-compressed riscv-atomic rv5stage-fp-register-file rv5stage-fp-pipeline rv5stage-register-file rv5stage-csr rv5stage-zihpm-rv32 rv5stage-zihpm-rv64 rv5stage-btb rv5stage-fetch rv5stage-fetch-prediction rv5stage-fetch-throughput rv5stage-branch-prediction rv5stage-core rv5stage-load-hit rv5stage-zcb rv5stage-mop rv5stage-core-rv32f rv5stage-core-rv64d rv5stage-data-fault rv5stage-mmu-replay rv5stage-interrupt rv5stage-pause rv5stage-instruction-memory-router rv5stage-memory-router rv5stage-uncached rv5stage-io-mshr rv5stage-io-boot rv5stage-multiply rv5stage-divide rv5stage-icache rv5stage-dcache rv5stage-dcache-rv32' bash tools/testing/circt/run.sh

circt-test: check-example-verilog
	bash tools/testing/circt/run.sh

circt-verify-test: check-example-verilog
	bash tools/testing/circt/run.sh --verify-only

verilator-test: check-example-verilog
	bash tools/testing/circt/run.sh --simulate-only

circt-full-test: check-example-verilog
	bash tools/testing/circt/run.sh --full

ci-circt-language-test:
	bash tools/check-example-verilog.sh examples/rtl examples/lop
	bash tools/testing/circt/run.sh --group language

ci-circt-std-test:
	bash tools/check-example-verilog.sh examples/std
	bash tools/testing/circt/run.sh --group std

ci-circt-protocols-test:
	bash tools/check-example-verilog.sh examples/noc examples/chi
	bash tools/testing/circt/run.sh --group protocols

ci-circt-core-components-test:
	bash tools/check-example-verilog.sh examples/riscv examples/cores
	bash tools/testing/circt/run.sh --group cores-components

ci-circt-core-execution-test:
	bash tools/testing/circt/run.sh --group cores-execution

ci-circt-core-vector-test:
	bash tools/testing/circt/run.sh --group cores-vector

ci-circt-core-vector-functional-test:
	bash tools/testing/circt/run.sh --group cores-vector-functional

ci-circt-core-vector-functional-1-test:
	bash tools/testing/circt/run.sh --group cores-vector-functional-1

ci-circt-core-vector-functional-2-test:
	bash tools/testing/circt/run.sh --group cores-vector-functional-2

ci-circt-core-vector-configurations-test:
	bash tools/testing/circt/run.sh --group cores-vector-configurations

ci-circt-core-memory-test:
	bash tools/testing/circt/run.sh --group cores-memory

ci-circt-core-cache-test:
	bash tools/testing/circt/run.sh --group cores-cache

verilog-golden-test: check-example-verilog
	bash tools/testing/circt/run.sh --golden-only

update-verilog-goldens:
	bash tools/check-example-verilog.sh --allow-empty
	bash tools/testing/circt/run.sh --update-goldens

host-checks: check-license-headers check-parameter-annotations support-annotation-test devicetree-test unit-test rfpl-unit-test noc-test riscv-test device-test chi-test soc-test hardfloat-host-test rv5stage-host-test

ci-host-foundation-test: support-annotation-test frontend-test lop-test

ci-host-backend-test: backend-test

ci-host-models-test: devicetree-test noc-test riscv-test hardfloat-host-test

ci-host-protocols-test: rfpl-unit-test device-test chi-test

ci-host-cores-test: rv5stage-host-test

ci-host-socs-test: soc-test

ci-plan-test:
	python3 -m unittest tools.ci.test_plan

ci-host-hygiene-test: ci-plan-test check-boundaries check-example-verilog check-license-headers check-parameter-annotations parameter-annotation-test racket-cache-test

host-test: host-checks examples

test: host-test circt-test rfpl-circt-test hardfloat-circt-test

setup-circt:
	bash tools/install-circt.sh

setup-verilator:
	bash tools/install-verilator.sh

sram-test:
	$(MAKE) -C sram test

examples: check-example-verilog
	tools/run-racket-tests.sh $(EXAMPLES)

examples-rhodium:
	bash tools/check-example-verilog.sh examples/rtl
	tools/run-racket-tests.sh $(RHODIUM_EXAMPLES)

examples-clocking:
	bash tools/check-example-verilog.sh examples/clocking
	tools/run-racket-tests.sh $(CLOCKING_EXAMPLES)

examples-std:
	bash tools/check-example-verilog.sh examples/std
	tools/run-racket-tests.sh $(STD_EXAMPLES)

examples-noc:
	bash tools/check-example-verilog.sh examples/noc
	tools/run-racket-tests.sh $(NOC_EXAMPLES)

examples-lop:
	bash tools/check-example-verilog.sh examples/lop
	tools/run-racket-tests.sh $(LOP_EXAMPLES)

examples-rfpl:
	bash tools/check-example-verilog.sh examples/rfpl
	tools/run-racket-tests.sh $(RFPL_LOGICAL_EXAMPLES) $(RFPL_EXAMPLES)

examples-riscv:
	bash tools/check-example-verilog.sh examples/riscv
	tools/run-racket-tests.sh $(RISCV_EXAMPLES)

examples-chi:
	bash tools/check-example-verilog.sh examples/chi
	tools/run-racket-tests.sh $(CHI_EXAMPLES)

examples-cores:
	bash tools/check-example-verilog.sh examples/cores
	tools/run-racket-tests.sh $(CORE_EXAMPLES)

examples-formal: check-boundaries
	@if ! env PLTCOLLECTS=$(CURDIR): tools/run-racket.sh -e '(require rosette) (unless (sat? (solve (assert #t))) (error '\''examples-formal "Rosette solver probe failed"))'; then echo 'examples-formal requires Rosette 4.0 and its Z3 4.8.8 solver; see rhodium/formal/README.md' >&2; exit 1; fi; tools/run-racket-tests.sh $(FORMAL_EXAMPLES)

examples-rv5stage:
	bash tools/check-example-verilog.sh examples/rv5stage
	tools/run-racket-tests.sh $(RV5STAGE_EXAMPLES)
