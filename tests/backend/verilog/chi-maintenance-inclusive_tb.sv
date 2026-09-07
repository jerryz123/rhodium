// Runs the common maintenance contract against the inclusive Home.
module chi_maintenance_inclusive_tb;
  `define MAINTENANCE_HOME CHIInclusiveHNF
  localparam bit INCLUSIVE = 1;
  `include "tests/backend/verilog/chi-maintenance-home-body.svh"
endmodule
