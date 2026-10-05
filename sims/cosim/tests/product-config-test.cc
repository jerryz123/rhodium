// Initializes an exported product without weakening its architectural configuration.
// SPDX-License-Identifier: Apache-2.0
#include "sail-reference.h"
#include <jsoncons/json.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace rhodium::cosim;

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("expected Sail configuration and manifest");
    std::ifstream config_file(argv[1]), manifest_file(argv[2]);
    if (!config_file || !manifest_file) throw std::runtime_error("cannot open configuration artifacts");
    const std::string config{std::istreambuf_iterator<char>(config_file), {}};
    const auto manifest = jsoncons::json::parse(manifest_file);
    std::vector<MemoryRange> backing;
    for (const auto& range : manifest["private_memory"].array_range())
      backing.push_back({range["address"].as<std::uint64_t>(), range["size"].as<std::uint64_t>()});
    const auto reset = manifest["reset_pc"].as<std::uint64_t>();
    const auto model_config = jsoncons::json::parse(config);
    for (std::size_t i = 0; i != model_config["memory"]["regions"].size(); ++i) {
      if (std::stoull(model_config["memory"]["regions"][i]["size"]["value"].as<std::string>(), nullptr, 0) >= 4096) continue;
      for (bool executable : {false, true}) {
        auto invalid = model_config;
        auto& attrs = invalid["memory"]["regions"][i]["attributes"];
        if (executable) attrs["executable"] = true;
        else attrs["misaligned_exceptions"]["load_store"] = jsoncons::json::parse(R"({"None":null})");
        bool rejected = false;
        try { SailReference invalid_reference(invalid.to_string(), reset, backing); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("unsafe subpage PMA must be rejected");
      }
    }
    SailReference reference(config, reset, backing);
    if (reference.pc() != reset) throw std::runtime_error("incorrect reset PC");
    // A tiny ROM probe validates the configuration/backing boundary, not firmware boot.
    const std::array<std::uint8_t, 4> addi = {0x93, 0x00, 0x70, 0x00};
    reference.load(reset, addi);
    const auto step = reference.step();
    if (!step.retired || step.trap || reference.integer_register(1) != 7)
      throw std::runtime_error("product ROM probe did not retire");
    for (const auto& region : model_config["memory"]["regions"].array_range()) {
      const auto size = std::stoull(region["size"]["value"].as<std::string>(), nullptr, 0);
      if (size >= 4096) continue;
      const auto address = std::stoull(region["base"]["value"].as<std::string>(), nullptr, 0);
      // Current Mini/Simple UART: an eight-byte device, not a whole mapped page.
      if (size != 8 || (address & 4095) || address >= 0x80000000)
        throw std::runtime_error("extend the subpage device probe for this aperture");
      const std::array<std::uint32_t, 3> words = {
          static_cast<std::uint32_t>(address) | 0x137, // lui x2, device base
          0x00010183,                               // lb x3, 0(x2)
          0x00810183};                              // lb x3, 8(x2): unmapped
      std::vector<std::uint8_t> bytes;
      for (auto word : words)
        for (unsigned i = 0; i != 4; ++i) bytes.push_back(word >> (8 * i));
      reference.load(reset + 4, bytes);
      if (!reference.step().retired) throw std::runtime_error("device-address setup failed");
      StepInputs input;
      input.device_reads.push_back({address, {0x5a}});
      if (!reference.step(input).retired || reference.integer_register(3) != 0x5a)
        throw std::runtime_error("subpage device read was not replayed");
      const auto fault = reference.step();
      if (!fault.trap || fault.trap->interrupt || fault.trap->cause != 5 || fault.retired)
        throw std::runtime_error("neighboring device address must produce a load access fault");
    }
    std::cout << manifest["product"].as<std::string>() << ": initialized and retired ROM probe\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
