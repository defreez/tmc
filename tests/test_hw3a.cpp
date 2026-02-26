#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/codegen.hpp"
#include "tmc/simulator.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace tmc {
namespace {

// Read file contents into string
std::string ReadFile(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) throw std::runtime_error("Cannot open: " + path);
  std::stringstream buf;
  buf << ifs.rdbuf();
  return buf.str();
}

// Parse the HW3A test suite format:
//   - Lines starting with # are comments
//   - "(empty)" means the empty string
//   - All other lines are literal test inputs
std::vector<std::string> ParseTestSuite(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) throw std::runtime_error("Cannot open test suite: " + path);

  std::vector<std::string> inputs;
  std::string line;
  while (std::getline(ifs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line == "(empty)") {
      inputs.push_back("");
    } else {
      inputs.push_back(line);
    }
  }
  return inputs;
}

// Oracle: {a^n b^m | m = n*(n+1)/2}
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

// Count transitions in the TM
int CountTransitions(const TM& tm) {
  int count = 0;
  for (const auto& [state, tmap] : tm.delta) {
    count += static_cast<int>(tmap.size());
  }
  return count;
}

class HW3ATest : public ::testing::Test {
protected:
  void SetUp() override {
    std::string yaml = ReadFile(std::string(EXAMPLES_DIR) + "/triangular.tm");
    tm_ = FromYAML(yaml);
    inputs_ = ParseTestSuite(std::string(FIXTURES_DIR) + "/hw3a_public.txt");
  }

  TM tm_;
  std::vector<std::string> inputs_;
};

TEST_F(HW3ATest, TMMetrics) {
  std::cout << "\n=== TM Metrics ===\n";
  std::cout << "States:           " << tm_.states.size() << "\n";
  std::cout << "Input alphabet:   {";
  bool first = true;
  for (Symbol s : tm_.input_alphabet) {
    if (!first) std::cout << ", ";
    std::cout << s;
    first = false;
  }
  std::cout << "}\n";
  std::cout << "Tape alphabet:    " << tm_.tape_alphabet.size() << " symbols\n";
  std::cout << "Transitions:      " << CountTransitions(tm_) << "\n";

  std::string error;
  ASSERT_TRUE(tm_.Validate(&error)) << "TM validation failed: " << error;
}

TEST_F(HW3ATest, AllTestCases) {
  ASSERT_FALSE(inputs_.empty()) << "No test inputs loaded";
  std::cout << "\n=== HW3A Public Test Suite (" << inputs_.size() << " cases) ===\n";

  Simulator sim(tm_, 10000000);

  int passed = 0, failed = 0;
  int total_steps = 0;
  int max_steps = 0;
  std::string max_steps_input;
  std::vector<int> step_counts;

  for (size_t i = 0; i < inputs_.size(); ++i) {
    const std::string& input = inputs_[i];
    bool expected = IsTriangular(input);
    auto result = sim.Run(input);

    int n = 0;
    for (char c : input) {
      if (c == 'a') ++n;
      else break;
    }

    step_counts.push_back(result.steps);
    total_steps += result.steps;
    if (result.steps > max_steps) {
      max_steps = result.steps;
      max_steps_input = input.size() <= 20 ? input : "(len " + std::to_string(input.size()) + ")";
    }

    bool correct = (result.accepted == expected) && !result.hit_limit;

    std::cout << "  [" << (i + 1) << "/" << inputs_.size() << "] "
              << "n=" << n << " |w|=" << input.size()
              << " expect=" << (expected ? "ACC" : "REJ")
              << " got=" << (result.accepted ? "ACC" : "REJ")
              << " steps=" << result.steps;
    if (result.hit_limit) std::cout << " HIT_LIMIT";
    std::cout << (correct ? "" : " FAIL") << "\n";

    if (correct) {
      ++passed;
    } else {
      ++failed;
    }

    EXPECT_EQ(result.accepted, expected)
        << "Test case " << (i + 1) << ": input=\""
        << (input.size() <= 40 ? input : input.substr(0, 40) + "...")
        << "\" (n=" << n << ", |w|=" << input.size() << "): "
        << "expected " << (expected ? "ACCEPT" : "REJECT")
        << ", got " << (result.accepted ? "ACCEPT" : "REJECT")
        << (result.hit_limit ? " (HIT STEP LIMIT)" : "");
  }

  double avg_steps = inputs_.empty() ? 0.0 : static_cast<double>(total_steps) / inputs_.size();

  std::cout << "\n=== Summary ===\n";
  std::cout << "Passed:           " << passed << "/" << inputs_.size() << "\n";
  std::cout << "Failed:           " << failed << "\n";
  std::cout << "Total steps:      " << total_steps << "\n";
  std::cout << "Average steps:    " << std::fixed << std::setprecision(1) << avg_steps << "\n";
  std::cout << "Max steps:        " << max_steps << " on " << max_steps_input << "\n";

  EXPECT_EQ(failed, 0) << failed << " test cases failed";
}

}  // namespace
}  // namespace tmc
