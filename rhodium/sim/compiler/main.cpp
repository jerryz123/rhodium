// Runs reproducible offline optimization with per-pass timings and optional
// snapshots.
#include "model.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
void write_json(const std::string &path, const rds::Json &j) {
  std::ofstream f(path);
  if (!f || !(f << j.dump() << '\n'))
    throw std::runtime_error("cannot write " + path);
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::string input, output, report, timing, dump;
    bool semantic_optimize = true;
    bool stateless_inputs = false, idle_fifos = false;
    bool narrow_fifos = false, fifo_derived = false;
    bool selector_columns = false;
    bool bit_relations = false;
    bool flow_regions = false, flow_order = false, contracts = false, eager_contracts = false, bundle_controls = false, optimize_programs = false;
    bool optimize = true, regroup = true, release = false, update_fusion = true, packed_matchers = true;
    bool matcher_masks = false, matcher_grants = false, matcher_columns = false, indexed_datapaths = false, stationary_matchers = false;
    bool cache_controls = false, guard_programs = false, handshake_rows = false, specialize_decode = false;
    uint64_t bytes = 0, work = 0, cone = 16;
    uint64_t snapshot_prefix_work = 0;
    unsigned payload_pool_limit = 0;
    bool payload_exchange = false, packed_payload_handles = false, fused_transport_state = false, isolate_payload_readers = false, bitmap_payload_slots = false;
    bool payload_lifetimes = false, payload_lifetime_sram = false, payload_lifetime_counters = false, payload_lifetime_split = false, payload_lifetime_phase = false;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      auto value = [&] {
        if (++i >= argc)
          throw std::runtime_error("missing value for " + arg);
        return std::string(argv[i]);
      };
      auto amount = [&] {
        auto text = value();
        uint64_t number = 0;
        auto result =
            std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
          throw std::runtime_error("expected unsigned integer for " + arg);
        return number;
      };
      if (arg == "--input")
        input = value();
      else if (arg == "--output")
        output = value();
      else if (arg == "--report")
        report = value();
      else if (arg == "--timing")
        timing = value();
      else if (arg == "--isolate-payload-readers")
        payload_exchange = isolate_payload_readers = true;
      else if (arg == "--bitmap-payload-slots")
        payload_exchange = bitmap_payload_slots = true;
      else if (arg == "--dump-passes")
        dump = value();
      else if (arg == "--payload-pool-size") {
        auto n=amount();if(n<2||n>31)throw std::runtime_error("payload pool group limit must be 2..31");payload_pool_limit=n;
      }
      else if (arg == "--share-payload-lifetimes")
        payload_lifetimes = true;
      else if (arg == "--exchange-payload-handles")
        payload_exchange = true;
      else if (arg == "--pack-payload-handles")
        payload_exchange = packed_payload_handles = true;
      else if (arg == "--fuse-transport-state")
        payload_exchange = packed_payload_handles = fused_transport_state = true;
      else if (arg == "--payload-lifetime-sram")
        payload_lifetimes = payload_lifetime_sram = true;
      else if (arg == "--payload-lifetime-packed-counters")
        payload_lifetimes = payload_lifetime_sram = payload_lifetime_counters = true;
      else if (arg == "--payload-lifetime-split-control")
        payload_lifetimes = payload_lifetime_sram = payload_lifetime_split = true;
      else if (arg == "--payload-lifetime-phase-index")
        payload_lifetimes = payload_lifetime_sram = payload_lifetime_phase = true;
      else if (arg == "--no-semantic-cse")
        semantic_optimize = false;
      else if (arg == "--no-optimize")
        optimize = false;
      else if (arg == "--release")
        release = true;
      else if (arg == "--prune-stateless-inputs")
        stateless_inputs = true;
      else if (arg == "--fold-idle-fifos")
        idle_fifos = true;
      else if (arg == "--narrow-fifo-payloads")
        narrow_fifos = true;
      else if (arg == "--cache-fifo-derived")
        fifo_derived = true;
      else if (arg == "--regroup-selector-columns")
        selector_columns = true;
      else if (arg == "--canonicalize-bit-relations")
        bit_relations = true;
      else if (arg == "--flow-regions")
        flow_regions = true;
      else if (arg == "--lift-contracts")
        contracts = true;
      else if (arg == "--bundle-contract-controls")
        bundle_controls = true;
      else if (arg == "--optimize-contract-programs")
        optimize_programs = true;
      else if (arg == "--share-handshake-rows")
        handshake_rows = true;
      else if (arg == "--specialize-decoders")
        specialize_decode = true;
      else if (arg == "--fold-stationary-matchers")
        stationary_matchers = true;
      else if (arg == "--simplify-matcher-masks")
        matcher_masks = true;
      else if (arg == "--reuse-matcher-grants")
        matcher_grants = true;
      else if (arg == "--split-matcher-columns")
        matcher_columns = true;
      else if (arg == "--lift-indexed-datapaths")
        indexed_datapaths = true;
      else if (arg == "--cache-contract-controls")
        cache_controls = true;
      else if (arg == "--guard-contract-programs")
        guard_programs = true;
      else if (arg == "--eager-contracts")
        contracts = eager_contracts = true;
      else if (arg == "--flow-order")
        flow_regions = flow_order = true;
      else if (arg == "--no-update-fusion")
        update_fusion = false;
      else if (arg == "--no-regroup")
        regroup = false;
      else if (arg == "--no-packed-matchers")
        packed_matchers = false;
      else if (arg == "--replicate-bytes")
        bytes = amount();
      else if (arg == "--replicate-work")
        work = amount();
      else if (arg == "--cone-work")
        cone = amount();
      else if (arg == "--snapshot-prefix-work") {
        snapshot_prefix_work = amount();
        if (!snapshot_prefix_work || snapshot_prefix_work > 256)
          throw std::runtime_error("snapshot prefix work must be 1..256");
      }
      else if (arg == "--help") {
        std::cout << "rhodium-opt --input extracted.json [--output model.rsim] "
                     "[--report optimized.json] [--timing timing.json] "
                     "[--dump-passes directory] [--no-optimize] [--no-regroup] [--no-packed-matchers] [--release] [--no-update-fusion] [--no-semantic-cse] [--prune-stateless-inputs] [--fold-idle-fifos] [--narrow-fifo-payloads] "
                     "[--cache-fifo-derived] [--regroup-selector-columns] [--flow-regions] [--flow-order] [--lift-contracts] [--eager-contracts] [--bundle-contract-controls] [--share-handshake-rows] [--specialize-decoders] [--optimize-contract-programs] [--simplify-matcher-masks] [--fold-stationary-matchers] [--cache-contract-controls] [--guard-contract-programs] [--snapshot-prefix-work N] "
                     "[--reuse-matcher-grants] [--split-matcher-columns] [--lift-indexed-datapaths] [--payload-pool-size 2..31] [--exchange-payload-handles] [--pack-payload-handles] [--fuse-transport-state] [--isolate-payload-readers] [--bitmap-payload-slots] [--share-payload-lifetimes] [--payload-lifetime-sram] [--payload-lifetime-packed-counters] [--payload-lifetime-split-control] [--payload-lifetime-phase-index] [--replicate-bytes N --replicate-work N --cone-work N]\n";
        return 0;
      } else
        throw std::runtime_error("unknown option " + arg);
    }
    if (input.empty() || (output.empty() && report.empty()) || !cone)
      throw std::runtime_error("specify --input and --output or --report; cone "
                               "work must be positive");
    if (!dump.empty())
      std::filesystem::create_directories(dump);
    rds::Json times = rds::Json::array();
    rds::Model m;
    auto stage = [&](const std::string &name,
                     const std::function<void()> &action) {
      auto before = m.ops.size();
      auto start = std::chrono::steady_clock::now();
      action();
      auto stop = std::chrono::steady_clock::now();
      times.push_back(
          {{"pass", name},
           {"seconds", std::chrono::duration<double>(stop - start).count()},
           {"operations_before", before},
           {"operations_after", m.ops.size()}});
    };
    stage("read_validate", [&] { m = rds::Model::read(input, semantic_optimize); });
    auto pass = [&](const std::string &name, auto fn) {
      stage(name, [&] { fn(m); });
      m.validate();
      if (!dump.empty())
        write_json(dump + "/" + std::to_string(times.size() - 1) + "-" + name +
                       ".json",
                   m.json());
    };
    if (optimize) {
      pass("share_decoders", rds::share_decoders);
      pass("share_matchers", rds::share_matchers);
      if(std::any_of(m.metadata["objects"].begin(),m.metadata["objects"].end(),
                     [](const rds::Json&o){return o[0]==11;}))
        pass("share_tlb_queries",rds::share_tlb_queries);
      pass("simplify_1", rds::optimize_body);
      pass("recover_words", rds::recover_words);
      if (regroup)
        pass("regroup_bits", rds::regroup_bits);
      pass("simplify_2", rds::optimize_body);
    }
    if (specialize_decode)
      pass("specialize_decoders", rds::specialize_decoders);
    if (stateless_inputs)
      pass("prune_stateless_inputs", rds::prune_stateless_inputs);
    if (bit_relations)
      pass("canonicalize_bit_relations", rds::canonicalize_bit_relations);
    if (idle_fifos)
      pass("fold_idle_fifos", rds::fold_idle_fifos);
    if (release)
      pass("release_liveness", rds::release_body);
    if (narrow_fifos)
      pass("narrow_fifo_payloads", rds::narrow_fifo_payloads);
    if (fifo_derived)
      pass("cache_fifo_derived", rds::cache_fifo_derived);
    if (selector_columns)
      pass("regroup_selector_columns", rds::regroup_selector_columns);
    if (release && optimize && update_fusion)
      pass("fuse_register_updates", rds::fuse_register_updates);
    if (optimize && packed_matchers && std::any_of(m.metadata["objects"].begin(),m.metadata["objects"].end(),
        [](const rds::Json &o){return o[0]==10 && !(o[3].get<uint32_t>()&32);}))
      pass("pack_matchers", [&](rds::Model &model){rds::pack_matchers(model,regroup);});
    if(matcher_masks)
      pass("simplify_matcher_masks",rds::simplify_matcher_masks);
    if(stationary_matchers)
      pass("fold_stationary_matchers",rds::fold_stationary_matchers);
    if(payload_lifetimes)
      pass("share_payload_lifetimes",[&](rds::Model &model){rds::share_payload_lifetimes(model,payload_lifetime_sram,payload_lifetime_counters,payload_lifetime_split,payload_lifetime_phase);});
    if(payload_exchange)
      pass("exchange_payload_handles",[&](rds::Model &model){rds::exchange_payload_handles(model,packed_payload_handles,fused_transport_state,isolate_payload_readers,bitmap_payload_slots);if(release)rds::release_body(model);});
    if(payload_pool_limit)
      pass("pool_payloads",[&](rds::Model &model){rds::pool_payloads(model,payload_pool_limit);if(release)rds::release_body(model);});
    if(indexed_datapaths)
      pass("lift_indexed_datapaths",rds::lift_indexed_datapaths);
    if(matcher_grants && !contracts)
      pass("reuse_matcher_grants",rds::reuse_matcher_grants);
    if(matcher_columns)
      pass("split_matcher_columns",rds::split_matcher_columns);
    if (bundle_controls)
      pass("bundle_contract_controls", rds::bundle_contract_controls);
    if (contracts)
      pass("lift_contracts",[&](rds::Model &model){
        rds::lift_contracts(model);
        if(eager_contracts)for(auto &o:model.ops)if(o.code==32)o.imm[0]=1;
        model.metadata["contract_kernel_summary"]["lazy_mux_arms"]=!eager_contracts;
      });
    if(matcher_grants && contracts)
      pass("reuse_matcher_grants",rds::reuse_matcher_grants);
    if(handshake_rows)
      pass("share_handshake_rows",rds::share_handshake_rows);
    if(optimize_programs)
      pass("optimize_contract_programs",rds::optimize_contract_programs);
    if(guard_programs)
      pass("guard_contract_programs",rds::guard_contract_programs);
    if(cache_controls)
      pass("cache_contract_controls",[&](rds::Model &model){
        unsigned count=0;uint64_t bits=0;
        for(auto &o:model.ops)if(o.code==RDS_KERNEL_OPCODE && o.imm[2]>=16 && model.widths[o.out]<=64){
          uint64_t input_bits=0;for(auto a:o.args)input_bits+=model.widths[a];
          if(input_bits<=63){o.imm[0]=3;++count;bits+=input_bits;}}
        model.metadata["contract_cache_summary"]={{"programs",count},{"key_bits",bits},{"single_worker_bytes",count*16}};
      });
    if (bytes && work) {
      stage("replicate", [&] { rds::replicate(m, bytes, work, cone); });
      m.validate();
    }
    if(flow_regions)
      pass("flow_regions", [&](rds::Model &model){rds::plan_flow_regions(model,flow_order);});
    if(snapshot_prefix_work)
      pass("snapshot_prefixes", [&](rds::Model &model){
        rds::split_snapshot_prefixes(model, uint32_t(snapshot_prefix_work));
      });
    if (!output.empty())
      stage("write_binary", [&] { m.write_binary(output); });
    if (!report.empty())
      stage("write_report", [&] { write_json(report, m.json()); });
    if (!timing.empty())
      write_json(timing, times);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "rhodium-opt: " << e.what() << '\n';
    return 1;
  }
}
