#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/simulator.hpp"
#include <fstream>
#include <sstream>

namespace tmc {
namespace {

// Read a .tmc file from the examples directory
std::string ReadFile(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) throw std::runtime_error("Cannot open: " + path);
  std::stringstream buf;
  buf << ifs.rdbuf();
  return buf.str();
}

// Compile a .tmc source string to a TM
TM CompileSource(const std::string& source) {
  Program prog = ParseHL(source);
  return CompileProgram(prog);
}

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

// ---- Oracles ----

// a^n b^n: equal count of a's and b's in a*b* form
bool IsAnBn(const std::string& s) {
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
  return n == m;
}

// Triangular: {a^n b^m | m = n*(n+1)/2}
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
  return m == n * (n + 1) / 2;
}

// Starts and ends with 'a': non-empty, first char = 'a', last char = 'a'
bool StartsAndEndsWithA(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c != 'a' && c != 'b') return false;
  }
  return s.front() == 'a' && s.back() == 'a';
}

// ---- Exhaustive verification helper ----

void VerifyExhaustive(const TM& tm,
                      const std::set<Symbol>& alphabet,
                      int max_len,
                      const std::function<bool(const std::string&)>& oracle,
                      int step_limit = 10000000) {
  Simulator sim(tm, step_limit);
  auto inputs = AllStrings(alphabet, max_len);

  for (const auto& input : inputs) {
    bool expected = oracle(input);
    auto result = sim.Run(input);
    EXPECT_EQ(result.accepted, expected)
        << "input=\"" << input << "\" (len " << input.size() << "): "
        << "oracle=" << (expected ? "accept" : "reject")
        << ", TM=" << (result.accepted ? "accept" : "reject")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
  }
}

// ---- Tests ----

TEST(ExampleTest, AnBn) {
  std::string src = ReadFile(EXAMPLES_DIR "/anbn.tmc");
  TM tm = CompileSource(src);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  VerifyExhaustive(tm, {'a', 'b'}, 10, IsAnBn);
}

TEST(ExampleTest, Triangular) {
  std::string src = ReadFile(EXAMPLES_DIR "/triangular.tmc");
  TM tm = CompileSource(src);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  VerifyExhaustive(tm, {'a', 'b'}, 8, IsTriangular);
}

TEST(ExampleTest, StartsWithA) {
  std::string src = ReadFile(EXAMPLES_DIR "/starts-with-a.tmc");
  TM tm = CompileSource(src);

  std::string error;
  ASSERT_TRUE(tm.Validate(&error)) << error;

  VerifyExhaustive(tm, {'a', 'b'}, 10, StartsAndEndsWithA);
}

}  // namespace
}  // namespace tmc
