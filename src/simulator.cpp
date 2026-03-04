#include "tmc/simulator.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace tmc {

Simulator::Simulator(const TM& tm, int64_t max_steps)
    : max_steps_(max_steps), head_(0), state_id_(0), steps_(0), halted_(false) {
  BuildTable(tm);
}

void Simulator::BuildTable(const TM& tm) {
  // --- Symbol mapping: char -> dense index ---
  // Collect all symbols from tape alphabet plus blank
  std::set<Symbol> all_symbols = tm.tape_alphabet;
  all_symbols.insert(kBlank);
  // Also include input alphabet (should already be in tape_alphabet, but be safe)
  all_symbols.insert(tm.input_alphabet.begin(), tm.input_alphabet.end());

  num_symbols_ = static_cast<int>(all_symbols.size());
  idx_to_char_.resize(num_symbols_);
  std::memset(char_to_idx_, 0, sizeof(char_to_idx_));

  uint8_t idx = 0;
  for (Symbol s : all_symbols) {
    char_to_idx_[static_cast<unsigned char>(s)] = idx;
    idx_to_char_[idx] = s;
    ++idx;
  }
  blank_idx_ = char_to_idx_[static_cast<unsigned char>(kBlank)];

  // --- State mapping: string -> dense integer ID ---
  // Assign accept and reject as the two highest IDs so that
  // all "running" states have IDs < halt_threshold_.
  std::unordered_map<std::string, uint16_t> state_to_id;

  // First pass: collect non-halt states
  std::vector<State> running_states;
  for (const auto& s : tm.states) {
    if (s != tm.accept && s != tm.reject) {
      running_states.push_back(s);
    }
  }

  id_to_state_.clear();
  id_to_state_.reserve(tm.states.size());
  uint16_t id = 0;
  for (const auto& s : running_states) {
    state_to_id[s] = id;
    id_to_state_.push_back(s);
    ++id;
  }

  // Accept and reject get the highest IDs
  accept_id_ = id++;
  state_to_id[tm.accept] = accept_id_;
  id_to_state_.push_back(tm.accept);

  reject_id_ = id++;
  state_to_id[tm.reject] = reject_id_;
  id_to_state_.push_back(tm.reject);

  num_states_ = id;
  halt_threshold_ = std::min(accept_id_, reject_id_);
  start_id_ = state_to_id.at(tm.start);

  // --- Build flat transition table ---
  table_.resize(num_states_ * num_symbols_);

  // Default: all transitions go to reject
  for (auto& ft : table_) {
    ft.next = reject_id_;
    ft.write = 0;
    ft.dir = 0;
  }

  // Fill from TM delta
  for (const auto& [state_str, trans_map] : tm.delta) {
    auto sit = state_to_id.find(state_str);
    if (sit == state_to_id.end()) continue;
    uint16_t sid = sit->second;

    // Find wildcard transition if any
    const Transition* wildcard = nullptr;
    auto wit = trans_map.find(kWildcard);
    if (wit != trans_map.end()) {
      wildcard = &wit->second;
    }

    // For each symbol in the alphabet, fill the table entry
    for (int si = 0; si < num_symbols_; ++si) {
      Symbol sym = idx_to_char_[si];
      const Transition* t = nullptr;

      // Exact match first
      auto eit = trans_map.find(sym);
      if (eit != trans_map.end()) {
        t = &eit->second;
      } else if (wildcard) {
        t = wildcard;
      }

      if (t) {
        FlatTransition& ft = table_[sid * num_symbols_ + si];

        // Resolve next state
        auto nit = state_to_id.find(t->next);
        ft.next = (nit != state_to_id.end()) ? nit->second : reject_id_;

        // Resolve write symbol (wildcard write means keep current)
        Symbol ws = (t->write == kWildcard) ? sym : t->write;
        ft.write = char_to_idx_[static_cast<unsigned char>(ws)];

        // Direction
        switch (t->dir) {
          case Dir::L: ft.dir = -1; break;
          case Dir::R: ft.dir = 1; break;
          case Dir::S: ft.dir = 0; break;
        }
      }
      // else: default (reject) already set
    }
  }
}

RunResult Simulator::Run(const std::string& input) {
  // Build tape of symbol indices with right padding
  const int pad = 4096;
  int input_len = static_cast<int>(input.size());
  int tape_alloc = std::max(input_len + pad, pad);

  std::vector<uint8_t> tape(tape_alloc, blank_idx_);
  for (int i = 0; i < input_len; ++i) {
    tape[i] = char_to_idx_[static_cast<unsigned char>(input[i])];
  }
  if (input_len == 0) {
    // tape[0] is already blank_idx_
  }

  uint16_t state = start_id_;
  int head = 0;
  int64_t steps = 0;
  const int64_t max = max_steps_;
  const int stride = num_symbols_;
  const FlatTransition* tbl = table_.data();
  const uint16_t halt = halt_threshold_;

  while (state < halt && steps < max) {
    // Extend tape if needed
    if (head >= static_cast<int>(tape.size())) {
      tape.resize(tape.size() * 2, blank_idx_);
    }

    const FlatTransition& t = tbl[state * stride + tape[head]];
    tape[head] = t.write;
    state = t.next;
    head += t.dir;
    if (head < 0) head = 0;  // left-bounded (Sipser)
    ++steps;
  }

  // Build result
  RunResult result;
  result.accepted = (state == accept_id_);
  result.steps = steps;
  result.hit_limit = (steps >= max && state < halt);

  // Extract final tape contents (convert back to chars, trim blanks)
  int left = 0, right = static_cast<int>(tape.size()) - 1;
  while (left < static_cast<int>(tape.size()) && tape[left] == blank_idx_) ++left;
  while (right >= 0 && tape[right] == blank_idx_) --right;
  if (left <= right) {
    result.final_tape.reserve(right - left + 1);
    for (int i = left; i <= right; ++i) {
      result.final_tape.push_back(idx_to_char_[tape[i]]);
    }
  }

  return result;
}

void Simulator::Reset(const std::string& input) {
  tape_.clear();
  tape_.reserve(input.size() + 100);

  for (char c : input) {
    tape_.push_back(char_to_idx_[static_cast<unsigned char>(c)]);
  }
  if (tape_.empty()) {
    tape_.push_back(blank_idx_);
  }

  head_ = 0;
  state_id_ = start_id_;
  steps_ = 0;
  halted_ = false;
}

bool Simulator::Step() {
  if (halted_) return false;

  // Check for halt states
  if (state_id_ >= halt_threshold_) {
    halted_ = true;
    return false;
  }

  // Left-bounded tape: clamp head at 0
  if (head_ < 0) head_ = 0;

  // Extend tape right if needed
  while (head_ >= static_cast<int>(tape_.size())) {
    tape_.push_back(blank_idx_);
  }

  // Flat table lookup
  const FlatTransition& t = table_[state_id_ * num_symbols_ + tape_[head_]];
  tape_[head_] = t.write;
  state_id_ = t.next;
  head_ += t.dir;
  ++steps_;

  // Check for halt after transition
  if (state_id_ >= halt_threshold_) {
    halted_ = true;
  }

  return !halted_;
}

bool Simulator::Halted() const {
  return halted_;
}

bool Simulator::Accepted() const {
  return halted_ && state_id_ == accept_id_;
}

int64_t Simulator::Steps() const {
  return steps_;
}

Config Simulator::CurrentConfig() const {
  Config c;
  c.tape.reserve(tape_.size());
  for (auto idx : tape_) {
    c.tape.push_back(idx_to_char_[idx]);
  }
  c.head = head_;
  c.state = id_to_state_[state_id_];
  return c;
}

}  // namespace tmc
