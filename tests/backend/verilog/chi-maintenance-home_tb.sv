// Runs the common maintenance contract against the storage-free Home.
module chi_maintenance_home_tb;
  `define MAINTENANCE_HOME CHIHNF
  localparam bit INCLUSIVE = 0;
  `include "tests/backend/verilog/chi-maintenance-home-body.svh"
endmodule
