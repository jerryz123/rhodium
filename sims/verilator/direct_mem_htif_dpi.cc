// Exposes DirectMemoryHtif through the multi-output DPI-C tick used by Verilator.
#include "direct_mem_htif.h"
#include "direct_mem_htif_dpi.h"

#include <cstdint>
#include <cstdlib>

#include <vpi_user.h>

namespace {

rhodium::fesvr::DirectMemoryHtif* transport = nullptr;

void clear_outputs(unsigned char* request_valid,
                   unsigned char* request_write,
                   long long* request_address,
                   long long* request_data,
                   unsigned char* request_length,
                   unsigned char* response_ready,
                   unsigned char* start_valid,
                   long long* start_entry) {
  *request_valid = 0;
  *request_write = 0;
  *request_address = 0;
  *request_data = 0;
  *request_length = 0;
  *response_ready = 0;
  *start_valid = 0;
  *start_entry = 0;
}

}  // namespace

int rhodium_htif_tick(unsigned char reset,
                   unsigned char target_xlen,
                   unsigned char request_ready,
                   unsigned char response_valid,
                   long long response_data,
                   unsigned char response_status,
                   unsigned char start_ready,
                   unsigned char* request_valid,
                   unsigned char* request_write,
                   long long* request_address,
                   long long* request_data,
                   unsigned char* request_length,
                   unsigned char* response_ready,
                   unsigned char* start_valid,
                   long long* start_entry) {
  if (reset) {
    clear_outputs(request_valid,
                  request_write,
                  request_address,
                  request_data,
                  request_length,
                  response_ready,
                  start_valid,
                  start_entry);
    return 0;
  }

  if (transport == nullptr) {
    s_vpi_vlog_info info;
    if (!vpi_get_vlog_info(&info)) {
      std::abort();
    }
    transport = new rhodium::fesvr::DirectMemoryHtif(info.argc, info.argv, target_xlen);
  }

  transport->tick(request_ready != 0,
                  response_valid != 0,
                  static_cast<std::uint64_t>(response_data),
                  response_status,
                  start_ready != 0);

  const auto& request = transport->request();
  *request_valid = transport->request_valid();
  *request_write = request.write;
  *request_address = static_cast<long long>(request.address);
  *request_data = static_cast<long long>(request.data);
  *request_length = request.length;
  *response_ready = transport->response_ready();
  *start_valid = transport->start_valid();
  *start_entry = static_cast<long long>(transport->start_entry());
  return static_cast<int>(transport->exit_word());
}
