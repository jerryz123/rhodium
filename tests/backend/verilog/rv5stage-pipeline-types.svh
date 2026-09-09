// Defines the RV64 EX/MEM/WB cache protocol used by the direct cache and MMU benches.
typedef struct packed {
  logic [63:0] address;
  logic [3:0] access;
  logic [1:0] width;
  logic unsigned_0;
  logic [63:0] data;
} pipeline_bits_t;
typedef struct packed {logic valid; pipeline_bits_t bits;} pipeline_request_t;
typedef struct packed {
  logic [2:0] outcome;
  logic [2:0] reason;
  logic [63:0] data;
} pipeline_result_t;
typedef struct packed {logic valid; pipeline_result_t bits;} pipeline_response_t;
typedef struct packed {pipeline_request_t request; logic commit;} pipeline_in_t;
typedef struct packed {pipeline_response_t response; logic commit_ready;} pipeline_out_t;
localparam logic [2:0] PIPE_SLOW=0, PIPE_LOAD_HIT=1, PIPE_STORE_HIT=2,
  PIPE_REPLAY=3, PIPE_PAGE_FAULT=4, PIPE_ACCESS_FAULT=5;
