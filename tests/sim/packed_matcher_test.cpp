// Checks packed matcher compilation against independent arbitration, including
// reset, snapshot reattachment, prefix feedback, grant reuse, and split priority ownership.
#include "../../rhodium/sim/compiler/model.hpp"
#include "../../rhodium/sim/runtime/include/rhodium_sim.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
using namespace rds;
static void require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
static void command(std::vector<std::string> args) {
  pid_t child = fork();
  require(child >= 0, "fork failed");
  if (!child) {
    std::vector<char *> argv;
    for (auto &a : args)
      argv.push_back(a.data());
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "C compilation failed");
}
static Model fixture(unsigned rows, unsigned cols, bool feedback) {
  Model m;
  m.metadata = {{"format", "rhodium-simulation-ir-v1"},
                {"registers", Json::array()},
                {"memories", Json::array()},
                {"writes", Json::array()},
                {"reads", Json::array()},
                {"assertions", Json::array()},
                {"objects", Json::array()},
                {"origins", Json::array()},
                {"inventory", Json::array()},
                {"occurrences", Json::array({"matcher"})}};
  m.widths = {1, rows * cols, cols};
  m.metadata["ports"] =
      Json::array({{0, 0, "reset"}, {0, 1, "requests"}, {0, 2, "accepts"}});
  auto emit = [&](unsigned c, unsigned w, std::vector<Id> a,
                  std::vector<uint64_t> i = std::vector<uint64_t>{}) {
    Id v = m.widths.size();
    m.widths.push_back(w);
    m.ops.push_back({c, v, std::move(a), std::move(i)});
    return v;
  };
  std::vector<Id> requests, accepts, grants, input{0}, prefix;
  for (unsigned i = 0; i < rows * cols; i++)
    requests.push_back(emit(17, 1, {1}, {i}));
  // Adaptive/fallback requests retain the input-valid predicate on both mux
  // arms. An independent selector must not stop that implication proof.
  auto original_requests=requests;
  for(unsigned i=0;i<rows*cols;++i){
    Id masked=emit(3,1,{requests[i],0});
    requests[i]=emit(15,1,{0,requests[i],masked},{1});
  }
  for (unsigned i = 0; i < cols; i++)
    accepts.push_back(emit(17, 1, {2}, {i}));
  for (unsigned col = 0; col < cols; col++) {
    if (feedback && col == 1) {
      Id bit = emit(17, 1, {grants[0]}, {0});
      requests[1] = emit(3, 1, {requests[1], emit(2, 1, {bit})});
    }
    for (unsigned row = 0; row < rows; row++)
      prefix.push_back(requests[row * cols + col]);
    grants.push_back(emit(29, rows, prefix, {0, col}));
  }
  std::vector<Id> output, accepted_output;
  for (unsigned row = 0; row < rows; row++)
    for (unsigned col = 0; col < cols; col++) {
      Id grant=emit(17, 1, {grants[col]}, {row});
      output.push_back(emit(3,1,{grant,original_requests[row*cols+col]}));
      // Acceptance is independent of requesting: this mask must survive.
      accepted_output.push_back(emit(3,1,{grant,accepts[col]}));
    }
  Id result = emit(20, rows * cols, output);
  Id accepted_result = emit(20, rows * cols, accepted_output);
  input.insert(input.end(), requests.begin(), requests.end());
  input.insert(input.end(), accepts.begin(), accepts.end());
  m.metadata["objects"].push_back(
      Json::array({10, rows, cols, 0, input, "matcher"}));
  m.metadata["ports"].push_back({1, result, "grants"});
  m.metadata["ports"].push_back({1, accepted_result, "accepted_grants"});
  m.validate();
  return m;
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: packed_matcher_test build-directory");
    std::string dir = argv[1];
    std::filesystem::create_directories(dir);
    std::mt19937_64 rng(81293);
    for (auto [rows, cols] : std::vector<std::pair<unsigned, unsigned>>{
             {1, 1}, {3, 2}, {3, 5}, {11, 11}, {64, 2}, {2, 64}})
      for (bool feedback : {false, true}) {
        if (feedback && (rows != 3 || cols != 2))
          continue;
        std::string stem = dir + "/matcher-" + std::to_string(rows) + "-" +
                           std::to_string(cols) + (feedback ? "-feedback" : "");
        auto m = fixture(rows, cols, feedback);
        m.write_binary(stem + "-scalar.rsim");
        auto scalar=m;simplify_matcher_masks(scalar);scalar.validate();
        require(scalar.metadata["matcher_mask_summary"]["rewritten"].get<unsigned>()>0,"scalar matcher implication not exercised");
        scalar.write_binary(stem+"-scalar-simplified.rsim");
        share_matchers(m);
        pack_matchers(m, !feedback);
        simplify_matcher_masks(m);
        require(m.metadata["matcher_mask_summary"]["rewritten"].get<unsigned>()>0,"packed matcher implication not exercised");
        m.validate();
        m.write_binary(stem + ".rsim");
        auto reused=m;reuse_matcher_grants(reused);reused.validate();
        require(reused.metadata["matcher_grant_reuse_summary"]["objects"]==1,"grant reuse fixture was not converted");
        reused.write_binary(stem+"-grants.rsim");
        auto split=m;split_matcher_columns(split);split.validate();split.write_binary(stem+"-split.rsim");
        auto split_reused=reused;split_matcher_columns(split_reused);split_reused.validate();split_reused.write_binary(stem+"-split-grants.rsim");
        // A query with an independent request column cannot update this owner.
        auto independent=m;for(auto &o:independent.ops)if(o.code==29){
          o.args[1]=o.args[0];
          break;
        }
        reuse_matcher_grants(independent);independent.validate();
        require(independent.metadata["matcher_grant_reuse_summary"]["objects"]==0,"reused independent query requests");
        if(cols>1){auto independent_prefix=m;Id empty=none;
          for(auto &o:independent_prefix.ops)if(o.code==29){
            if(empty==none)empty=o.args[0];else{o.args[0]=empty;break;}}
          reuse_matcher_grants(independent_prefix);independent_prefix.validate();
          require(independent_prefix.metadata["matcher_grant_reuse_summary"]["objects"]==0,"reused independent query prefix");
        }
        auto invalid = m;
        invalid.metadata["objects"][0][4][1] = 0;
        bool rejected = false;
        try {
          invalid.validate();
        } catch (const std::exception &) {
          rejected = true;
        }
        if (rows > 1)
          require(rejected, "accepted wrong packed column width");
        std::vector<rds_sim *> sims;
        for (unsigned mode = 0; mode < 16; mode++) {
          char error[512];
          rds_options options{mode == 4 || mode == 7 || mode==12 || mode==15 ? 4u : 1u, mode < 2 || mode==8 || mode==9 || mode==13 ? 1u : 576u | (mode >= 5 ? RDS_LIFT_PRIMITIVES : 0u)};
          std::string file = stem + (mode >= 13 ? (mode==15?"-split-grants.rsim":"-split.rsim") : mode == 0 ? "-scalar.rsim" : mode==8?"-scalar-simplified.rsim":mode>=9?"-grants.rsim":".rsim");
          auto *s = rds_load_with_options(file.c_str(), &options, error,
                                          sizeof error);
          require(s, error);
          if (mode >= 2 && mode!=8 && mode!=9 && mode!=13) {
            std::string source = stem + "-" + std::to_string(mode) + ".c",
                        binary = source + ".so";
            require(!rds_emit_c(s, source.c_str(), 512), rds_error(s));
            command({std::getenv("CC") ? std::getenv("CC") : "cc", "-std=c17",
                     "-O2", mode == 2 || mode == 5 || mode==10 ? "-UNDEBUG" : "-DNDEBUG", "-shared",
                     "-fPIC", source, "-o", binary});
            require(!rds_use_compiled(s, binary.c_str()), rds_error(s));
          }
          sims.push_back(s);
        }
        std::vector<unsigned> priority(cols);
        size_t words = (rows * cols + 63) / 64;
        for (unsigned cycle = 0; cycle < 700; cycle++) {
          std::vector<uint64_t> req(words), expected(words);
          for (auto &v : req)
            v = rng();
          if (rows * cols % 64)
            req.back() &= (uint64_t(1) << (rows * cols % 64)) - 1;
          uint64_t accept = cycle % 9 ? rng() : 0;
          if (cols < 64)
            accept &= (uint64_t(1) << cols) - 1;
          bool reset = cycle % 83 == 0;
          uint64_t taken = 0;
          bool first = false;
          auto next = priority;
          for (unsigned col = 0; col < cols; col++)
            for (unsigned distance = 0; distance < rows; distance++) {
              unsigned row = (priority[col] + distance) % rows,
                       bit = row * cols + col;
              bool request = (req[bit / 64] >> (bit % 64)) & 1;
              if (feedback && col == 1 && row == 0)
                request &= !first;
              if (request && !((taken >> row) & 1)) {
                expected[bit / 64] |= uint64_t(1) << (bit % 64);
                taken |= uint64_t(1) << row;
                if (col == 0 && row == 0)
                  first = true;
                if ((accept >> col) & 1)
                  next[col] = (row + 1) % rows;
                break;
              }
            }
          for (unsigned mode=0;mode<sims.size();++mode) {
            auto *s=sims[mode];bool compiled=mode>=2&&mode!=8&&mode!=9&&mode!=13;
            auto check = [&](int rc) { require(!rc, rds_error(s)); };
            check(rds_set_u64(s, rds_find_port(s, "reset"), reset));
            check(rds_set(s, rds_find_port(s, "requests"), req.data(), words));
            check(rds_set_u64(s, rds_find_port(s, "accepts"), accept));
            check(rds_eval(s));
            if(cycle==201&&compiled){
              // Debug and release may have different private object layouts.
              unsigned peer=mode==5?6:mode==6?5:mode;
              check(rds_use_compiled(s,(stem+"-"+std::to_string(peer)+".c.so").c_str()));
              check(rds_eval(s));
            }
            std::vector<uint64_t> actual(words);
            check(rds_get(s, rds_find_port(s, "grants"), actual.data(), words));
            require(actual == expected, "grant mismatch at " + stem +
                                            " cycle " + std::to_string(cycle));
            check(rds_get(s,rds_find_port(s,"accepted_grants"),actual.data(),words));
            for(unsigned bit=0;bit<rows*cols;++bit)
              require(((actual[bit/64]>>(bit%64))&1)==(((expected[bit/64]>>(bit%64))&1)&((accept>>(bit%cols))&1)),"independent acceptance mask changed");
            check(rds_advance(s));
            if(cycle==402&&compiled)
              check(rds_use_compiled(s,(stem+"-"+std::to_string(mode)+".c.so").c_str()));
          }
          priority = reset ? std::vector<unsigned>(cols) : next;
        }
        for (auto *s : sims)
          rds_free(s);
        std::cout << rows << "x" << cols << " feedback=" << feedback
                  << " passed\n";
      }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
