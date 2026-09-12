// Requires a complete eight-core pointer-chase timing matrix and renders its scaling table.
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using J = nlohmann::json;
int main(int argc, char **argv) { try {
  if (argc != 2) throw std::runtime_error("chase-report aggregated-report.json");
  J report; std::ifstream file(argv[1]); if (!file) throw std::runtime_error("missing report"); file >> report;
  const auto &sig = report.at("signature"), &times = report.at("timings");
  if (sig.at("workload") != "bank-striped-pointer-chase" || sig.at("harts") != 8)
    throw std::runtime_error("requires the eight-core pointer-chase workload");
  for (const auto &hart : sig.at("harts_detail"))
    if (hart.at("heartbeat") != true || hart.at("progress") != sig.at("rounds"))
      throw std::runtime_error("missing hart progress");
  for (unsigned n : {1u, 2u, 4u, 8u}) for (auto engine : {"native", "verilator"})
    if (times.at(std::string(engine) + "-" + std::to_string(n)).at("samples").empty())
      throw std::runtime_error("requires every matrix cell");
  std::cout << "| Workers / threads | Runs (rsim / Verilator) | rsim seconds | Verilator seconds | rsim speedup | rsim total seconds | Verilator total seconds |\n"
               "|---:|---:|---:|---:|---:|---:|---:|\n" << std::fixed << std::setprecision(3);
  for (unsigned n : {1u, 2u, 4u, 8u}) {
    auto key = std::to_string(n); const auto &a = times.at("native-" + key), &b = times.at("verilator-" + key);
    double native = a.at("median_seconds"), verilator = b.at("median_seconds");
    std::cout << "| " << n << " | " << a.at("samples").size() << " / " << b.at("samples").size() << " | " << native << " | " << verilator << " | " << verilator/native
              << "× | " << a.at("whole_median_seconds").get<double>() << " | " << b.at("whole_median_seconds").get<double>() << " |\n";
  }
  std::cout << "\nOne-to-eight scaling: rsim " << times.at("native-1").at("median_seconds").get<double>()/times.at("native-8").at("median_seconds").get<double>()
            << "×; Verilator " << times.at("verilator-1").at("median_seconds").get<double>()/times.at("verilator-8").at("median_seconds").get<double>() << "×.\n";
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
