#pragma once

#include "tmc/ir.hpp"
#include <sstream>

namespace tmc {

// Convert a TM to YAML format for Doty's simulator
std::string ToYAML(const TM& tm);

// Compile IR program to TM
TM CompileIR(const IRProgram& program);

// State name generator for codegen
class StateGen {
public:
  State Next(const std::string& prefix = "q");
  void Reset();
private:
  int counter_ = 0;
};

// IR to TM compiler context
class Compiler {
public:
  Compiler();

  // Compile a full program
  TM Compile(const IRProgram& program);

private:
  // Compile a single IR node, returning the entry and exit states
  struct CompileResult {
    State entry;
    State exit;
  };

  CompileResult CompileNode(const IRNodePtr& node);
  CompileResult CompileScanUntil(const ScanUntil& scan);
  CompileResult CompileWriteSymbol(const WriteSymbol& write);
  CompileResult CompileMove(const Move& move);
  CompileResult CompileIfSymbol(const IfSymbol& if_sym);
  CompileResult CompileWhileSymbol(const WhileSymbol& while_sym);
  CompileResult CompileMark(const Mark& mark);
  CompileResult CompileBlock(const std::vector<IRNodePtr>& body);

  TM tm_;
  StateGen gen_;
  std::map<std::string, State> labels_;  // label -> entry state
};

}  // namespace tmc
