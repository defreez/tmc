#include "tmc/ir.hpp"

namespace tmc {

void TM::AddTransition(const State& from, Symbol read, Symbol write, Dir dir, const State& to) {
  states.insert(from);
  states.insert(to);
  tape_alphabet.insert(read);
  tape_alphabet.insert(write);
  delta[from][read] = {read, write, dir, to};
}

void TM::Finalize() {
  // Ensure blank is in tape alphabet
  tape_alphabet.insert(kBlank);

  // Ensure input alphabet is subset of tape alphabet
  for (Symbol s : input_alphabet) {
    tape_alphabet.insert(s);
  }

  // Ensure all states are registered
  states.insert(start);
  states.insert(accept);
  states.insert(reject);
}

bool TM::Validate(std::string* error) const {
  // Check start state exists
  if (states.find(start) == states.end()) {
    if (error) *error = "Start state not in states set";
    return false;
  }

  // Check accept state exists
  if (states.find(accept) == states.end()) {
    if (error) *error = "Accept state not in states set";
    return false;
  }

  // Check reject state exists
  if (states.find(reject) == states.end()) {
    if (error) *error = "Reject state not in states set";
    return false;
  }

  // Check all transitions reference valid states and symbols
  for (const auto& [state, trans_map] : delta) {
    if (states.find(state) == states.end()) {
      if (error) *error = "Delta references unknown state: " + state;
      return false;
    }
    for (const auto& [sym, trans] : trans_map) {
      if (tape_alphabet.find(sym) == tape_alphabet.end() && sym != kWildcard) {
        if (error) *error = "Delta references unknown symbol: " + std::string(1, sym);
        return false;
      }
      if (states.find(trans.next) == states.end()) {
        if (error) *error = "Transition targets unknown state: " + trans.next;
        return false;
      }
    }
  }

  return true;
}

}  // namespace tmc
