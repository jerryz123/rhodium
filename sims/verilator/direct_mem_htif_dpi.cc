// Exposes DirectMemoryHtif through the multi-output DPI-C tick used by Verilator.
#include "direct_mem_htif.h"
#include "direct_mem_htif_dpi.h"

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <vpi_user.h>

namespace {

rhodium::fesvr::DirectMemoryHtif* transport = nullptr;

void clear_outputs(unsigned char* request_valid,
                   unsigned char* request_write,
                   long long* request_address,
                   long long* request_data,
                   unsigned char* request_length,
                   unsigned char* response_ready) {
  *request_valid = 0;
  *request_write = 0;
  *request_address = 0;
  *request_data = 0;
  *request_length = 0;
  *response_ready = 0;
}

}  // namespace

int rhodium_htif_tick(unsigned char reset,
                   unsigned char target_xlen,
                   long long boot_address_register,
                   unsigned char request_ready,
                   unsigned char response_valid,
                   long long response_data,
                   unsigned char response_status,
                   unsigned char* request_valid,
                   unsigned char* request_write,
                   long long* request_address,
                   long long* request_data,
                   unsigned char* request_length,
                   unsigned char* response_ready) {
  if (reset) {
    clear_outputs(request_valid,
                  request_write,
                  request_address,
                  request_data,
                  request_length,
                  response_ready);
    return 0;
  }

  if (transport == nullptr) {
    s_vpi_vlog_info info;
    if (!vpi_get_vlog_info(&info)) {
      std::abort();
    }
    // TestDriver owns these plusargs; do not let FESVR interpret them as an ELF
    // filename. Preserve all other host and target arguments, including order.
    static std::vector<char*> htif_arguments;
    for (int index = 0; index < info.argc; ++index) {
      const std::string_view argument(info.argv[index]);
      if (index != 0 && (argument.starts_with("+rheg-trace=") || argument.starts_with("+max-cycles="))) continue;
      htif_arguments.push_back(info.argv[index]);
    }
    transport = new rhodium::fesvr::DirectMemoryHtif(static_cast<int>(htif_arguments.size()), htif_arguments.data(), target_xlen, static_cast<std::uint64_t>(boot_address_register));
  }

  transport->tick(request_ready != 0,
                  response_valid != 0,
                  static_cast<std::uint64_t>(response_data),
                  response_status);

  const auto& request = transport->request();
  *request_valid = transport->request_valid();
  *request_write = request.write;
  *request_address = static_cast<long long>(request.address);
  *request_data = static_cast<long long>(request.data);
  *request_length = request.length;
  *response_ready = transport->response_ready();
  return static_cast<int>(transport->exit_word());
}
