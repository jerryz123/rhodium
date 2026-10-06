// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "config_utils.h"
inline std::string vector_configuration() {
  auto config=jsoncons::json::parse(get_default_config());
  config["platform"]["clint"]["supported"]=false;
  config["platform"]["simple_interrupt_generator"]["supported"]=false;
  // The fixture has no guest state or supervisor timer compare. Do not inherit
  // forced delegation/status bits from Sail's broad default platform.
  for(auto& extension:config["extensions"].object_range()) {
    const std::string name(extension.key());
    if((name=="H" || name.starts_with("Sh") || name=="Sstc") && extension.value().contains("supported"))
      extension.value()["supported"]=false;
  }
  auto& delegatable=config["base"]["medeleg"]["delegatable_bits"]["value"];
  delegatable=std::to_string(std::stoull(delegatable.as<std::string>(),nullptr,0) & ~((1ULL<<10)|(15ULL<<20)));
  config["extensions"]["V"]["support_level"]="Integer";
  config["extensions"]["V"]["vlen_exp"]=7;
  config["extensions"]["V"]["elen_exp"]=6;
  config["extensions"]["Zvfhmin"]["supported"]=false;
  config["extensions"]["Zvfh"]["supported"]=false;
  config["extensions"]["Zvfbfmin"]["supported"]=false;
  config["extensions"]["Zvfbfwma"]["supported"]=false;
  config["extensions"]["V"]["vstart"]["zero_required"]["arith"]=false;
  config["extensions"]["V"]["reserved_behavior"]["vstart_out_of_bounds"]="Vstart_Ignore";
  config["extensions"]["V"]["vl_use_ceil"]=false;
  return config.to_string();
}
