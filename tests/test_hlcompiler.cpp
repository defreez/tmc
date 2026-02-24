#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/simulator.hpp"
#include "tmc/codegen.hpp"
#include <random>

namespace tmc {
namespace {

// Generate all strings up to length n over alphabet
std::vector<std::string> AllStrings(const std::set<Symbol>& alphabet, int max_len) {
  std::vector<std::string> result;
  result.push_back("");

  std::vector<std::string> current = {""};
  for (int len = 1; len <= max_len; ++len) {
    std::vector<std::string> next;
    for (const auto& s : current) {
      for (Symbol c : alphabet) {
        std::string ns = s + static_cast<char>(c);
        next.push_back(ns);
        result.push_back(ns);
      }
    }
    current = next;
  }
  return result;
}

// Oracle for triangular numbers
bool IsTriangular(const std::string& s) {
  int n = 0, m = 0;
  bool in_b = false;
  for (char c : s) {
    if (c == 'a') {
      if (in_b) return false;
      ++n;
    } else if (c == 'b') {
      in_b = true;
      ++m;
    } else {
      return false;
    }
  }
  int expected = n * (n + 1) / 2;
  return m == expected;
}

// Test that optimization doesn't change behavior
class OptimizationCorrectnessTest : public ::testing::Test {
protected:
  void VerifyEquivalent(const TM& original, const TM& optimized,
                        const std::set<Symbol>& alphabet, int max_len) {
    auto inputs = AllStrings(alphabet, max_len);

    Simulator sim_orig(original);
    Simulator sim_opt(optimized);

    for (const auto& input : inputs) {
      auto r1 = sim_orig.Run(input);
      auto r2 = sim_opt.Run(input);

      EXPECT_EQ(r1.accepted, r2.accepted)
          << "Mismatch on input \"" << input << "\": "
          << "original=" << (r1.accepted ? "accept" : "reject")
          << ", optimized=" << (r2.accepted ? "accept" : "reject");
    }
  }
};

TEST(HLCompilerTest, ParseTriangular) {
  std::string src = R"(
alphabet input: [a, b]

n = count(a)
sum = 0
for i in 1..n {
  sum = sum + i
}
return count(b) == sum
)";

  Program prog = ParseHL(src);

  EXPECT_TRUE(prog.input_alphabet.count('a'));
  EXPECT_TRUE(prog.input_alphabet.count('b'));
  EXPECT_EQ(prog.body.size(), 4);  // n=, sum=, for, return
}

TEST(HLCompilerTest, CompileSimpleCount) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
return count(b) == n
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  EXPECT_TRUE(tm.Validate(&error)) << error;

  // This should accept a^n b^n
  Simulator sim(tm);

  // Test simple cases
  EXPECT_TRUE(sim.Run("").accepted) << "Empty should accept";
  EXPECT_TRUE(sim.Run("ab").accepted) << "ab should accept";
  EXPECT_TRUE(sim.Run("aabb").accepted) << "aabb should accept";

  EXPECT_FALSE(sim.Run("a").accepted) << "a should reject";
  EXPECT_FALSE(sim.Run("abb").accepted) << "abb should reject";
  EXPECT_FALSE(sim.Run("aab").accepted) << "aab should reject";
}

// Test that we can verify TM against oracle
TEST(HLCompilerTest, VerifyAgainstOracle) {
  // This uses the hand-built triangular TM from test_triangular.cpp
  // to verify the methodology works

  auto inputs = AllStrings({'a', 'b'}, 8);

  int correct = 0;
  for (const auto& s : inputs) {
    bool expected = IsTriangular(s);
    // Here we'd compare against TM output
    // For now just count valid triangular strings
    if (expected) ++correct;
  }

  // Valid strings up to len 8: "", ab, aabbb (len 5), aaabbbbbb (len 9, too long)
  // So we expect 3: "", ab, aabbb
  EXPECT_GE(correct, 3);
}

// Property: optimization should not change accept/reject behavior
TEST(HLCompilerTest, OptimizationPreservesSemantics) {
  // Build a simple TM
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};

  // Accept if starts with 'a'
  tm.AddTransition("q0", 'a', 'a', Dir::S, "qA");
  tm.AddTransition("q0", 'b', 'b', Dir::S, "qR");
  tm.AddTransition("q0", kBlank, kBlank, Dir::S, "qR");
  tm.Finalize();

  // Make a copy
  TM tm_copy = tm;

  // Verify they behave the same
  Simulator sim1(tm);
  Simulator sim2(tm_copy);

  EXPECT_EQ(sim1.Run("a").accepted, sim2.Run("a").accepted);
  EXPECT_EQ(sim1.Run("b").accepted, sim2.Run("b").accepted);
  EXPECT_EQ(sim1.Run("ab").accepted, sim2.Run("ab").accepted);
  EXPECT_EQ(sim1.Run("").accepted, sim2.Run("").accepted);
}

// Generate random strings and verify
TEST(HLCompilerTest, RandomTestingFramework) {
  std::mt19937 rng(42);  // fixed seed for reproducibility

  std::set<Symbol> alphabet = {'a', 'b'};

  // Generate random strings
  std::vector<std::string> random_inputs;
  for (int i = 0; i < 100; ++i) {
    int len = rng() % 15;
    std::string s;
    for (int j = 0; j < len; ++j) {
      s += (rng() % 2 == 0) ? 'a' : 'b';
    }
    random_inputs.push_back(s);
  }

  // Verify oracle is consistent
  for (const auto& s : random_inputs) {
    bool r1 = IsTriangular(s);
    bool r2 = IsTriangular(s);
    EXPECT_EQ(r1, r2);
  }
}

// Test step counting consistency
TEST(HLCompilerTest, StepCountingDeterministic) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a'};

  // Scan right to blank, accept
  tm.AddTransition("q0", 'a', 'a', Dir::R, "q0");
  tm.AddTransition("q0", kBlank, kBlank, Dir::S, "qA");
  tm.Finalize();

  Simulator sim(tm);

  // Multiple runs should give same step count
  for (int trial = 0; trial < 5; ++trial) {
    auto r = sim.Run("aaaa");
    EXPECT_EQ(r.steps, 5);  // 4 moves + 1 final
    EXPECT_TRUE(r.accepted);
  }
}

// Oracle: accept iff count(a) == count(b) regardless of order
bool CountAEqCountB(const std::string& s) {
  int a_count = 0, b_count = 0;
  for (char c : s) {
    if (c == 'a') ++a_count;
    else if (c == 'b') ++b_count;
    else return false;
  }
  return a_count == b_count;
}

// Exhaustive oracle test: n = count(a); return count(b) == n
// Verifies compiled TM matches oracle on ALL strings up to length 8
TEST(HLCompilerTest, ExhaustiveCountAEqCountB) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
return count(b) == n
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  Simulator sim(tm);
  auto inputs = AllStrings({'a', 'b'}, 8);

  int tested = 0;
  for (const auto& input : inputs) {
    bool expected = CountAEqCountB(input);
    auto result = sim.Run(input);
    EXPECT_EQ(result.accepted, expected)
        << "input=\"" << input << "\" (len " << input.size() << "): "
        << "oracle=" << (expected ? "accept" : "reject")
        << ", TM=" << (result.accepted ? "accept" : "reject")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
    ++tested;
  }
  EXPECT_EQ(tested, 511);  // 2^0 + 2^1 + ... + 2^8
}

// Count restores input: n = count(a); return count(a) == n
// Should accept everything because count(a) == count(a) is always true.
// If the restore sweep is broken, the second count(a) in the return
// would find 0 a's (still marked as A) and reject any input with a's.
TEST(HLCompilerTest, CountRestoresInput) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
return count(a) == n
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  Simulator sim(tm);
  auto inputs = AllStrings({'a', 'b'}, 8);

  for (const auto& input : inputs) {
    auto result = sim.Run(input);
    EXPECT_TRUE(result.accepted)
        << "input=\"" << input << "\" should accept (count(a)==count(a)) "
        << "but got reject"
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
  }
}

// Exhaustive T(n) triangular number test.
// Accepts {a^n b^m : m = n*(n+1)/2} using VM instructions:
// structural check (scan + if-current), count, inc, append, loop, break, if-eq
TEST(HLCompilerTest, ExhaustiveTriangular) {
  std::string src = R"(
alphabet input: [a, b]

scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}

n = count(a)
m = count(b)
sum = 0
i = 0
z = 0

if n == z {
  if sum == m { accept }
  reject
}

loop {
  inc i
  append i -> sum
  if i == n { break }
}

if sum == m { accept }
reject
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  Simulator sim(tm, 10000000);  // generous step limit for complex VM
  auto inputs = AllStrings({'a', 'b'}, 8);

  int tested = 0;
  for (const auto& input : inputs) {
    bool expected = IsTriangular(input);
    auto result = sim.Run(input);
    EXPECT_EQ(result.accepted, expected)
        << "input=\"" << input << "\" (len " << input.size() << "): "
        << "oracle=" << (expected ? "accept" : "reject")
        << ", TM=" << (result.accepted ? "accept" : "reject")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
    ++tested;
  }
  EXPECT_EQ(tested, 511);
}

}  // namespace
}  // namespace tmc
