// Compares statically guarded scalar and wide state cones with eager execution.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void check(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
static void arbiter_trace(void *context,uint64_t,const char *,uint32_t kind,
                          uint32_t count,int enqueue,int dequeue){
  if(kind==6)static_cast<std::vector<uint64_t>*>(context)->push_back(
      uint64_t(count)*4+unsigned(enqueue)*2+unsigned(dequeue));
}
static void compile(const std::string &source, bool release, bool native) {
  auto child = fork();
  check(child >= 0, "fork failed");
  if (!child) {
    const char *cc = std::getenv("CC");
    if (!cc)
      cc = "cc";
    auto binary = source + ".so";
    execlp(cc, cc, "-std=c17", "-O2", release ? "-DNDEBUG" : "-UNDEBUG",
           native ? "-march=native" : "-O2",
           "-shared", "-fPIC", source.c_str(), "-o", binary.c_str(), nullptr);
    _exit(127);
  }
  int status;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            !WEXITSTATUS(status),
        "compile failed");
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: regions_test build-directory");
    std::string dir = argv[1];
    std::filesystem::create_directories(dir);
    Model m;
    m.widths = {1, 1, 64, 64};
    m.metadata = {
        {"format", "rhodium-simulation-ir-v1"},
        {"registers", Json::array()},
        {"memories", Json::array()},
        {"writes", Json::array()},
        {"reads", Json::array()},
        {"assertions", Json::array()},
        {"objects", Json::array()},
        {"origins", Json::array()},
        {"inventory", Json::array()},
        {"occurrences", Json::array({"regions"})},
        {"ports",
         Json::array(
             {{0, 0, "reset"}, {0, 1, "valid"}, {0, 2, "x"}, {0, 3, "y"}})}};
    auto emit = [&](unsigned c, unsigned w, std::vector<Id> a,
                    std::vector<uint64_t> im = {}) {
      Id v = m.widths.size();
      m.widths.push_back(w);
      m.ops.push_back({c, v, std::move(a), std::move(im)});
      return v;
    };
    Id zero = emit(0, 64, {}, {0}), reset_value = emit(0, 192, {}, {0, 0, 0});
    // Indexed ROM fields and current register snapshots can remain local across
    // their consumers. Exercise every host-word subdivision and bank swaps.
    Id table = emit(0, 256, {}, {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                                0x55aa33cc0ff09669ULL, 0x1020304050607080ULL});
    Id table_state = m.widths.size();
    m.widths.push_back(256);
    Id table_next = emit(20, 256, {2, 3, 3, 2});
    m.metadata["registers"].push_back({table_state, table_next, none, none});
    for (unsigned ew = 1, iw = 8; ew <= 64; ew *= 2, --iw) {
      Id index = emit(17, iw, {2}, {0});
      Id rom = emit(21, ew, {table, index}, {256 / ew, ew});
      Id snapshot = emit(21, ew, {table_state, index}, {256 / ew, ew});
      Id combined = emit(5, ew, {rom, snapshot});
      m.metadata["ports"].push_back({1, combined, "indexed" + std::to_string(ew)});
    }
    // Independent events, shared cones, AND/OR short circuits, a control value
    // with late ordinary uses, and wide muxes exercise reused physical storage.
    for (unsigned region = 0; region < 40; ++region) {
      Id bit = emit(17, 1, {2}, {region % 64});
      Id guard = emit(3, 1, {1, bit});
      Id x = emit(6, 64, {2, emit(0, 64, {}, {region + 1})});
      for (unsigned j = 0; j < 27; ++j)
        x = emit(j % 2 ? 5 : 8, 64, {x, 3});
      Id eq = emit(12, 1, {x, zero});
      Id a = emit(3, 1, {guard, eq}), b = emit(4, 1, {guard, eq});
      Id control = emit(5, 1, {a, b});
      Id payload = emit(20, 192, {x, 2, 3});
      Id q = m.widths.size();
      m.widths.push_back(192);
      Id d = emit(15, 192, {control, q, payload}, {1});
      m.metadata["registers"].push_back({q, d, 0, reset_value});
      m.metadata["ports"].push_back({1, q, "q" + std::to_string(region)});
    }
    for (auto [kind, depth, flags] :
         std::vector<std::tuple<unsigned, unsigned, unsigned>>{{1, 1, 0},
                                                               {1, 1, 2},
                                                               {1, 1, 4},
                                                               {1, 1, 6},
                                                               {1, 3, 2},
                                                               {1, 3, 4},
                                                               {2, 3, 0},
                                                               {2, 1, 8},
                                                               {2, 1, 9},
                                                               {2, 1, 0},
                                                               {2, 3, 8},
                                                               {3, 1, 0},
                                                               {9, 2, 0}}) {
      Id object = m.metadata["objects"].size();
      Id ready = emit(17, 1, {3}, {0});
      std::vector<Id> inputs{0, 1, 2, ready};
      if (kind == 9)
        inputs.push_back(emit(17, 1, {3}, {1}));
      m.metadata["objects"].push_back(
          {kind, 64, depth, flags, inputs, "cache" + std::to_string(object)});
      Id x = emit(29, 64, flags & 4 ? std::vector<Id>{2} : std::vector<Id>{},
                  {object, kind == 9 ? 1u : 3u});
      Id original = x;
      if (object == 0)
        x = emit(6, 64, {x, emit(6, 64, {zero, emit(0, 64, {}, {9})})});
      Id valid = emit(29, 1, flags & 4 ? std::vector<Id>{1} : std::vector<Id>{},
                      {object, 2});
      m.metadata["ports"].push_back({1, valid, "object_valid" + std::to_string(object)});
      x = emit(15, 64, {valid, zero, x}, {1});
      for (unsigned j = 0; j < 27; ++j)
        x = emit(j % 2 ? 5 : 6, 64, {x, emit(0, 64, {}, {j * 13 + 1})});
      // Expose invalid-cycle payloads as well as their derived cached results.
      m.metadata["ports"].push_back({1, x, "cached" + std::to_string(object)});
      m.metadata["ports"].push_back(
          {1, original, "payload" + std::to_string(object)});
    }
    // Independent one-entry queues exercise all batch sizes (16 + 8 + 4),
    // with distinct ready/valid signals and directly staged functional payloads.
    for(unsigned lane=0;lane<28;++lane){
      Id object=m.metadata["objects"].size();
      Id valid=emit(17,1,{2},{lane}),ready=emit(17,1,{3},{lane});
      Id payload=emit(5,64,{2,emit(0,64,{}, {lane+1})});
      m.metadata["objects"].push_back({1,64,1,0,std::vector<Id>{0,valid,payload,ready},"batch"+std::to_string(lane)});
      for(unsigned query:{0u,2u,3u}){
        Id result=emit(29,query==3?64:1,{}, {object,query});
        m.metadata["ports"].push_back({1,result,"batch"+std::to_string(lane)+"_"+std::to_string(query)});
      }
    }
    // One-hot payload kernels materialize scratch; batched preparation reads
    // their pinned values after the latest producer without changing queries.
    for(unsigned lane=0;lane<28;++lane){
      Id object=m.metadata["objects"].size();
      Id valid=emit(17,1,{2},{lane}),ready=emit(17,1,{3},{lane});
      Id which=emit(17,1,{2},{lane+28});
      Id selector=emit(15,2,{which,emit(0,2,{}, {1}),emit(0,2,{}, {2})},{0});
      Id left=emit(20,128,{2,emit(0,64,{}, {lane+1})});
      Id right=emit(20,128,{3,emit(0,64,{}, {lane+100})});
      Id payload=emit(16,128,{selector,left,right});
      m.metadata["objects"].push_back({1,128,1,0,std::vector<Id>{0,valid,payload,ready},"copied"+std::to_string(lane)});
      for(unsigned query:{0u,2u,3u}){
        Id result=emit(29,query==3?128:1,{}, {object,query});
        m.metadata["ports"].push_back({1,result,"copied"+std::to_string(lane)+"_"+std::to_string(query)});
      }
    }
    // Mix direct and materialized payloads with late controls derived from
    // earlier payload work. This requires retaining inputs across a delayed batch.
    std::vector<Id> mixed_payloads;
    Id late=2;
    for(unsigned lane=0;lane<28;++lane){
      Id left=emit(20,128,{2,emit(0,64,{}, {lane+700})});
      Id right=emit(20,128,{3,emit(0,64,{}, {lane+900})});
      Id which=emit(17,1,{2},{lane+28});
      Id selector=emit(15,2,{which,emit(0,2,{}, {1}),emit(0,2,{}, {2})},{0});
      Id payload=lane%2?emit(16,128,{selector,left,right}):emit(5,128,{left,right});
      mixed_payloads.push_back(payload);
      if(lane%2)late=emit(5,64,{late,emit(17,64,{payload},{0})});
    }
    for(unsigned lane=0;lane<28;++lane){
      Id object=m.metadata["objects"].size();
      Id valid=emit(17,1,{2},{lane}),ready=emit(5,1,{emit(17,1,{late},{lane}),emit(17,1,{3},{lane})});
      m.metadata["objects"].push_back({1,128,1,0,std::vector<Id>{0,valid,mixed_payloads[lane],ready},"mixed"+std::to_string(lane)});
      for(unsigned query:{0u,2u,3u}){
        Id result=emit(29,query==3?128:1,{}, {object,query});
        m.metadata["ports"].push_back({1,result,"mixed"+std::to_string(lane)+"_"+std::to_string(query)});
      }
    }
    // Fixed arbiters have no publication work; rotating and packet arbiters
    // retain their transfer-dependent state across stalls and retries.
    for(unsigned flags:{0u,1u,2u,18u}){
      Id object=m.metadata["objects"].size();
      Id valid0=emit(17,1,{2},{0}),valid1=emit(17,1,{3},{0});
      std::vector<Id> inputs{0,1,valid0,2,valid1,3};
      if(flags&16){inputs.push_back(emit(17,1,{2},{1}));inputs.push_back(emit(17,1,{3},{1}));}
      m.metadata["objects"].push_back({6,64,2,flags,inputs,"arbiter"+std::to_string(flags)});
      for(unsigned query=0;query<5;++query){
        std::vector<Id> args{valid0,valid1};
        if(query==2){if(flags&1)args.clear();else{args.push_back(2);args.push_back(3);}}
        if(query>=3)args.push_back(1);
        Id result=emit(29,query==2?64:1,args,{object,query});
        m.metadata["ports"].push_back({1,result,"arbiter"+std::to_string(flags)+"_"+std::to_string(query)});
      }
    }
    // Write addresses and byte enables are observed only during enabled writes.
    // Their sizeable independent cones exercise event demand across SRAM effects.
    Id address_word = 2, mask_word = 3;
    for (unsigned j = 0; j < 27; ++j) {
      address_word = emit(j % 2 ? 5 : 6, 64, {address_word, 3});
      mask_word = emit(j % 2 ? 5 : 6, 64, {mask_word, 2});
    }
    Id address = emit(17, 4, {address_word}, {0});
    Id byte_mask = emit(17, 8, {mask_word}, {0});
    m.metadata["memories"].push_back({64, 16});
    m.metadata["writes"].push_back({0, address, 2, 1, byte_mask, 8});
    Id read_address = emit(17, 4, {2}, {4});
    Id data = emit(24, 64, {read_address}, {0});
    m.metadata["ports"].push_back({1, data, "memory_data"});
    // Low-field reuse must survive changes in unobserved high fields, while
    // input-dependent forwarding remains outside the cached instruction cone.
    for(unsigned width:{64u,192u}){
      Id q=m.widths.size();m.widths.push_back(width);
      Id low=width==64?q:emit(17,64,{q},{31});
      Id derived=low;
      for(unsigned j=0;j<30;++j)
        derived=emit(j%2?5:8,64,{derived,emit(0,64,{}, {UINT64_C(0x9e3779b97f4a7c15)+j})});
      Id mixed=emit(6,64,{derived,2});
      Id payload=width==64?2:emit(20,192,{2,3,2});
      Id d=emit(15,width,{1,q,payload},{1});
      m.metadata["registers"].push_back({q,d,0,width==64?zero:reset_value});
      m.metadata["ports"].push_back({1,derived,"derived"+std::to_string(width)});
      m.metadata["ports"].push_back({1,mixed,"ambient"+std::to_string(width)});
    }
    // In tolerant mode an unobserved one-hot result may be skipped, but its
    // original zero/multi-hot diagnostic remains eager when strict mode is set.
    Id selector = emit(17, 2, {2}, {0});
    Id selected = emit(16, 64, {selector, 2, 3});
    for (unsigned j = 0; j < 27; ++j)
      selected = emit(j % 2 ? 5 : 6, 64, {selected, 3});
    Id observed = emit(15, 64, {1, zero, selected}, {1});
    m.metadata["ports"].push_back({1, observed, "selected"});
    // Shared expensive cones serve independent consumers. Two to four events
    // fit a union; five events and an eager consumer exercise conservative
    // fallback, while opposite events require unconditional evaluation.
    for (unsigned scenario = 2; scenario <= 9; ++scenario) {
      std::vector<Id> events;
      unsigned consumers = scenario <= 5 ? scenario : 2;
      for (unsigned j = 0; j < consumers; ++j)
        events.push_back(emit(17, 1, {2}, {j + 9}));
      if (scenario == 7) events[1] = emit(2, 1, {events[0]});
      if (scenario == 9) events[1] = emit(0, 1, {}, {0});
      Id shared = emit(6, 64, {2, emit(0, 64, {}, {scenario})});
      for (unsigned j = 0; j < 29; ++j)
        shared = emit(j % 2 ? 5 : 8, 64, {shared, 3});
      if (scenario == 8) events[1] = emit(17, 1, {shared}, {7});
      for (unsigned j = 0; j < consumers; ++j) {
        Id output = emit(15, 64, {events[j], zero, shared}, {1});
        m.metadata["ports"].push_back({1, output, "union" + std::to_string(scenario) + "_" + std::to_string(j)});
      }
      if (scenario == 6) m.metadata["ports"].push_back({1, shared, "union_eager"});
    }
    // Multiword muxes select contiguous current snapshots, including tails and
    // multiple keys. Both alternatives change across bank swaps; a local
    // producer remains available without forced arena materialization.
    Id mux_selector=emit(17,2,{2},{8});
    Id payload_a=emit(20,512,{2,3,2,3,2,3,2,3});
    Id payload_b=emit(20,512,{3,2,3,2,3,2,3,2});
    for(unsigned width:{65u,127u,128u,151u,256u,512u}) {
      Id qa=m.widths.size();m.widths.push_back(width);
      Id qb=m.widths.size();m.widths.push_back(width);
      Id da=emit(17,width,{payload_a},{0}),db=emit(17,width,{payload_b},{0});
      m.metadata["registers"].push_back({qa,da,none,none});
      m.metadata["registers"].push_back({qb,db,none,none});
      Id clear=emit(0,width,{},std::vector<uint64_t>((width+63)/64));
      Id selection=emit(15,width,{mux_selector,qa,qb,clear},{1,3});
      Id local=emit(15,width,{mux_selector,da,qb},{2});
      m.metadata["ports"].push_back({1,selection,"snapshot_mux"+std::to_string(width)});
      m.metadata["ports"].push_back({1,local,"local_mux"+std::to_string(width)});
    }
    m.validate();
    auto file = dir + "/regions.rsim";
    m.write_binary(file);
    std::string pruned=file;
    if(const char *optimizer=std::getenv("RDS_STATELESS_OPTIMIZER")){
      m.metadata["opcodes"]={"constant","copy","not","and","or","xor","add","sub","mul",
          "shl","shru","shrs","eq","ult","slt","mux_lookup","onehot_mux","extract","zext","sext",
          "pack","vector_index","vector_inject","vector_write_set","memory_read_async","decode",
          "set_clear","balance","counter_step","object_query","alu","byte_merge"};
      auto input=dir+"/regions.json";pruned=dir+"/regions-pruned.rsim";
      std::ofstream(input)<<m.json().dump()<<'\n';
      auto child=fork();check(child>=0,"fork optimizer");
      if(!child){execl(optimizer,optimizer,"--input",input.c_str(),"--output",pruned.c_str(),
                     "--no-optimize","--prune-stateless-inputs",nullptr);_exit(127);}
      int status;check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"prune stateless inputs");
    }
    std::vector<rds_sim *> sims;
    std::vector<std::string> binaries;
    std::vector<std::vector<uint64_t>> traces(75);
    for (unsigned mode = 0; mode < 75; ++mode) {
      char error[512];
      rds_options opt{(mode >= 9 && mode < 15) || (mode >= 39 && mode < 43) ? 4u : 1u,
                      mode >= 55 ? 2428944u | RDS_SEMANTIC_REGIONS | RDS_UNION_DEMAND | RDS_GUARDED_ONEHOT | RDS_STABLE_FIELDS | RDS_COLD_REGIONS | (mode>=59?RDS_FLOW_PREPARE:0u) | (mode>=63?RDS_TYPED_SCRATCH:0u) | (mode>=67?RDS_COALESCE_SCRATCH:0u)
                      : mode >= 51 ? 2428944u | RDS_SEMANTIC_REGIONS | RDS_UNION_DEMAND
                      : mode >= 43 ? 2428944u | RDS_SEMANTIC_REGIONS | RDS_UNION_DEMAND | RDS_GUARDED_ONEHOT | (mode >= 47 ? RDS_STABLE_FIELDS : 0u)
                      : mode >= 39 ? 2445904u | RDS_GUARDED_ONEHOT | RDS_TYPED_SCRATCH
                      : mode >= 35 ? 2428944u | RDS_STABLE_FIELDS | RDS_SEMANTIC_REGIONS | RDS_GUARDED_ONEHOT
                      : mode >= 31 ? 2428944u | RDS_GUARDED_ONEHOT
                      : mode >= 27 ? 2428944u | RDS_SEMANTIC_REGIONS | RDS_GUARDED_ONEHOT
                      : mode >= 23 ? 2428944u | RDS_STABLE_FIELDS | RDS_SEMANTIC_REGIONS
                      : mode >= 19 ? 2428944u | RDS_STABLE_FIELDS
                      : mode >= 15 ? 2428944u | RDS_SEMANTIC_REGIONS
                      : mode >= 13  ? 348752u | RDS_BALANCE_PARTITIONS
                      : mode >= 9 ? 2445904u | (mode >= 11 ? 1024u : 0u)
                      : mode >= 5 ? 2428944u
                      : mode      ? 1380368u
                                  : 1u};
      if (mode >= 71) opt.flags |= RDS_LIFT_TRANSITIONS | RDS_LIFT_PRIMITIVES;
      auto *s = rds_load_with_options((mode?pruned:file).c_str(), &opt, error, sizeof error);
      check(s, error);
      if ((mode >= 9 && mode < 15) || (mode >= 39 && mode < 43))
        check(rds_get_stats(s).workers > 1 && rds_get_stats(s).workers <= opt.workers,
              "parallel fixture has an invalid worker count");
      if (mode >= 43) {
        auto plan_path = dir + "/union-plan.json";
        check(!rds_emit_plan(s, plan_path.c_str()), rds_error(s));
        std::ifstream plan_file(plan_path);
        Json plan; plan_file >> plan;
        unsigned maximum = 0;
        for (const auto &guard : plan["instruction_guards"])
          if (guard.is_object()) maximum = std::max(maximum, unsigned(guard["or"].size()));
        check(maximum == 4, "bounded unions must retain four independent events");
      }
      binaries.push_back("");
      if (mode) {
        auto source = dir + "/regions-" + std::to_string(mode) + ".c";
        // Small budgets deliberately exercise generic fallback and footprint
        // convergence.
        check(
            !rds_emit_c(s, source.c_str(), mode % 4 < 3 && mode % 4 ? 2 : 4096),
            rds_error(s));
        compile(source, mode % 2 == 0, mode >= 67);
        binaries.back() = source + ".so";
        check(!rds_use_compiled(s, (source + ".so").c_str()), rds_error(s));
      }
      sims.push_back(s);
      rds_set_object_trace(s,arbiter_trace,&traces[mode]);
    }
    std::mt19937_64 rng(493821);
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
      if (cycle == 501)
        for (size_t i = 1; i < sims.size(); ++i)
          check(!rds_use_compiled(sims[i], binaries[i].c_str()),
                rds_error(sims[i]));
      uint64_t input[] = {cycle % 61 == 0, cycle % 3 == 0, rng(), rng()};
      const char *names[] = {"reset", "valid", "x", "y"};
      for (auto *s : sims) {
        for (unsigned i = 0; i < 4; ++i)
          check(!rds_set_u64(s, rds_find_port(s, names[i]), input[i]),
                rds_error(s));
        check(!rds_eval(s), rds_error(s));
      }
      if(cycle%17==0){
        // A second evaluation can cancel an update before publication.
        input[1]^=1;input[2]^=UINT64_C(0x1b873593);
        for(auto *s:sims){
          check(!rds_set_u64(s,rds_find_port(s,"valid"),input[1]),rds_error(s));
          check(!rds_set_u64(s,rds_find_port(s,"x"),input[2]),rds_error(s));
          check(!rds_eval(s),rds_error(s));
        }
      }
      for (const auto &port : m.metadata["ports"]) {
        if (port[0] != 1)
          continue;
        std::string name = port[2];
        size_t words = (m.widths[port[1].get<Id>()] + 63) / 64;
        std::vector<uint64_t> expected(words);
        check(!rds_get(sims[0], rds_find_port(sims[0], name.c_str()),
                       expected.data(), words),
              "get reference");
        for (auto *s : sims) {
          std::vector<uint64_t> actual(words);
          check(
              !rds_get(s, rds_find_port(s, name.c_str()), actual.data(), words),
              "get compiled");
          for (unsigned w = 0; w < words; ++w)
            check(actual[w] == expected[w],
                  "state mismatch at cycle " + std::to_string(cycle));
        }
      }
      for(auto &trace:traces)trace.clear();
      for (auto *s : sims)
        check(!rds_advance(s), rds_error(s));
      for(const auto &trace:traces)check(trace==traces[0],"arbiter trace mismatch");
    }
    for (auto *s : sims) {
      check(!rds_set_u64(s, rds_find_port(s, "valid"), 0), rds_error(s));
      for (uint64_t invalid : {0u, 3u}) {
        check(!rds_set_u64(s, rds_find_port(s, "x"), invalid), rds_error(s));
        rds_set_strict(s, 1);
        check(rds_eval(s) != 0, "inactive one-hot selector must fail in strict mode");
        rds_set_strict(s, 0);
        check(!rds_eval(s), rds_error(s));
        uint64_t output;
        check(!rds_get_u64(s, rds_find_port(s, "selected"), &output) && output == 0,
              "tolerant retry changed the inactive result");
      }
      rds_free(s);
    }
    std::cout << "Static demand: shared guards, wide state, scratch reuse and "
                 "body budgets passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
