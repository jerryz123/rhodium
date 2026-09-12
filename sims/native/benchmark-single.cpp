// Runs audited single-worker comparisons, optional shared-inode library staging, and matched profile-guided builds.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using Json = nlohmann::json;
namespace fs = std::filesystem;
extern char **environ;
static void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string capture(const std::vector<std::string> &args, int timeout_ms,
                           const Json &env = nullptr, bool build_environment = false) {
    int pipes[2]; require(pipe(pipes) == 0, "pipe failed");
    pid_t child = fork(); require(child >= 0, "fork failed");
    if (!child) {
        setpgid(0, 0); close(pipes[0]);
        dup2(pipes[1], STDOUT_FILENO); dup2(pipes[1], STDERR_FILENO); close(pipes[1]);
        if(build_environment)for(const char *key:{"MAKEFLAGS","MFLAGS","MAKEOVERRIDES","CC","CXX","CFLAGS","CXXFLAGS","CPPFLAGS",
            "USER_CPPFLAGS","USER_LDFLAGS","LDFLAGS","OPT","OPT_FAST","OPT_SLOW","OPT_GLOBAL","VM_USER_CFLAGS","VM_USER_LDLIBS",
            "OBJCACHE","CCACHE_PREFIX","CPATH","C_INCLUDE_PATH","CPLUS_INCLUDE_PATH","OBJC_INCLUDE_PATH","LIBRARY_PATH",
            "LD_LIBRARY_PATH","LD_PRELOAD","COMPILER_PATH","GCC_EXEC_PREFIX","LLVM_PROFILE_FILE","CCC_OVERRIDE_OPTIONS","CCC_ADD_ARGS"})unsetenv(key);
        if (!env.is_null()) {
            std::vector<std::string> remove;
            for (char **e = environ; *e; ++e) {
                std::string key(*e, std::strchr(*e, '=') - *e);
                if (key.rfind("RDS_", 0) == 0 || key.rfind("PMU_", 0) == 0 || key == "LD_PRELOAD")
                    remove.push_back(key);
            }
            for (const auto &key : remove) unsetenv(key.c_str());
            for (auto it = env.begin(); it != env.end(); ++it)
                setenv(it.key().c_str(), it.value().get<std::string>().c_str(), 1);
        }
        std::vector<char *> argv;
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr); execvp(argv[0], argv.data()); _exit(127);
    }
    close(pipes[1]); setpgid(child, child);
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    std::string output; int status = 0; bool done = false, eof = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    try {
        while (!done || !eof) {
            require(std::chrono::steady_clock::now() < deadline, "subprocess timed out: " + args[0]);
            pollfd fd{pipes[0], POLLIN, 0}; poll(eof ? nullptr : &fd, eof ? 0 : 1, 20);
            char buffer[8192]; ssize_t n;
            while ((n = read(pipes[0], buffer, sizeof buffer)) > 0) {
                output.append(buffer, static_cast<size_t>(n));
                require(output.size() <= 1024 * 1024, "subprocess output exceeds 1 MiB");
            }
            if (!n) eof = true;
            else require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR, "subprocess read failed");
            if (!done) done = waitpid(child, &status, WNOHANG) == child;
        }
    } catch (...) {
        kill(-child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        close(pipes[0]); throw;
    }
    close(pipes[0]);
    require(WIFEXITED(status) && !WEXITSTATUS(status), args[0] + ": " + output);
    return output;
}
static std::string hash_file(const std::string &file) {
    std::string result = capture({"sha256sum", "--", file}, 300000);
    if (!result.empty() && result[0] == '\\') result.erase(0, 1);
    require(result.size() >= 64, "invalid SHA256 output"); return result.substr(0, 64);
}
static bool unsigned_integer(const Json &j, uint64_t minimum = 0) {
    return j.is_number_integer() && j.get<double>() >= static_cast<double>(minimum)
        && j.get<double>() <= 9007199254740991.0;
}
static void validate(const Json &row, const Json &variant) {
    std::string name = variant.at("name");
    require(row.contains("workers") && row["workers"] == 1 && (variant.at("kind") != "verilator" || (row.contains("context_threads") && row["context_threads"] == 1)),
            name + ": exactly one actual worker and Verilator context thread required");
    const std::string digest = row.value("digest", "");
    require(row.contains("seconds") && row["seconds"].is_number() && row["seconds"].get<double>() > 0
            && row.contains("cycles") && unsigned_integer(row["cycles"], 1)
            && row.contains("polls") && unsigned_integer(row["polls"])
            && digest.size() == 16 && digest.find_first_not_of("0123456789abcdef") == std::string::npos,
            name + ": malformed timing or trace");
}
static double median(std::vector<double> values) {
    require(!values.empty(), "empty median"); std::sort(values.begin(), values.end()); return values[values.size() / 2];
}
// Reuse one file's overlapping page-cache backing between sequential children.
// This controls an observed file-instance effect; it does not guarantee identical
// physical placement, which remains the operating system's decision.
class LibrarySlot {
    int fd_ = -1;
public:
    fs::path path;
    Json identity;
    explicit LibrarySlot(const fs::path &directory) {
        require(fs::create_directory(directory), "library-slot directory must be new");
        path=fs::absolute(directory/"model.so");
        fd_=open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        require(fd_>=0,"cannot create library slot");
        struct stat info{};
        if(fstat(fd_,&info)){close(fd_);fd_=-1;throw std::runtime_error("cannot inspect library slot");}
        identity={{"path",path.string()},{"device",info.st_dev},{"inode",info.st_ino}};
    }
    LibrarySlot(const LibrarySlot&)=delete;
    LibrarySlot& operator=(const LibrarySlot&)=delete;
    ~LibrarySlot(){if(fd_>=0)close(fd_);}
    void stage(const fs::path &source,const std::string &expected_hash) {
        std::ifstream input(source,std::ios::binary);require(bool(input),"cannot open library source");
        char bytes[65536];off_t at=0;
        while(input.read(bytes,sizeof bytes)||input.gcount()){
            std::streamsize remaining=input.gcount();const char *next=bytes;
            while(remaining){ssize_t n=pwrite(fd_,next,size_t(remaining),at);
                if(n<0&&errno==EINTR)continue;
                require(n>0,"cannot write library slot");at+=n;next+=n;remaining-=n;}
        }
        require(input.eof()&&!input.bad(),"cannot read library source");
        require(ftruncate(fd_,at)==0,"cannot size library slot");
        require(hash_file(path)==expected_hash,"staged library hash mismatch");
    }
};
static void benchmark(const fs::path &config_file, const fs::path &output) {
    std::ifstream input(config_file); require(bool(input), "cannot open config"); Json config; input >> config;
    Json &variants = config.at("variants"); std::set<std::string> names; unsigned references = 0;
    require(variants.is_array() && variants.size() >= 2, "supply at least two uniquely named variants");
    require(config.at("compiler_flags").is_array() && !config["compiler_flags"].empty(), "record common compiler_flags");
    Json artifacts = Json::object(); std::string reference;
    for (auto &v : variants) {
        const std::string name = v.at("name"), kind = v.at("kind");
        require(names.insert(name).second, "supply at least two uniquely named variants");
        require(kind == "native" || kind == "verilator", "invalid simulator kind");
        if (kind == "verilator") { ++references; reference = name; }
        require(v.at("compiler_flags") == config["compiler_flags"] && v.at("compiler") == config.at("compiler")
                && !v.at("compiler").get<std::string>().empty(), name + ": compiler version and flags must match the comparison");
        if (kind == "native") require(v.contains("flags") && unsigned_integer(v["flags"])
                && v.contains("model") && v.contains("library"), name + ": native flags, model and library required");
        require(v.contains("executable"), name + ": executable required");
        for (const char *field : {"executable", "model", "library"}) if (v.contains(field)) {
            fs::path file = v[field].get<std::string>();
            if (file.is_relative()) file = fs::absolute(config_file).parent_path() / file;
            v[field] = file.lexically_normal().string(); artifacts[v[field].get<std::string>()] = hash_file(v[field]);
        }
    }
    require(references == 1, "supply exactly one Verilator reference");
    Json trials = config.value("trials", Json(5)), boots = config.value("boots", Json(1000));
    Json loops = config.value("loops", Json::array({256}));
    require(unsigned_integer(trials, 1) && unsigned_integer(boots, 1) && loops.is_array() && !loops.empty(), "invalid trials, boots or loops");
    require(config.contains("cpu") && unsigned_integer(config["cpu"]), "supply one nonnegative CPU index");
    Json report = {{"format", "rhodium-single-worker-benchmark-v1"}, {"config", config}, {"artifacts", artifacts},
        {"rows", Json::array()}, {"validation", Json::array()}, {"summaries", Json::array()},
        {"provenance_note", "Compiler metadata is supplied by the build owner; executable hashes identify the measured artifacts."}};
    fs::create_directories(fs::absolute(output).parent_path());
    require(!config.contains("shared_library_slot")||config["shared_library_slot"].is_boolean(),"shared_library_slot must be Boolean");
    std::unique_ptr<LibrarySlot> slot;
    if(config.value("shared_library_slot",false)){
        slot=std::make_unique<LibrarySlot>(fs::absolute(output).string()+".library-slot");
        report["library_slot"]=slot->identity;
    }
    auto save = [&] { std::ofstream stream(output); stream << report.dump(2) << '\n'; require(bool(stream), "cannot write report"); };
    auto run = [&](const Json &v, const Json &loop, const Json &count) {
        require(unsigned_integer(loop) && unsigned_integer(count, 1), "invalid target loop or boot count");
        Json env = {{"RDS_WORKERS", "1"}, {"RDS_LOOP_ITERATIONS", loop.dump()}};
        std::vector<std::string> args = {"taskset", "-c", config["cpu"].dump(), v.at("executable")};
        if (v.at("kind") == "native") {
            env["RDS_FLAGS"] = v.at("flags").dump(); env["RDS_COMPILED"] = v.at("library"); args.push_back(v.at("model"));
            if(slot){slot->stage(v.at("library").get<std::string>(),artifacts.at(v.at("library").get<std::string>()));env["RDS_COMPILED"]=slot->path.string();}
        }
        args.push_back(count.dump()); args.push_back("bench");
        Json row = Json::parse(capture(args, config.value("timeout_ms", 300000), env)); validate(row, v);
        row["name"] = v.at("name"); row["loop"] = loop; row["boots"] = count;
        if(slot&&v.at("kind")=="native"){
            require(hash_file(slot->path)==artifacts.at(v.at("library").get<std::string>()),"staged library changed during child execution");
            row["library_slot"]=slot->identity;row["library_sha256"]=artifacts.at(v.at("library").get<std::string>());
        }
        row["rate"] = row["cycles"].get<double>() / row["seconds"].get<double>(); return row;
    };
    auto signature = [](const Json &r) { return Json::array({r.at("cycles"), r.at("polls"), r.at("digest")}); };
    for (const auto &loop : loops) {
        Json expected;
        for (uint64_t trial = 0; trial < trials.get<uint64_t>(); ++trial)
            for (size_t j = 0; j < variants.size(); ++j) {
                Json row = run(variants[(j + trial) % variants.size()], loop, boots), sig = signature(row);
                require(expected.is_null() || expected == sig, row["name"].get<std::string>() + ": trace mismatch at loop " + loop.dump());
                expected = sig; row["trial"] = trial; report["rows"].push_back(row); save();
                std::cout << row["name"].get<std::string>() << " loop=" << loop << " trial=" << trial << " cycles/s=" << row["rate"] << std::endl;
            }
        Json rates = Json::object(), ratios = Json::object();
        for (const auto &v : variants) {
            std::vector<double> values;
            for (const auto &r : report["rows"]) if (r["loop"] == loop && r["name"] == v["name"]) values.push_back(r["rate"]);
            rates[v["name"].get<std::string>()] = median(values);
        }
        for (auto it = rates.begin(); it != rates.end(); ++it) ratios[it.key()] = it.value().get<double>() / rates[reference].get<double>();
        report["summaries"].push_back({{"loop", loop}, {"rates", rates}, {"ratios", ratios}}); save();
    }
    for (const auto &loop : config.value("validation_loops", Json::array({0, 1024}))) {
        Json expected;
        for (const auto &v : variants) {
            Json row = run(v, loop, config.value("validation_boots", Json(100))), sig = signature(row);
            require(expected.is_null() || expected == sig, v["name"].get<std::string>() + ": validation trace mismatch at loop " + loop.dump());
            expected = sig; report["validation"].push_back(row); save();
        }
    }
    for (auto it = artifacts.begin(); it != artifacts.end(); ++it) require(hash_file(it.key()) == it.value(), "artifact changed during benchmark: " + it.key());
    report["complete"] = true; save(); std::cout << report["summaries"].dump(2) << std::endl;
}
static void profile_build(const fs::path &model_arg, const fs::path &source_arg,
                          const fs::path &verilated_arg, const fs::path &output_arg,
                          uint32_t native_flags, int lock, bool lto = false) {
    require(flock(lock,LOCK_SH)==0,"cannot acquire PGO preparation lock");
    const fs::path model=fs::absolute(model_arg), source=fs::absolute(source_arg),
        verilated=fs::absolute(verilated_arg), out=fs::absolute(output_arg), repo=fs::current_path();
    require(fs::exists(repo/"rhodium/sim/runtime/model.c"), "run --pgo from the Rhodium repository root");
    require(fs::is_regular_file(model)&&fs::is_regular_file(source)&&fs::is_regular_file(verilated/"VSoCHarness.mk"), "supply a model, generated C, and retained single-thread VSoCHarness build");
    require(out.string().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-")==std::string::npos,
            "PGO output path must contain only letters, digits, underscores, dots, slashes and hyphens for generated make flags");
    require(!fs::exists(out)||fs::is_empty(out), "PGO output directory must be new or empty");
    fs::create_directories(out/"verilated"); fs::create_directories(out/"profiles");
    fs::copy_file(source,out/"native.c");
    for(const auto &entry:fs::directory_iterator(verilated)) {
        const auto ext=entry.path().extension();
        if(ext==".cpp"||ext==".h"||ext==".mk")fs::copy_file(entry.path(),out/"verilated"/entry.path().filename());
    }
    std::string compiler=capture({"clang","--version"},10000); compiler.resize(compiler.find('\n'));
    std::string cxx=capture({"clang++","--version"},10000); cxx.resize(cxx.find('\n'));
    require(compiler==cxx,"Clang C and C++ versions must match");
    Json manifest={{"purpose","Matched instrumentation, training and profile-use provenance"},{"compiler",compiler},{"commands",Json::array()},
        {"inputs",{{model.string(),hash_file(model.string())},{source.string(),hash_file(source.string())}}}};
    if(lto)manifest["linker_version"]=capture({"ld.lld","--version"},10000);
    auto track=[&](const fs::path &p){require(fs::is_regular_file(p),"missing build input: "+p.string());manifest["inputs"][p.string()]=hash_file(p.string());};
    track(out/"native.c"); track(repo/"sims/native/mini-smoke.c");track(repo/"sims/native/mini-loader.h");
    std::vector<std::string> runtime;
    for(const auto &entry:fs::recursive_directory_iterator(repo/"rhodium/sim/runtime"))
        if(entry.path().extension()==".c"||entry.path().extension()==".h")track(entry.path());
    for(const auto &entry:fs::directory_iterator(repo/"rhodium/sim/runtime"))if(entry.path().extension()==".c")runtime.push_back(entry.path());
    std::sort(runtime.begin(),runtime.end());
    for(const auto &entry:fs::directory_iterator(out/"verilated"))track(entry.path());
    std::ifstream makefile(out/"verilated/VSoCHarness.mk"); std::string line; fs::path harness, verilator_root;
    while(std::getline(makefile,line)){
        size_t start=std::string::npos;
        if(line.rfind("verilator-smoke.o:",0)==0)start=std::strlen("verilator-smoke.o:");
        else if(line.rfind("VERILATOR_ROOT",0)==0&&line.find('=')!=std::string::npos)start=line.find('=')+1;
        if(start!=std::string::npos){
            auto first=line.find_first_not_of(" \t",start);require(first!=std::string::npos,"missing Verilator build path");
            fs::path p=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
            if(line.rfind("verilator-smoke.o:",0)==0)harness=p;else verilator_root=p;
        }
    }
    require(harness.is_absolute(),"retained Verilator makefile must name its absolute verilator-smoke.cpp prerequisite");
    track(harness);track(harness.parent_path()/"mini-loader.h");manifest["verilator_harness_source"]=harness.string();
    require(verilator_root.is_absolute()&&fs::is_directory(verilator_root/"include"),"retained makefile must identify its Verilator installation");
    for(const auto &entry:fs::recursive_directory_iterator(verilator_root/"include"))
        if(entry.path().extension()==".cpp"||entry.path().extension()==".h"||entry.path().extension()==".mk")track(entry.path());
    manifest["verilator_installation"]={{"root",verilator_root.string()},{"cli_version",capture({"verilator","--version"},10000)}};
    auto unchanged=[&]{for(auto it=manifest["inputs"].begin();it!=manifest["inputs"].end();++it)
        require(hash_file(it.key())==it.value(),"build input changed during PGO: "+it.key());};
    std::ofstream build_log(out/"build.log");
    auto command=[&](const std::vector<std::string> &args){
        manifest["commands"].push_back(args); build_log<<Json(args).dump()<<'\n'; build_log.flush();
        const std::string output=capture(args,600000,nullptr,true);build_log<<output; build_log.flush();
        std::istringstream lines(output);std::string diagnostic;
        while(std::getline(lines,diagnostic))require(diagnostic.find("warning:")==std::string::npos||diagnostic.find("profile")==std::string::npos,
            "profile warning invalidates matched build: "+diagnostic);
        std::ofstream(out/"build.json")<<manifest.dump(2)<<'\n';
    };
    auto build=[&](const std::string &profile_flag){
        require(flock(lock,LOCK_SH)==0,"cannot acquire build lock");
        unchanged();
        std::vector<std::string> flags={"-O3","-march=native","-DNDEBUG",profile_flag};
        if(lto)flags.insert(flags.end(),{"-flto","-fuse-ld=lld"});
        std::vector<std::string> args={"clang","-std=c17"}; args.insert(args.end(),flags.begin(),flags.end());
        args.insert(args.end(),{"-Wall","-Wextra","-Werror",(repo/"sims/native/mini-smoke.c").string()});
        args.insert(args.end(),runtime.begin(),runtime.end());
        args.insert(args.end(),{"-pthread","-ldl","-o",(out/"mini-smoke").string()}); command(args);
        args={"clang","-std=c17"}; args.insert(args.end(),flags.begin(),flags.end());
        args.insert(args.end(),{"-shared","-fPIC",(out/"native.c").string(),"-o",(out/"native.so").string()}); command(args);
        for(const auto &entry:fs::directory_iterator(out/"verilated"))
            if(entry.path().extension()==".o"||entry.path().extension()==".a"||entry.path().extension()==".gch"||entry.path().extension()==".pch"||entry.path().extension()==".d"||entry.path().filename()=="VSoCHarness")fs::remove(entry.path());
        const std::string link_options=profile_flag+(lto?" -flto -fuse-ld=lld":"");
        const std::string options="-O3 -march=native -DNDEBUG "+link_options;
        command({"make","-C",(out/"verilated").string(),"-f","VSoCHarness.mk","-j","4","CXX=clang++","LINK=clang++",
            "OPT_FAST="+options,"OPT_SLOW="+options,"OPT_GLOBAL="+options,"OPT=","CXXFLAGS=","USER_CPPFLAGS=","USER_LDFLAGS=","OBJCACHE=",
            "VM_USER_CFLAGS="+options+" -DRDS_VERILATOR_THREADS=1","LDFLAGS="+options});
        unchanged();require(flock(lock,LOCK_UN)==0,"cannot release build lock");
    };
    auto configuration=[&](const std::string &profile_flag,bool training){
        Json flags=Json::array({"-O3","-march=native","-DNDEBUG",profile_flag});
        if(lto){flags.push_back("-flto");flags.push_back("-fuse-ld=lld");}
        return Json{{"purpose",training?"Instrumented training; rates are not production performance":"Matched profile-use single-worker performance"},
            {"cpu",0},{"trials",training?1:5},{"boots",training?100:1000},
            {"loops",training?Json::array({0,256,1024}):Json::array({256})},
            {"validation_loops",training?Json::array():Json::array({0,1024})},{"validation_boots",100},
            {"compiler",compiler},{"compiler_flags",flags},{"variants",Json::array({
                {{"name","native-pgo"},{"kind","native"},{"executable",(out/(training?"mini-smoke.instrumented":"mini-smoke")).string()},
                 {"model",model.string()},{"library",(out/(training?"native.instrumented.so":"native.so")).string()},{"flags",native_flags},{"compiler",compiler},{"compiler_flags",flags}},
                {{"name","verilator-pgo"},{"kind","verilator"},{"executable",(out/(training?"VSoCHarness.instrumented":"verilated/VSoCHarness")).string()},
                 {"compiler",compiler},{"compiler_flags",flags}}})}};
    };
    build("-fprofile-instr-generate");
    require(flock(lock,LOCK_SH)==0,"cannot acquire training-artifact lock");
    fs::copy_file(out/"mini-smoke",out/"mini-smoke.instrumented");
    fs::copy_file(out/"native.so",out/"native.instrumented.so");
    fs::copy_file(out/"verilated/VSoCHarness",out/"VSoCHarness.instrumented");
    for(const char *name:{"mini-smoke.instrumented","native.instrumented.so","VSoCHarness.instrumented"})
        manifest["instrumented_artifacts"][(out/name).string()]=hash_file((out/name).string());
    std::ofstream(out/"training-config.json")<<configuration("-fprofile-instr-generate",true).dump(2)<<'\n';
    setenv("LLVM_PROFILE_FILE",(out/"profiles/%m-%p.profraw").c_str(),1);
    require(flock(lock,LOCK_EX)==0,"cannot acquire training lock");
    benchmark(out/"training-config.json",out/"training.json");
    unsetenv("LLVM_PROFILE_FILE"); require(flock(lock,LOCK_UN)==0,"cannot release training lock");
    require(flock(lock,LOCK_SH)==0,"cannot acquire profile merge lock");
    std::vector<std::string> merge={"llvm-profdata","merge","-o",(out/"merged.profdata").string()};
    const std::vector<std::string> profile_functions={"rds_eval","phase_0_0","_ZN11VSoCHarness9eval_stepEv"};
    std::vector<unsigned> coverage(profile_functions.size());
    for(const auto &entry:fs::directory_iterator(out/"profiles"))if(entry.path().extension()==".profraw"){
        merge.push_back(entry.path());manifest["raw_profiles"][entry.path().string()]=hash_file(entry.path());
        for(size_t i=0;i<profile_functions.size();++i){
            std::string info=capture({"llvm-profdata","show","--function="+profile_functions[i],entry.path().string()},300000);
            size_t pos=info.find("Function count:");
            if(pos!=std::string::npos&&std::stoull(info.substr(pos+15))>0)++coverage[i];
        }
    }
    for(size_t i=0;i<coverage.size();++i)require(coverage[i]==3,"each training workload must record "+profile_functions[i]);
    manifest["profile_module_coverage"]=coverage;
    require(merge.size()>4,"instrumented runs produced no profiles"); std::sort(merge.begin()+4,merge.end()); command(merge);
    manifest["profile_sha256"]=hash_file((out/"merged.profdata").string());
    track(out/"merged.profdata");
    const std::string use="-fprofile-instr-use="+(out/"merged.profdata").string(); build(use);
    std::ofstream(out/"config.json")<<configuration(use,false).dump(2)<<'\n';
    require(flock(lock,LOCK_EX)==0,"cannot acquire timing lock"); benchmark(out/"config.json",out/"benchmark.json");
    unchanged();manifest["complete"]=true; std::ofstream(out/"build.json")<<manifest.dump(2)<<'\n';
}
int main(int argc, char **argv) {
    try {
        const bool lto=argc==7&&std::string(argv[1])=="--pgo-lto";
        const bool pgo=lto||(argc==7&&std::string(argv[1])=="--pgo");
        require(argc == 3||pgo, "usage: benchmark-single config.json output.json | --pgo[-lto] MODEL C_SOURCE VERILATED_DIR OUTPUT_DIR FLAGS");
        int lock = open("/tmp/rhodium-single-worker-perf.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        require(lock >= 0, "cannot open benchmark lock");
        if(pgo){
            size_t end=0; unsigned long long flags=std::stoull(argv[6],&end,0);
            require(end==std::strlen(argv[6])&&flags<=UINT32_MAX,"invalid native flags");
            profile_build(argv[2],argv[3],argv[4],argv[5],static_cast<uint32_t>(flags),lock,lto);close(lock);return 0;
        }
        require(flock(lock, LOCK_EX) == 0, "cannot acquire benchmark lock");
        benchmark(argv[1], argv[2]); close(lock); return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
