#pragma once

#include "tmc/ir.hpp"
#include <string>
#include <vector>

namespace tmc {

// Result of running a TM
struct RunResult {
  bool accepted;
  int steps;
  std::string final_tape;  // tape contents at end
  bool hit_limit;
};

// Configuration of a TM at a point in time
struct Config {
  std::vector<Symbol> tape;
  int head;
  State state;
};

// Simulate a TM on an input
class Simulator {
public:
  explicit Simulator(const TM& tm, int max_steps = 1000000);

  // Run on input string
  RunResult Run(const std::string& input);

  // Step-by-step execution
  void Reset(const std::string& input);
  bool Step();  // returns false if halted
  bool Halted() const;
  bool Accepted() const;
  int Steps() const;
  Config CurrentConfig() const;

private:
  const TM& tm_;
  int max_steps_;

  std::vector<Symbol> tape_;
  int head_;
  State state_;
  int steps_;
  bool halted_;
};

}  // namespace tmc
