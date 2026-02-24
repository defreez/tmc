#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/codegen.hpp"
#include "tmc/simulator.hpp"

namespace tmc {
namespace {

// Calculate triangular number T(n) = n(n+1)/2
int T(int n) {
  return n * (n + 1) / 2;
}

// Generate valid string a^n b^T(n)
std::string ValidTriangular(int n) {
  return std::string(n, 'a') + std::string(T(n), 'b');
}

// Build a TM that decides { a^n b^m | m = T(n) }
// This is a naive O(n^3) implementation
TM MakeTriangularNaive() {
  TM tm;
  tm.start = "start";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};
  tm.tape_alphabet.insert('A');  // marked a
  tm.tape_alphabet.insert('B');  // marked b
  tm.tape_alphabet.insert('X');  // counter marker

  // Algorithm:
  // For the i-th unmarked 'a' (1-indexed), mark exactly i 'b's
  // T(n) = 1 + 2 + ... + n, so we need to mark 1 b for 1st a, 2 for 2nd, etc.

  // start: find first unmarked 'a'
  tm.AddTransition("start", 'A', 'A', Dir::R, "start");
  tm.AddTransition("start", 'a', 'A', Dir::R, "count");  // mark it, need to mark 1 b
  tm.AddTransition("start", 'B', 'B', Dir::R, "verify");  // all a's done, verify b's
  tm.AddTransition("start", 'b', 'b', Dir::S, "qR");  // b before finishing a's
  tm.AddTransition("start", kBlank, kBlank, Dir::S, "qA");  // empty = accept

  // count: we're at position after the a we just marked
  // We need to go back to start of marked a's, count how many A's there are,
  // and mark that many b's

  // Actually, let's do a simpler approach:
  // Use the tape to track state. For each A (marked a), mark one more b.
  // So we scan A's and for each A, mark one b.

  // Simpler algorithm:
  // Phase 1: mark first a as A
  // Phase 2: scan right, for each A we pass, remember we need one more b
  //          When we hit an unmarked b, mark it as B, go back to count again
  // Phase 3: After marking all b's for current a, go to next unmarked a

  // Even simpler: for each unmarked a, scan all previous A's and mark one b for each
  // This is O(n^3): n a's, each a scans n A's, each scan is O(n)

  // Let's implement:
  // q_scan_a: scan past A's, counting them
  // Actually for TM we can't "count" in finite state, we have to iterate

  // Simplest correct approach:
  // For i = 1 to n:
  //   For j = 1 to i:
  //     mark one b
  //
  // Implemented as:
  // - Find first unmarked a, mark as A
  // - For each A on tape (including just-marked), mark one b
  // - Repeat until no more a's
  // - Verify no more b's

  // reset to start
  tm.delta.clear();
  tm.states.clear();

  tm.start = "find_a";
  tm.accept = "qA";
  tm.reject = "qR";

  // find_a: skip A's, find unmarked a
  tm.AddTransition("find_a", 'A', 'A', Dir::R, "find_a");
  tm.AddTransition("find_a", 'a', 'A', Dir::L, "go_start");  // mark a, go to start
  tm.AddTransition("find_a", 'B', 'B', Dir::R, "verify");  // no more a's
  tm.AddTransition("find_a", 'b', 'b', Dir::S, "qR");  // unmatched b before done
  tm.AddTransition("find_a", kBlank, kBlank, Dir::S, "qA");  // empty string

  // go_start: go to tape start
  tm.AddTransition("go_start", 'A', 'A', Dir::L, "go_start");
  tm.AddTransition("go_start", 'B', 'B', Dir::L, "go_start");
  tm.AddTransition("go_start", 'a', 'a', Dir::L, "go_start");
  tm.AddTransition("go_start", 'b', 'b', Dir::L, "go_start");
  tm.AddTransition("go_start", kBlank, kBlank, Dir::R, "mark_b_per_A");

  // mark_b_per_A: for each A, mark one b
  // We scan right. For each A, we go find an unmarked b and mark it.
  tm.AddTransition("mark_b_per_A", 'A', 'A', Dir::R, "find_b");  // found A, go mark a b
  tm.AddTransition("mark_b_per_A", 'B', 'B', Dir::R, "mark_b_per_A");  // skip marked b's
  tm.AddTransition("mark_b_per_A", 'a', 'a', Dir::R, "mark_b_per_A");  // skip unmarked a's
  tm.AddTransition("mark_b_per_A", 'b', 'b', Dir::R, "mark_b_per_A");  // skip - but shouldn't happen
  tm.AddTransition("mark_b_per_A", kBlank, kBlank, Dir::L, "back_to_find_a");  // done with this round

  // find_b: scan right past A's, B's, a's to find an unmarked b
  tm.AddTransition("find_b", 'A', 'A', Dir::R, "find_b");
  tm.AddTransition("find_b", 'B', 'B', Dir::R, "find_b");
  tm.AddTransition("find_b", 'a', 'a', Dir::R, "find_b");
  tm.AddTransition("find_b", 'b', 'B', Dir::L, "back_to_A");  // mark b, go back
  tm.AddTransition("find_b", kBlank, kBlank, Dir::S, "qR");  // no b to mark = reject

  // back_to_A: go back left to the A we were at, then continue
  // We need to find the A we were processing. We marked it, so it's still A.
  // But there might be multiple A's. We need to track which one.
  // Hmm, this is getting complex. Let me use a different marker.

  // Actually, let's mark the current A as 'X' while processing, then change back.
  // Redesign:

  tm.delta.clear();
  tm.tape_alphabet.insert('X');  // temp marker for "current A being processed"

  // find_a: skip A's, find unmarked a
  tm.AddTransition("find_a", 'A', 'A', Dir::R, "find_a");
  tm.AddTransition("find_a", 'a', 'A', Dir::L, "rewind");  // mark a
  tm.AddTransition("find_a", 'B', 'B', Dir::R, "verify");  // no more a's
  tm.AddTransition("find_a", 'b', 'b', Dir::S, "qR");
  tm.AddTransition("find_a", kBlank, kBlank, Dir::S, "qA");

  // rewind: go to start
  tm.AddTransition("rewind", 'A', 'A', Dir::L, "rewind");
  tm.AddTransition("rewind", kBlank, kBlank, Dir::R, "mark_first_A");

  // mark_first_A: mark first A as X (current)
  tm.AddTransition("mark_first_A", 'A', 'X', Dir::R, "find_unmarked_b");

  // find_unmarked_b: go right to find unmarked b
  tm.AddTransition("find_unmarked_b", 'A', 'A', Dir::R, "find_unmarked_b");
  tm.AddTransition("find_unmarked_b", 'B', 'B', Dir::R, "find_unmarked_b");
  tm.AddTransition("find_unmarked_b", 'a', 'a', Dir::R, "find_unmarked_b");
  tm.AddTransition("find_unmarked_b", 'b', 'B', Dir::L, "back_to_X");  // mark b
  tm.AddTransition("find_unmarked_b", kBlank, kBlank, Dir::S, "qR");  // not enough b's

  // back_to_X: go back to X
  tm.AddTransition("back_to_X", 'A', 'A', Dir::L, "back_to_X");
  tm.AddTransition("back_to_X", 'B', 'B', Dir::L, "back_to_X");
  tm.AddTransition("back_to_X", 'a', 'a', Dir::L, "back_to_X");
  tm.AddTransition("back_to_X", 'X', 'A', Dir::R, "next_A");  // restore X to A, move right

  // next_A: find next A to process
  tm.AddTransition("next_A", 'A', 'X', Dir::R, "find_unmarked_b");  // process this A
  tm.AddTransition("next_A", 'B', 'B', Dir::R, "next_A");  // skip B's
  tm.AddTransition("next_A", 'a', 'a', Dir::L, "rewind_for_next");  // no more A's in this range, but more a's exist
  tm.AddTransition("next_A", 'b', 'b', Dir::L, "rewind_for_next");  // hit b's, done with this a
  tm.AddTransition("next_A", kBlank, kBlank, Dir::L, "rewind_for_next");  // done with this a

  // rewind_for_next: go back to start, then find next a
  tm.AddTransition("rewind_for_next", 'A', 'A', Dir::L, "rewind_for_next");
  tm.AddTransition("rewind_for_next", 'B', 'B', Dir::L, "rewind_for_next");
  tm.AddTransition("rewind_for_next", 'a', 'a', Dir::L, "rewind_for_next");
  tm.AddTransition("rewind_for_next", 'b', 'b', Dir::L, "rewind_for_next");
  tm.AddTransition("rewind_for_next", kBlank, kBlank, Dir::R, "find_a");

  // verify: check no unmarked b's remain
  tm.AddTransition("verify", 'B', 'B', Dir::R, "verify");
  tm.AddTransition("verify", 'b', 'b', Dir::S, "qR");  // unmatched b
  tm.AddTransition("verify", kBlank, kBlank, Dir::S, "qA");

  tm.Finalize();
  return tm;
}

TEST(TriangularTest, ValidStrings) {
  TM tm = MakeTriangularNaive();
  Simulator sim(tm);

  // T(0) = 0: "" should accept
  EXPECT_TRUE(sim.Run("").accepted) << "Empty string should accept";

  // T(1) = 1: "ab" should accept
  EXPECT_TRUE(sim.Run("ab").accepted) << "ab should accept";

  // T(2) = 3: "aabbb" should accept
  EXPECT_TRUE(sim.Run("aabbb").accepted) << "aabbb should accept";

  // T(3) = 6: "aaabbbbbb" should accept
  EXPECT_TRUE(sim.Run("aaabbbbbb").accepted) << "aaabbbbbb should accept";

  // T(4) = 10: "aaaabbbbbbbbbb" should accept
  EXPECT_TRUE(sim.Run("aaaabbbbbbbbbb").accepted) << "aaaabbbbbbbbbb should accept";
}

TEST(TriangularTest, InvalidStrings) {
  TM tm = MakeTriangularNaive();
  Simulator sim(tm);

  EXPECT_FALSE(sim.Run("a").accepted) << "a should reject";
  EXPECT_FALSE(sim.Run("b").accepted) << "b should reject";
  EXPECT_FALSE(sim.Run("aabb").accepted) << "aabb should reject (need 3 b's)";
  EXPECT_FALSE(sim.Run("aabbbb").accepted) << "aabbbb should reject (need 3 b's)";
  EXPECT_FALSE(sim.Run("aaabbbbb").accepted) << "aaabbbbb should reject (need 6 b's)";
  EXPECT_FALSE(sim.Run("aaabbbbbbb").accepted) << "aaabbbbbbb should reject (need 6 b's)";
  EXPECT_FALSE(sim.Run("ba").accepted) << "ba should reject (wrong order)";
  EXPECT_FALSE(sim.Run("abab").accepted) << "abab should reject (not a^n b^m form)";
}

TEST(TriangularTest, StepCounts) {
  TM tm = MakeTriangularNaive();
  Simulator sim(tm);

  std::cout << "Step counts for valid inputs:\n";
  for (int n = 0; n <= 5; ++n) {
    std::string input = ValidTriangular(n);
    auto result = sim.Run(input);
    std::cout << "  n=" << n << " |input|=" << input.size()
              << " steps=" << result.steps << "\n";
    EXPECT_TRUE(result.accepted);
  }
}

// Helper to verify our TM matches expected behavior
class TriangularOracle {
public:
  bool operator()(const std::string& s) const {
    int n = 0, m = 0;
    bool in_b = false;
    for (char c : s) {
      if (c == 'a') {
        if (in_b) return false;  // a after b
        ++n;
      } else if (c == 'b') {
        in_b = true;
        ++m;
      } else {
        return false;  // invalid char
      }
    }
    return m == T(n);
  }
};

TEST(TriangularTest, OracleVerification) {
  TriangularOracle oracle;

  EXPECT_TRUE(oracle(""));
  EXPECT_TRUE(oracle("ab"));
  EXPECT_TRUE(oracle("aabbb"));
  EXPECT_TRUE(oracle("aaabbbbbb"));

  EXPECT_FALSE(oracle("a"));
  EXPECT_FALSE(oracle("aabb"));
  EXPECT_FALSE(oracle("ba"));
}

TEST(TriangularTest, ExhaustiveSmall) {
  TM tm = MakeTriangularNaive();
  Simulator sim(tm);
  TriangularOracle oracle;

  // Test all strings of length <= 10
  std::vector<std::string> inputs = {""};
  std::queue<std::string> queue;
  queue.push("");

  while (!queue.empty()) {
    std::string s = queue.front();
    queue.pop();

    if (s.size() < 10) {
      queue.push(s + "a");
      queue.push(s + "b");
      inputs.push_back(s + "a");
      inputs.push_back(s + "b");
    }
  }

  int passed = 0, failed = 0;
  for (const std::string& s : inputs) {
    bool expected = oracle(s);
    auto result = sim.Run(s);

    if (result.accepted == expected) {
      ++passed;
    } else {
      ++failed;
      std::cout << "MISMATCH: \"" << s << "\" expected "
                << (expected ? "ACCEPT" : "REJECT")
                << " got " << (result.accepted ? "ACCEPT" : "REJECT") << "\n";
    }
  }

  std::cout << "Exhaustive test: " << passed << " passed, " << failed << " failed\n";
  EXPECT_EQ(failed, 0);
}

}  // namespace
}  // namespace tmc
