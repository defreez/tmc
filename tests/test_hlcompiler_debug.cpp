#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/simulator.hpp"
#include "tmc/codegen.hpp"
#include <iostream>

namespace tmc {
namespace {

TEST(HLDebug, JustCount) {
  std::string src = R"(
alphabet input: [a]
n = count(a)
accept
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);
  Simulator sim(tm);

  EXPECT_TRUE(sim.Run("").accepted);
  EXPECT_TRUE(sim.Run("a").accepted);
}

TEST(HLDebug, CountComparison) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
return count(b) == n
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::cout << "\n=== Generated TM ===\n";
  std::cout << ToYAML(tm);
  std::cout << "===================\n";

  Simulator sim(tm);

  // Trace on "ab"
  sim.Reset("ab");
  std::cout << "\n'ab' trace:\n";
  for (int i = 0; i < 50 && !sim.Halted(); ++i) {
    auto cfg = sim.CurrentConfig();
    std::cout << "  " << i << ": " << cfg.state << " @" << cfg.head << " [";
    for (size_t j = 0; j < cfg.tape.size(); ++j) {
      if (j == static_cast<size_t>(cfg.head)) std::cout << ">";
      std::cout << cfg.tape[j];
    }
    std::cout << "]\n";
    sim.Step();
  }
  std::cout << "  Final: " << (sim.Accepted() ? "ACCEPT" : "REJECT")
            << " in " << sim.Steps() << " steps\n";

  // Empty trace
  sim.Reset("");
  std::cout << "\nEmpty trace:\n";
  for (int i = 0; i < 30 && !sim.Halted(); ++i) {
    auto cfg = sim.CurrentConfig();
    std::cout << "  " << i << ": " << cfg.state << " @" << cfg.head << " [";
    for (size_t j = 0; j < cfg.tape.size(); ++j) {
      if (j == static_cast<size_t>(cfg.head)) std::cout << ">";
      std::cout << cfg.tape[j];
    }
    std::cout << "]\n";
    sim.Step();
  }
  std::cout << "  Final: " << (sim.Accepted() ? "ACCEPT" : "REJECT")
            << " in " << sim.Steps() << " steps\n";
}

}  // namespace
}  // namespace tmc
