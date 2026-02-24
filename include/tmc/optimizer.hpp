#pragma once

#include "tmc/ir.hpp"
#include <functional>

namespace tmc {

// Optimization configuration
struct OptConfig {
  // Maximum number of states to generate (0 = unlimited)
  int max_states = 0;

  // Maximum tape alphabet size (0 = unlimited)
  int max_tape_symbols = 0;

  // Precomputation settings
  bool enable_precompute = true;
  int precompute_max_input_len = 10;  // precompute results for inputs up to this length

  // State merging
  bool merge_equivalent_states = true;

  // Dead state elimination
  bool eliminate_dead_states = true;

  // Scan fusion (combine consecutive scans)
  bool fuse_scans = true;

  // Direction optimization (minimize reversals)
  bool optimize_directions = true;
};

// Optimization pass on TM
void Optimize(TM& tm, const OptConfig& config = {});

// Optimization pass on IR (before compilation)
void OptimizeIR(IRProgram& program, const OptConfig& config = {});

// Add precomputed results for small inputs
// For each input string up to max_len, add direct transitions
void AddPrecomputed(TM& tm, int max_len,
                    const std::function<bool(const std::string&)>& oracle);

// Merge equivalent states
int MergeEquivalentStates(TM& tm);

// Remove unreachable states
int EliminateDeadStates(TM& tm);

// Fuse consecutive unidirectional scans
int FuseScans(TM& tm);

}  // namespace tmc
