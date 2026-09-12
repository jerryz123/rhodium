// Checks timing-gate validation, shared-inode staging, subprocess isolation, and immutable artifact audits.
#include <sched.h>
#define main benchmark_program_main
#include "../../sims/native/benchmark-single.cpp"
#undef main

template<class F> static void rejects(F f, const std::string &message) {
    bool failed = false;
    try { f(); } catch (const std::exception &e) { failed = std::string(e.what()).find(message) != std::string::npos; }
    require(failed, "expected rejection: " + message);
}
int main() {
    try {
        Json row = {{"workers", 1}, {"context_threads", 1}, {"seconds", 1}, {"cycles", 2609}, {"polls", 82}, {"digest", "2bf4058d07c9c1b0"}};
        for (const char *kind : {"native", "verilator"}) {
            Json v = {{"name", kind}, {"kind", kind}}; validate(row, v);
            for (const Json &bad : {Json(6), Json(1.5)}) {
                Json r = row; r["workers"] = bad; rejects([&] { validate(r, v); }, "exactly one");
            }
            for (const char *field : {"seconds", "digest", "cycles"}) {
                Json r = row; r[field] = field == std::string("digest") ? Json("bad") : Json(0);
                rejects([&] { validate(r, v); }, "malformed");
            }
        }
        Json v = {{"name", "v"}, {"kind", "verilator"}}, r = row;
        r.erase("context_threads"); rejects([&] { validate(r, v); }, "exactly one");
        r["context_threads"] = 4; rejects([&] { validate(r, v); }, "exactly one");
        require(median({3, 1, 2}) == 2, "median");
        require(capture({"/bin/printf", "%s", "space ' $literal"}, 1000) == "space ' $literal", "argument quoting");
        setenv("RDS_TEST_REMOVE", "yes", 1);
        auto environment = capture({"/usr/bin/env"}, 1000, Json{{"RDS_KEEP", "present"}});
        require(environment.find("RDS_TEST_REMOVE") == std::string::npos && environment.find("RDS_KEEP=present") != std::string::npos, "environment filtering");
        setenv("MAKEFLAGS","unexpected-build-flags",1);setenv("CXXFLAGS","unexpected-cxx-flags",1);
        environment=capture({"/usr/bin/env"},1000,nullptr,true);
        require(environment.find("unexpected-build-flags")==std::string::npos&&environment.find("unexpected-cxx-flags")==std::string::npos,"build environment filtering");
        unsetenv("MAKEFLAGS");unsetenv("CXXFLAGS");
        rejects([&] { capture({"/bin/sleep", "1"}, 10); }, "timed out");
        char temporary[] = "/tmp/rhodium-benchmark-cpp.XXXXXX";
        require(mkdtemp(temporary) != nullptr, "temporary directory"); fs::path dir = temporary;
        auto write = [&](const fs::path &p, const std::string &s) { std::ofstream(p) << s; };
        {
            write(dir/"large-library",std::string(70001,'x'));
            write(dir/"small-library",std::string(19,'y'));
            LibrarySlot slot(dir/"slot");auto identity=slot.identity;
            for(const char *name:{"large-library","small-library","large-library"}){
                auto source=dir/name;slot.stage(source,hash_file(source));
                struct stat info{};require(stat(slot.path.c_str(),&info)==0,"slot stat");
                require(identity["inode"]==info.st_ino&&identity["device"]==info.st_dev,"staging replaced inode");
                require(fs::file_size(slot.path)==fs::file_size(source)&&hash_file(slot.path)==hash_file(source),"staging retained stale bytes");
            }
            rejects([&]{LibrarySlot duplicate(dir/"slot");},"must be new");
        }
        const std::string good = row.dump();
        write(dir / "native", "#!/bin/sh\n# Returns deterministic native trace data for the compiled benchmark fixture.\nprintf '%s\\n' '" + good + "'\n");
        write(dir / "verilator", "#!/bin/sh\n# Returns deterministic reference trace data for the compiled benchmark fixture.\nprintf '%s\\n' '" + good + "'\n");
        fs::permissions(dir / "native", fs::perms::owner_all); fs::permissions(dir / "verilator", fs::perms::owner_all);
        cpu_set_t mask; CPU_ZERO(&mask); require(sched_getaffinity(0, sizeof mask, &mask) == 0, "get affinity");
        unsigned cpu = 0; while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &mask)) ++cpu;
        Json flags = Json::array({"-O3", "-march=native", "-DNDEBUG"});
        Json config = {{"cpu", cpu}, {"trials", 2}, {"boots", 1}, {"loops", Json::array({0, 256})}, {"validation_loops", Json::array({1024})}, {"compiler", "fixture"}, {"compiler_flags", flags},
            {"variants", Json::array({{{"name", "native"}, {"kind", "native"}, {"flags", 0}, {"model", "native"}, {"library", "native"}, {"executable", "native"}, {"compiler", "fixture"}, {"compiler_flags", flags}},
                {{"name", "verilator"}, {"kind", "verilator"}, {"executable", "verilator"}, {"compiler", "fixture"}, {"compiler_flags", flags}}})}};
        auto run = [&] { write(dir / "config.json", config.dump()); benchmark(dir / "config.json", dir / "report.json"); };
        run(); Json report; std::ifstream(dir / "report.json") >> report;
        require(report["complete"] == true && report["rows"].size() == 8 && report["validation"].size() == 2 && report["summaries"][0]["ratios"]["native"] == 1, "complete report");
        require(report["rows"][2]["name"] == "verilator", "rotated order");
        config["shared_library_slot"]=true;write(dir/"config.json",config.dump());
        benchmark(dir/"config.json",dir/"staged-report.json");
        std::ifstream(dir/"staged-report.json")>>report;
        require(report["complete"]==true,"staged report incomplete");
        for(const auto &timing:report["rows"])if(timing["name"]=="native")
            require(timing["library_slot"]==report["library_slot"]&&timing["library_sha256"]==hash_file(dir/"native"),"missing staged provenance");
        config.erase("shared_library_slot");
        config["variants"][0]["compiler"] = "different"; rejects(run, "compiler version and flags"); config["variants"][0]["compiler"] = "fixture";
        r = row; r["polls"] = 83;
        write(dir / "native", "#!/bin/sh\n# Emits a deliberately different fixture trace.\nprintf '%s\\n' '" + r.dump() + "'\n");
        rejects(run, "trace mismatch");
        write(dir / "native", "#!/bin/sh\n# Mutates a tracked artifact during the fixture timing run.\nprintf '# changed\\n' >> \"$0\"\nprintf '%s\\n' '" + good + "'\n");
        rejects(run, "artifact changed"); fs::remove_all(dir);
        std::cout << "Compiled single-worker benchmark gate passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
