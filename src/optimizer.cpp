#include "tmc/optimizer.hpp"
#include <queue>
#include <unordered_set>
#include <algorithm>
#include <functional>

namespace tmc {

void Optimize(TM& tm, const OptConfig& config) {
  if (config.eliminate_dead_states) {
    EliminateDeadStates(tm);
  }

  if (config.merge_equivalent_states) {
    MergeEquivalentStates(tm);
  }

  // Note: precomputation is done separately with AddPrecomputed
  // since it requires an oracle function

  tm.Finalize();
}

void OptimizeIR(IRProgram& program, const OptConfig& config) {
  // TODO: IR-level optimizations
  // - Fuse consecutive moves in same direction
  // - Remove dead code after accept/reject
  // - Simplify nested if statements
}

void AddPrecomputed(TM& tm, int max_len,
                    const std::function<bool(const std::string&)>& oracle) {
  // Generate all strings up to max_len over input alphabet
  std::vector<std::string> inputs;
  inputs.push_back("");  // empty string

  std::queue<std::string> queue;
  queue.push("");

  while (!queue.empty()) {
    std::string current = queue.front();
    queue.pop();

    if (static_cast<int>(current.size()) < max_len) {
      for (Symbol s : tm.input_alphabet) {
        std::string next = current + static_cast<char>(s);
        inputs.push_back(next);
        queue.push(next);
      }
    }
  }

  // Create fast-path states for each precomputed input
  // For input "abc", we need states for "", "a", "ab", "abc"
  // with transitions that bypass the normal algorithm

  std::map<std::string, State> prefix_states;

  for (const std::string& input : inputs) {
    bool accepted = oracle(input);

    // Create a chain of states for this input prefix
    std::string prefix;
    State prev_state = tm.start;

    for (size_t i = 0; i < input.size(); ++i) {
      std::string new_prefix = prefix + input[i];

      auto it = prefix_states.find(new_prefix);
      if (it == prefix_states.end()) {
        // Create new state for this prefix
        State new_state = "pre_" + new_prefix;
        if (new_prefix.empty()) new_state = "pre_eps";
        tm.states.insert(new_state);
        prefix_states[new_prefix] = new_state;

        // Add transition from previous state
        // Only add if there's no existing precompute transition
        auto& trans_map = tm.delta[prev_state];
        Symbol sym = input[i];
        if (trans_map.find(sym) == trans_map.end()) {
          trans_map[sym] = {sym, sym, Dir::R, new_state};
        }
      }

      prev_state = prefix_states[new_prefix];
      prefix = new_prefix;
    }

    // From the final state, on blank, go to accept/reject
    auto& trans_map = tm.delta[prev_state];
    if (trans_map.find(kBlank) == trans_map.end()) {
      trans_map[kBlank] = {kBlank, kBlank, Dir::S, accepted ? tm.accept : tm.reject};
    }
  }
}

int MergeEquivalentStates(TM& tm) {
  // Simple equivalence: states with identical transition tables
  // More sophisticated: Hopcroft's algorithm for DFA minimization
  // For now, just do simple identical-table merging

  int merged = 0;
  bool changed = true;

  while (changed) {
    changed = false;

    // Find two states with identical transitions
    for (auto it1 = tm.states.begin(); it1 != tm.states.end(); ++it1) {
      if (*it1 == tm.accept || *it1 == tm.reject || *it1 == tm.start) continue;

      auto trans1_it = tm.delta.find(*it1);
      if (trans1_it == tm.delta.end()) continue;

      for (auto it2 = std::next(it1); it2 != tm.states.end(); ++it2) {
        if (*it2 == tm.accept || *it2 == tm.reject || *it2 == tm.start) continue;

        auto trans2_it = tm.delta.find(*it2);
        if (trans2_it == tm.delta.end()) continue;

        // Compare transition tables
        if (trans1_it->second == trans2_it->second) {
          // Merge: replace all references to *it2 with *it1
          for (auto& [state, trans_map] : tm.delta) {
            for (auto& [sym, trans] : trans_map) {
              if (trans.next == *it2) {
                trans.next = *it1;
              }
            }
          }

          // Remove it2's transitions and state
          tm.delta.erase(*it2);
          tm.states.erase(*it2);
          changed = true;
          ++merged;
          break;
        }
      }

      if (changed) break;
    }
  }

  return merged;
}

int EliminateDeadStates(TM& tm) {
  // Find all reachable states from start
  std::set<State> reachable;
  std::queue<State> queue;
  queue.push(tm.start);
  reachable.insert(tm.start);

  while (!queue.empty()) {
    State current = queue.front();
    queue.pop();

    auto it = tm.delta.find(current);
    if (it != tm.delta.end()) {
      for (const auto& [sym, trans] : it->second) {
        if (reachable.find(trans.next) == reachable.end()) {
          reachable.insert(trans.next);
          queue.push(trans.next);
        }
      }
    }
  }

  // Always keep accept and reject reachable
  reachable.insert(tm.accept);
  reachable.insert(tm.reject);

  // Remove unreachable states
  int removed = 0;
  std::vector<State> to_remove;

  for (const State& s : tm.states) {
    if (reachable.find(s) == reachable.end()) {
      to_remove.push_back(s);
    }
  }

  for (const State& s : to_remove) {
    tm.states.erase(s);
    tm.delta.erase(s);
    ++removed;
  }

  return removed;
}

int FuseScans(TM& tm) {
  // TODO: Identify consecutive scan states and merge them
  // This requires analyzing the TM structure to find patterns like:
  // q1: all transitions go R to q2
  // q2: all transitions go R to q3
  // Can be merged into a single state that moves R twice per transition
  // But single-tape TM can only move once per step, so this optimization
  // is about reducing intermediate states, not reducing steps.
  return 0;
}

}  // namespace tmc
