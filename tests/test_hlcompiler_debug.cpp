#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/simulator.hpp"
#include <iostream>
#include <set>

namespace tmc {
namespace {

// Helper: compile and run, print trace on failure
RunResult CompileAndRun(const std::string& src, const std::string& input,
                        int max_steps = 1000000) {
  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);
  std::string error;
  if (!tm.Validate(&error)) {
    std::cerr << "TM validation failed: " << error << "\n";
  }
  Simulator sim(tm, max_steps);
  return sim.Run(input);
}

// Helper: compile, run, and print step-by-step trace
RunResult TraceRun(const std::string& src, const std::string& input,
                   int max_trace = 200, int max_steps = 10000) {
  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);
  Simulator sim(tm, max_steps);
  sim.Reset(input);

  std::cout << "\nTrace for \"" << input << "\":\n";
  for (int i = 0; i < max_trace && !sim.Halted(); ++i) {
    auto cfg = sim.CurrentConfig();
    std::cout << "  " << i << ": " << cfg.state << " @" << cfg.head << " [";
    for (size_t j = 0; j < cfg.tape.size(); ++j) {
      if (j == static_cast<size_t>(cfg.head)) std::cout << ">";
      std::cout << cfg.tape[j];
    }
    std::cout << "]\n";
    sim.Step();
  }
  auto result = sim.Run(input);
  std::cout << "  Result: " << (result.accepted ? "ACCEPT" : "REJECT")
            << " in " << result.steps << " steps"
            << (result.hit_limit ? " (HIT LIMIT)" : "") << "\n";
  return result;
}

// ============================================================
// TIER 1: Single VM instructions in isolation
// ============================================================

// inc: just increment a variable and accept
TEST(VMInc, IncAndAccept) {
  std::string src = R"(
alphabet input: [a]
x = 0
inc x
accept
)";
  // Should accept any input - inc doesn't depend on input
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted);
}

// inc: increment twice
TEST(VMInc, IncTwice) {
  std::string src = R"(
alphabet input: [a]
x = 0
inc x
inc x
accept
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
}

// inc: increment a count variable
TEST(VMInc, IncAfterCount) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
inc n
accept
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted);
}

// inc + if-eq: inc x from 0, check if x == count(a)
// For input "a": count(a)=1, x=0, inc x -> x=1, so x==n -> accept
TEST(VMIncIfEq, IncOnceMatchesOneA) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
x = 0
inc x
if x == n { accept }
reject
)";
  EXPECT_FALSE(CompileAndRun(src, "").accepted) << "n=0, x=1, should reject";
  EXPECT_TRUE(CompileAndRun(src, "a").accepted) << "n=1, x=1, should accept";
  EXPECT_FALSE(CompileAndRun(src, "aa").accepted) << "n=2, x=1, should reject";
  EXPECT_FALSE(CompileAndRun(src, "b").accepted) << "n=0, x=1, should reject";
}

// Simpler: two zero vars, if-eq should say they're equal
TEST(VMIfEq, TwoZerosEqual) {
  std::string src = R"(
alphabet input: [a]
x = 0
y = 0
if x == y { accept }
reject
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
}

// Two counts of same symbol should be equal
TEST(VMIfEq, SameCountsEqual) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(a)
if n == m { accept }
reject
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted);
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted);
}

// Different counts should not be equal (unless they happen to match)
TEST(VMIfEq, DifferentCounts) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
if n == m { accept }
reject
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted) << "0==0";
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted) << "1==1";
  EXPECT_TRUE(CompileAndRun(src, "aabb").accepted) << "2==2";
  EXPECT_FALSE(CompileAndRun(src, "a").accepted) << "1!=0";
  EXPECT_FALSE(CompileAndRun(src, "aab").accepted) << "2!=1";
  EXPECT_FALSE(CompileAndRun(src, "abb").accepted) << "1!=2";
}

// if-eq with else branch
TEST(VMIfEq, ElseBranch) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
if n == m { accept } else { reject }
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted);
  EXPECT_FALSE(CompileAndRun(src, "a").accepted);
  EXPECT_FALSE(CompileAndRun(src, "b").accepted);
}

// ============================================================
// TIER 2: Loop + break
// ============================================================

// Simple loop with immediate break
TEST(VMLoop, ImmediateBreak) {
  std::string src = R"(
alphabet input: [a]
x = 0
loop {
  break
}
accept
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted);
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
}

// Loop that increments then breaks
TEST(VMLoop, IncThenBreak) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
x = 0
loop {
  inc x
  if x == n { break }
}
accept
)";
  // This should terminate for any input with a's (n>0)
  // For n=0: inc x -> x=1, x==n -> 1==0 -> false, inc x -> x=2, ... infinite loop!
  // So this test only works for n>0
  EXPECT_TRUE(CompileAndRun(src, "a").accepted) << "n=1: loop once";
  EXPECT_TRUE(CompileAndRun(src, "aa").accepted) << "n=2: loop twice";
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted) << "n=3: loop thrice";
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted) << "n=1 with extra b";
}

// Loop counting: inc x in loop until x==n, then check x==n after loop
TEST(VMLoop, CountToN) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
x = 0
loop {
  inc x
  if x == n { break }
}
if x == n { accept }
reject
)";
  // Should accept anything with at least one 'a'
  EXPECT_TRUE(CompileAndRun(src, "a").accepted);
  EXPECT_TRUE(CompileAndRun(src, "aa").accepted);
  EXPECT_TRUE(CompileAndRun(src, "aab").accepted);
}

// ============================================================
// TIER 3: Append
// ============================================================

// Append: copy a count to another variable
TEST(VMAppend, CopyCountToVar) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
x = 0
append n -> x
if x == n { accept }
reject
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted) << "0==0";
  EXPECT_TRUE(CompileAndRun(src, "a").accepted) << "1==1";
  EXPECT_TRUE(CompileAndRun(src, "aa").accepted) << "2==2";
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted) << "3==3";
}

// Append twice doubles the value
TEST(VMAppend, AppendTwiceDoubles) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
x = 0
append n -> x
append n -> x
if x == m { accept }
reject
)";
  // x = 2*count(a), accept if count(b) == 2*count(a)
  EXPECT_TRUE(CompileAndRun(src, "").accepted) << "0==0";
  EXPECT_TRUE(CompileAndRun(src, "abb").accepted) << "2==2";
  EXPECT_TRUE(CompileAndRun(src, "aabbbb").accepted) << "4==4";
  EXPECT_FALSE(CompileAndRun(src, "ab").accepted) << "2!=1";
  EXPECT_FALSE(CompileAndRun(src, "aab").accepted) << "4!=1";
}

// ============================================================
// TIER 4: Composed - inc + append in loop
// ============================================================

// Sum 1..n = n*(n+1)/2 but just test sum of 1
TEST(VMCompose, SumOfOne) {
  // n=1: loop runs once, i=1, sum=1. Check sum==m.
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
sum = 0
i = 0
loop {
  inc i
  append i -> sum
  if i == n { break }
}
if sum == m { accept }
reject
)";
  // a^1 b^1: n=1, sum = T(1) = 1, m=1 -> accept
  EXPECT_TRUE(CompileAndRun(src, "ab", 10000000).accepted) << "T(1)=1";
  // a^1 b^2: T(1)=1, m=2 -> reject
  EXPECT_FALSE(CompileAndRun(src, "abb", 10000000).accepted) << "T(1)!=2";
}

// Sum of 1..2 = 3
TEST(VMCompose, SumOfTwo) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
sum = 0
i = 0
loop {
  inc i
  append i -> sum
  if i == n { break }
}
if sum == m { accept }
reject
)";
  // a^2 b^3: n=2, T(2) = 1+2 = 3, m=3 -> accept
  EXPECT_TRUE(CompileAndRun(src, "aabbb", 10000000).accepted) << "T(2)=3";
  // a^2 b^2: n=2, T(2)=3, m=2 -> reject
  EXPECT_FALSE(CompileAndRun(src, "aabb", 10000000).accepted) << "T(2)!=2";
}

// Sum of 1..3 = 6
TEST(VMCompose, SumOfThree) {
  std::string src = R"(
alphabet input: [a, b]
n = count(a)
m = count(b)
sum = 0
i = 0
loop {
  inc i
  append i -> sum
  if i == n { break }
}
if sum == m { accept }
reject
)";
  // a^3 b^6: n=3, T(3) = 1+2+3 = 6, m=6 -> accept
  EXPECT_TRUE(CompileAndRun(src, "aaabbbbbb", 10000000).accepted) << "T(3)=6";
  // a^3 b^5: T(3)=6, m=5 -> reject
  EXPECT_FALSE(CompileAndRun(src, "aaabbbbb", 10000000).accepted) << "T(3)!=5";
}

// Edge case: n=0 needs a guard before the loop since inc i makes i=1
// and 1 != 0 so break never fires. Handle with if n == z check.
TEST(VMCompose, SumOfZero) {
  std::string src = R"(
alphabet input: [a, b]
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
  // Empty: n=0, m=0, sum=0 -> 0==0 -> accept
  EXPECT_TRUE(CompileAndRun(src, "", 10000000).accepted) << "T(0)=0, m=0";
  // Just b's: n=0, m=1 -> 0!=1 -> reject
  EXPECT_FALSE(CompileAndRun(src, "b", 10000000).accepted) << "T(0)=0, m=1";
}

// ============================================================
// HELPERS for exhaustive testing
// ============================================================

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

bool IsAbStar(const std::string& s) {
  bool seen_b = false;
  for (char c : s) {
    if (c == 'a') {
      if (seen_b) return false;
    } else if (c == 'b') {
      seen_b = true;
    } else {
      return false;
    }
  }
  return true;
}

bool IsAnBn(const std::string& s) {
  if (!IsAbStar(s)) return false;
  int a_count = 0, b_count = 0;
  for (char c : s) {
    if (c == 'a') ++a_count;
    else ++b_count;
  }
  return a_count == b_count;
}

bool IsTriangularStrict(const std::string& s) {
  if (!IsAbStar(s)) return false;
  int n = 0, m = 0;
  for (char c : s) {
    if (c == 'a') ++n;
    else ++m;
  }
  return m == n * (n + 1) / 2;
}

// ============================================================
// TIER 5: Structural check (imperative scan + if-current)
// ============================================================

// Verify a*b* form using scan + if-current
TEST(VMStructural, AbStarCheck) {
  std::string src = R"(
alphabet input: [a, b]
scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}
accept
)";
  // Valid a*b* forms
  EXPECT_TRUE(CompileAndRun(src, "").accepted) << "empty";
  EXPECT_TRUE(CompileAndRun(src, "a").accepted) << "a";
  EXPECT_TRUE(CompileAndRun(src, "aaa").accepted) << "aaa";
  EXPECT_TRUE(CompileAndRun(src, "b").accepted) << "b";
  EXPECT_TRUE(CompileAndRun(src, "bbb").accepted) << "bbb";
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted) << "ab";
  EXPECT_TRUE(CompileAndRun(src, "aabb").accepted) << "aabb";
  EXPECT_TRUE(CompileAndRun(src, "aaabbb").accepted) << "aaabbb";

  // Invalid (not a*b*)
  EXPECT_FALSE(CompileAndRun(src, "ba").accepted) << "ba";
  EXPECT_FALSE(CompileAndRun(src, "aba").accepted) << "aba";
  EXPECT_FALSE(CompileAndRun(src, "bab").accepted) << "bab";
  EXPECT_FALSE(CompileAndRun(src, "abba").accepted) << "abba";
  EXPECT_FALSE(CompileAndRun(src, "aabba").accepted) << "aabba";
}

// Exhaustive: verify structural check on all strings up to length 8
TEST(VMStructural, ExhaustiveAbStar) {
  std::string src = R"(
alphabet input: [a, b]
scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}
accept
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  Simulator sim(tm);
  auto inputs = AllStrings({'a', 'b'}, 8);

  for (const auto& input : inputs) {
    bool expected = IsAbStar(input);
    auto result = sim.Run(input);
    EXPECT_EQ(result.accepted, expected)
        << "input=\"" << input << "\": oracle=" << (expected ? "accept" : "reject")
        << ", TM=" << (result.accepted ? "accept" : "reject")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
  }
}

// ============================================================
// TIER 6: Structural check + count
// ============================================================

// a*b* check composed with count(a)==count(b) -> a^n b^n
TEST(VMStructComposed, AnBnSpot) {
  std::string src = R"(
alphabet input: [a, b]
scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}
n = count(a)
return count(b) == n
)";
  EXPECT_TRUE(CompileAndRun(src, "").accepted) << "empty: a^0 b^0";
  EXPECT_TRUE(CompileAndRun(src, "ab").accepted) << "ab: a^1 b^1";
  EXPECT_TRUE(CompileAndRun(src, "aabb").accepted) << "aabb: a^2 b^2";
  EXPECT_TRUE(CompileAndRun(src, "aaabbb").accepted) << "aaabbb: a^3 b^3";

  EXPECT_FALSE(CompileAndRun(src, "a").accepted) << "a: reject";
  EXPECT_FALSE(CompileAndRun(src, "b").accepted) << "b: reject";
  EXPECT_FALSE(CompileAndRun(src, "aab").accepted) << "aab: reject";
  EXPECT_FALSE(CompileAndRun(src, "abb").accepted) << "abb: reject";
  EXPECT_FALSE(CompileAndRun(src, "ba").accepted) << "ba: structural reject";
  EXPECT_FALSE(CompileAndRun(src, "aba").accepted) << "aba: structural reject";
}

// Exhaustive: a^n b^n on all strings up to length 8
TEST(VMStructComposed, ExhaustiveAnBn) {
  std::string src = R"(
alphabet input: [a, b]
scan right for [b, _]
if b {
  scan right for [a, _]
  if a { reject }
}
n = count(a)
return count(b) == n
)";

  Program prog = ParseHL(src);
  TM tm = CompileProgram(prog);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  Simulator sim(tm);
  auto inputs = AllStrings({'a', 'b'}, 8);

  for (const auto& input : inputs) {
    bool expected = IsAnBn(input);
    auto result = sim.Run(input);
    EXPECT_EQ(result.accepted, expected)
        << "input=\"" << input << "\": oracle=" << (expected ? "accept" : "reject")
        << ", TM=" << (result.accepted ? "accept" : "reject")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
  }
}

// ============================================================
// TIER 7: Full T(n) with structural check
// ============================================================

// Spot checks for T(n) with structural check
TEST(VMStructComposed, TriangularSpot) {
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
  // T(0)=0, T(1)=1, T(2)=3, T(3)=6
  EXPECT_TRUE(CompileAndRun(src, "", 10000000).accepted) << "T(0)=0";
  EXPECT_TRUE(CompileAndRun(src, "ab", 10000000).accepted) << "T(1)=1";
  EXPECT_TRUE(CompileAndRun(src, "aabbb", 10000000).accepted) << "T(2)=3";
  EXPECT_TRUE(CompileAndRun(src, "aaabbbbbb", 10000000).accepted) << "T(3)=6";

  EXPECT_FALSE(CompileAndRun(src, "a", 10000000).accepted) << "a: T(1)=1!=0";
  EXPECT_FALSE(CompileAndRun(src, "b", 10000000).accepted) << "b: T(0)=0!=1";
  EXPECT_FALSE(CompileAndRun(src, "aabb", 10000000).accepted) << "aabb: T(2)=3!=2";
  EXPECT_FALSE(CompileAndRun(src, "ba", 10000000).accepted) << "ba: structural reject";
  EXPECT_FALSE(CompileAndRun(src, "aba", 10000000).accepted) << "aba: structural reject";
}

// Exhaustive T(n) with structural check on all strings up to length 8
TEST(VMStructComposed, ExhaustiveTriangular) {
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

  Simulator sim(tm, 10000000);
  auto inputs = AllStrings({'a', 'b'}, 8);

  int tested = 0;
  for (const auto& input : inputs) {
    bool expected = IsTriangularStrict(input);
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
