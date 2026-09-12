// Defines the standalone optimizer's typed graph and serialized interchange
// boundary.
#pragma once
#include "../runtime/kernel-format.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
namespace rds {
using Json = nlohmann::json;
using Big = boost::multiprecision::cpp_int;
using Id = uint32_t;
constexpr Id none = UINT32_MAX;
struct Op {
  uint32_t code;
  Id out;
  std::vector<Id> args;
  std::vector<uint64_t> imm;
};
struct Model {
  std::vector<uint32_t> widths;
  std::vector<Op> ops;
  Json metadata;
  static Model read(const std::string &path, bool semantic_optimize = true);
  Json json() const;
  void validate() const;
  void write_binary(const std::string &path) const;
};
Json lower_semantic(Json, bool optimize);
void validate_semantic_structure(const Model &);
void remap_semantic_structure(Model &, const std::function<Id(Id)> &);
Big mask(uint32_t width);
Big number(const std::vector<uint64_t> &words);
std::vector<uint64_t> words(Big value, uint32_t width);
void share_decoders(Model &);
void specialize_decoders(Model &);
void canonicalize_bit_relations(Model &);
void share_matchers(Model &);
void simplify_matcher_masks(Model &);
void fold_stationary_matchers(Model &);
void reuse_matcher_grants(Model &);
void split_matcher_columns(Model &);
void lift_indexed_datapaths(Model &);
void pool_payloads(Model &,unsigned);
void exchange_payload_handles(Model &, bool pack_handles = false, bool fuse_state = false, bool isolate_readers = false, bool bitmap_slots = false);
void share_payload_lifetimes(Model &,bool direct_storage=false,bool packed_counters=false,bool split_control=false,bool phase_index=false);
void share_tlb_queries(Model &);
void optimize_body(Model &);
void release_body(Model &);
void prune_stateless_inputs(Model &);
void fold_idle_fifos(Model &);
void narrow_fifo_payloads(Model &);
void cache_fifo_derived(Model &);
void fuse_register_updates(Model &);
void recover_words(Model &);
void regroup_bits(Model &);
void regroup_selector_columns(Model &);
void pack_matchers(Model &, bool recover_rows = true);
void plan_flow_regions(Model &, bool reorder = false);
void lift_contracts(Model &);
void bundle_contract_controls(Model &);
void optimize_contract_programs(Model &);
void share_handshake_rows(Model &);
void guard_contract_programs(Model &);
void compact_graph(Model &);
void replicate(Model &, uint64_t bytes, uint64_t work, uint64_t cone);
void split_snapshot_prefixes(Model &, uint32_t prefix_work);
} // namespace rds
