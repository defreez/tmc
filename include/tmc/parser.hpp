#pragma once

#include "tmc/ir.hpp"
#include <string>

namespace tmc {

// Parse low-level IR TMC source
IRProgram Parse(const std::string& source);

// Parse high-level DSL source
Program ParseHL(const std::string& source);

// Parse errors
struct ParseError {
  int line;
  int column;
  std::string message;
};

}  // namespace tmc
