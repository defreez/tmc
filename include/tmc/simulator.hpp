#pragma once

#include "tmc/ir.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace tmc {

// Result of running a TM
struct RunResult {
  bool accepted;
  int64_t steps;
  std::string final_tape;  // tape contents at end
  bool hit_limit;
};

// Configuration of a TM at a point in time
struct Config {
  std::vector<Symbol> tape;
  int head;
  State state;
};

// Pre-expanded transition entry for flat table lookup
struct FlatTransition {
  uint16_t next;   // next state ID
  uint8_t write;   // symbol index to write
  int8_t dir;      // -1, 0, +1
};

// Simulate a TM on an input
class Simulator {
public:
  explicit Simulator(const TM& tm, int64_t max_steps = 1000000);

  // Run on input string
  RunResult Run(const std::string& input);

  // Step-by-step execution
  void Reset(const std::string& input);
  bool Step();  // returns false if halted
  bool Halted() const;
  bool Accepted() const;
  int64_t Steps() const;
  Config CurrentConfig() const;

private:
  void BuildTable(const TM& tm);

  int64_t max_steps_;

  // Flat transition table: table_[state_id * num_symbols_ + symbol_idx]
  int num_states_;
  int num_symbols_;
  uint16_t start_id_;
  uint16_t accept_id_;
  uint16_t reject_id_;
  uint16_t halt_threshold_;  // min(accept_id, reject_id)
  std::vector<FlatTransition> table_;

  // Symbol mapping
  uint8_t char_to_idx_[256];
  std::vector<char> idx_to_char_;
  uint8_t blank_idx_;

  // State mapping (for CurrentConfig/Accepted)
  std::vector<State> id_to_state_;

  // Runtime state
  std::vector<uint8_t> tape_;
  int head_;
  uint16_t state_id_;
  int64_t steps_;
  bool halted_;
};

}  // namespace tmc
