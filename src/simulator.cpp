#include "tmc/simulator.hpp"
#include <algorithm>

namespace tmc {

Simulator::Simulator(const TM& tm, int max_steps)
    : tm_(tm), max_steps_(max_steps), head_(0), steps_(0), halted_(false) {}

RunResult Simulator::Run(const std::string& input) {
  Reset(input);

  while (!Halted() && steps_ < max_steps_) {
    Step();
  }

  RunResult result;
  result.accepted = Accepted();
  result.steps = steps_;
  result.hit_limit = (steps_ >= max_steps_ && !halted_);

  // Extract final tape contents
  int left = 0, right = static_cast<int>(tape_.size()) - 1;
  while (left < static_cast<int>(tape_.size()) && tape_[left] == kBlank) ++left;
  while (right >= 0 && tape_[right] == kBlank) --right;
  if (left <= right) {
    result.final_tape = std::string(tape_.begin() + left, tape_.begin() + right + 1);
  }

  return result;
}

void Simulator::Reset(const std::string& input) {
  tape_.clear();
  tape_.reserve(input.size() + 100);

  // Initialize tape with input
  for (char c : input) {
    tape_.push_back(static_cast<Symbol>(c));
  }
  if (tape_.empty()) {
    tape_.push_back(kBlank);
  }

  head_ = 0;
  state_ = tm_.start;
  steps_ = 0;
  halted_ = false;
}

bool Simulator::Step() {
  if (halted_) return false;

  // Check for halt states
  if (state_ == tm_.accept || state_ == tm_.reject) {
    halted_ = true;
    return false;
  }

  // Get current symbol
  Symbol current = (head_ >= 0 && head_ < static_cast<int>(tape_.size()))
                       ? tape_[head_]
                       : kBlank;

  // Find transition
  auto state_it = tm_.delta.find(state_);
  if (state_it == tm_.delta.end()) {
    // No transitions from this state - implicit reject
    state_ = tm_.reject;
    halted_ = true;
    return false;
  }

  const auto& trans_map = state_it->second;

  // Try exact match first
  auto trans_it = trans_map.find(current);
  if (trans_it == trans_map.end()) {
    // Try wildcard
    trans_it = trans_map.find(kWildcard);
  }

  if (trans_it == trans_map.end()) {
    // No transition - implicit reject
    state_ = tm_.reject;
    halted_ = true;
    return false;
  }

  const Transition& trans = trans_it->second;

  // Left-bounded tape: clamp head at 0 (Sipser model)
  if (head_ < 0) {
    head_ = 0;
  }
  // Extend tape right if needed
  while (head_ >= static_cast<int>(tape_.size())) {
    tape_.push_back(kBlank);
  }

  // Execute transition
  Symbol write_sym = trans.write;
  if (write_sym == kWildcard) {
    write_sym = current;  // Wildcard in write means keep current
  }
  tape_[head_] = write_sym;

  switch (trans.dir) {
    case Dir::L:
      --head_;
      break;
    case Dir::R:
      ++head_;
      break;
    case Dir::S:
      break;
  }

  state_ = trans.next;
  ++steps_;

  // Check for halt after transition
  if (state_ == tm_.accept || state_ == tm_.reject) {
    halted_ = true;
  }

  return !halted_;
}

bool Simulator::Halted() const {
  return halted_;
}

bool Simulator::Accepted() const {
  return halted_ && state_ == tm_.accept;
}

int Simulator::Steps() const {
  return steps_;
}

Config Simulator::CurrentConfig() const {
  return {tape_, head_, state_};
}

}  // namespace tmc
