#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/simulator.hpp"

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

}  // namespace
}  // namespace tmc
