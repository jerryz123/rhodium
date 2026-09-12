// Decodes private contract programs into ordinary graphs for exact validation.
#pragma once
#include "model.hpp"
#include "../runtime/kernel-format.h"
#include <stdexcept>
namespace rds {
inline Model kernel_model(const Model &parent, const Op &op) {
  rds_kernel_format k;
  if (!rds_kernel_parse(op.imm.data(),op.imm.size(),&k) || k.inputs!=op.args.size())
    throw std::runtime_error("invalid contract program encoding");
  Model child;
  for(auto name:{"registers","memories","writes","reads","assertions","ports","objects","origins","inventory"})
    child.metadata[name]=Json::array();
  child.widths.assign(k.widths,k.widths+k.values);
  for(uint32_t i=0;i<k.inputs;++i){
    if(child.widths[i]!=parent.widths.at(op.args[i]))throw std::runtime_error("contract input width mismatch");
    child.metadata["ports"].push_back(Json::array({0,i,"input"+std::to_string(i)}));
  }
  if(child.widths.back()!=parent.widths.at(op.out))throw std::runtime_error("contract output width mismatch");
  for(uint32_t i=0;i<k.count;++i){const auto &o=k.ops[i];
    child.ops.push_back({o.code,o.out,{k.args+o.args,k.args+o.args+o.nargs},
                        {op.imm.begin()+o.imm,op.imm.begin()+o.imm+o.nimm}});
  }
  return child;
}
}
