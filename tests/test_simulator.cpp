#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/codegen.hpp"
#include "tmc/simulator.hpp"
#include <fstream>
#include <sstream>

namespace tmc {
namespace {

// Create a simple TM that accepts strings starting with 'a'
TM MakeStartsWithA() {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};

  // On 'a', accept
  tm.AddTransition("q0", 'a', 'a', Dir::S, "qA");
  // On 'b', reject
  tm.AddTransition("q0", 'b', 'b', Dir::S, "qR");
  // On blank, reject (empty string)
  tm.AddTransition("q0", kBlank, kBlank, Dir::S, "qR");

  tm.Finalize();
  return tm;
}

TEST(SimulatorTest, AcceptStartsWithA) {
  TM tm = MakeStartsWithA();
  Simulator sim(tm);

  auto result = sim.Run("a");
  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.steps, 1);

  result = sim.Run("abc");
  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.steps, 1);
}

TEST(SimulatorTest, RejectStartsWithA) {
  TM tm = MakeStartsWithA();
  Simulator sim(tm);

  auto result = sim.Run("b");
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.steps, 1);

  result = sim.Run("");
  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.steps, 1);
}

// Create a TM that accepts a^n b^n
TM MakeAnBn() {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};
  tm.tape_alphabet.insert('X');
  tm.tape_alphabet.insert('Y');

  // q0: mark 'a' as 'X', go right to find 'b'
  tm.AddTransition("q0", 'a', 'X', Dir::R, "q1");
  tm.AddTransition("q0", 'Y', 'Y', Dir::R, "q3");  // all a's matched, check b's
  tm.AddTransition("q0", kBlank, kBlank, Dir::S, "qA");  // empty string

  // q1: scan right past 'a's and 'Y's to find 'b'
  tm.AddTransition("q1", 'a', 'a', Dir::R, "q1");
  tm.AddTransition("q1", 'Y', 'Y', Dir::R, "q1");
  tm.AddTransition("q1", 'b', 'Y', Dir::L, "q2");  // mark 'b' as 'Y'
  tm.AddTransition("q1", kBlank, kBlank, Dir::S, "qR");  // no matching b

  // q2: go back left to find next 'a'
  tm.AddTransition("q2", 'a', 'a', Dir::L, "q2");
  tm.AddTransition("q2", 'Y', 'Y', Dir::L, "q2");
  tm.AddTransition("q2", 'X', 'X', Dir::R, "q0");  // found start, go to q0

  // q3: verify all b's are marked
  tm.AddTransition("q3", 'Y', 'Y', Dir::R, "q3");
  tm.AddTransition("q3", kBlank, kBlank, Dir::S, "qA");  // all matched
  tm.AddTransition("q3", 'b', 'b', Dir::S, "qR");  // unmatched b

  tm.Finalize();
  return tm;
}

TEST(SimulatorTest, AnBnAccepts) {
  TM tm = MakeAnBn();
  Simulator sim(tm);

  EXPECT_TRUE(sim.Run("").accepted);
  EXPECT_TRUE(sim.Run("ab").accepted);
  EXPECT_TRUE(sim.Run("aabb").accepted);
  EXPECT_TRUE(sim.Run("aaabbb").accepted);
}

TEST(SimulatorTest, AnBnRejects) {
  TM tm = MakeAnBn();
  Simulator sim(tm);

  EXPECT_FALSE(sim.Run("a").accepted);
  EXPECT_FALSE(sim.Run("b").accepted);
  EXPECT_FALSE(sim.Run("aab").accepted);
  EXPECT_FALSE(sim.Run("abb").accepted);
  EXPECT_FALSE(sim.Run("ba").accepted);
}

TEST(SimulatorTest, StepCounting) {
  TM tm = MakeAnBn();
  Simulator sim(tm);

  // Count steps for various inputs
  auto r1 = sim.Run("ab");
  auto r2 = sim.Run("aabb");

  // aabb should take more steps than ab
  EXPECT_GT(r2.steps, r1.steps);
}

TEST(SimulatorTest, StepByStep) {
  TM tm = MakeStartsWithA();
  Simulator sim(tm);

  sim.Reset("ab");
  EXPECT_FALSE(sim.Halted());
  EXPECT_EQ(sim.Steps(), 0);

  bool ran = sim.Step();
  EXPECT_FALSE(ran);  // should halt after first step
  EXPECT_TRUE(sim.Halted());
  EXPECT_TRUE(sim.Accepted());
  EXPECT_EQ(sim.Steps(), 1);
}

// Exact step count verification for AnBn TM.
// Manually traced:
//   "ab":   q0→q1→q2→q0→q3→qA = 5 transitions
//   "aabb": 13 transitions (traced by hand)
TEST(SimulatorTest, ExactStepCounts) {
  TM tm = MakeAnBn();
  Simulator sim(tm);

  EXPECT_EQ(sim.Run("ab").steps, 5);
  EXPECT_EQ(sim.Run("aabb").steps, 13);
  EXPECT_EQ(sim.Run("").steps, 1);    // q0 reads blank → qA
  EXPECT_EQ(sim.Run("a").steps, 2);   // q0→q1, q1 reads blank → qR
  EXPECT_EQ(sim.Run("b").steps, 1);   // q0 reads 'b' → qR
}

// Cross-check: Run() step count must equal Step()-by-step count.
// This is the critical invariant for competition scoring.
TEST(SimulatorTest, RunVsStepCrossCheck) {
  TM tm = MakeAnBn();
  Simulator sim(tm);

  std::vector<std::string> inputs = {"", "a", "b", "ab", "ba", "aabb",
                                      "aaabbb", "aab", "abb", "aaaa"};
  for (const auto& input : inputs) {
    auto run_result = sim.Run(input);

    sim.Reset(input);
    while (sim.Step()) {}

    EXPECT_EQ(run_result.steps, sim.Steps())
        << "Step count mismatch for input \"" << input << "\": "
        << "Run()=" << run_result.steps << " Step()=" << sim.Steps();
    EXPECT_EQ(run_result.accepted, sim.Accepted())
        << "Accept/reject mismatch for input \"" << input << "\"";
  }
}

// Cross-check Run() vs Step() on a real YAML TM from examples.
TEST(SimulatorTest, RunVsStepOnYAML) {
  std::ifstream ifs(std::string(EXAMPLES_DIR) + "/triangular.tm");
  ASSERT_TRUE(ifs.good()) << "Cannot open triangular.tm";
  std::stringstream buf;
  buf << ifs.rdbuf();
  TM tm = FromYAML(buf.str());

  Simulator sim(tm, 10000000);

  // Test a range of inputs
  std::vector<std::string> inputs = {
      "",           // empty
      "ab",         // n=1, T(1)=1 → accept
      "a",          // n=1, 0 b's → reject
      "aabbb",      // n=2, T(2)=3 → accept
      "aabb",       // n=2, 2 b's ≠ 3 → reject
  };

  for (const auto& input : inputs) {
    auto run_result = sim.Run(input);

    sim.Reset(input);
    while (sim.Step()) {}

    EXPECT_EQ(run_result.steps, sim.Steps())
        << "YAML TM step mismatch for \"" << input << "\": "
        << "Run()=" << run_result.steps << " Step()=" << sim.Steps();
    EXPECT_EQ(run_result.accepted, sim.Accepted())
        << "YAML TM accept mismatch for \"" << input << "\"";
  }
}

}  // namespace
}  // namespace tmc
