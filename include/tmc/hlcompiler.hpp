#pragma once

#include "tmc/ir.hpp"
#include <unordered_map>

namespace tmc {

// Tape layout:
// [input region]#[var1]#[var2]#...#[varn]#_
//
// Variables stored as unary: value 3 = "111"
// Markers: '#' = region separator
//          Uppercase = marked/processed version

// Compile high-level Program to TM
class HLCompiler {
public:
  HLCompiler();

  TM Compile(const Program& program);

private:
  // State generation
  State NewState(const std::string& hint = "q");
  int state_counter_ = 0;

  // Variable tracking
  struct VarInfo {
    int index;          // which region (0 = first var after input)
    Symbol one_symbol;  // symbol for "1" in this var's unary representation
    Symbol mark_symbol; // marked version
  };
  std::unordered_map<std::string, VarInfo> vars_;
  int next_var_index_ = 0;

  // Register a variable, returns its info
  VarInfo& DeclareVar(const std::string& name);
  VarInfo& GetVar(const std::string& name);

  // The TM being built
  TM tm_;

  // Compilation phases
  void SetupAlphabet(const Program& program);
  State CompileStmt(const StmtPtr& stmt, State entry);
  State CompileStmts(const std::vector<StmtPtr>& stmts, State entry);

  // Statement compilation
  State CompileLet(const LetStmt& stmt, State entry);
  State CompileAssign(const AssignStmt& stmt, State entry);
  State CompileFor(const ForStmt& stmt, State entry);
  State CompileIf(const IfStmt& stmt, State entry);
  State CompileReturn(const ReturnStmt& stmt, State entry);

  // Imperative statement compilation
  State CompileScan(const ScanStmt& stmt, State entry);
  State CompileWrite(const WriteStmt& stmt, State entry);
  State CompileMove(const MoveStmt& stmt, State entry);
  State CompileLoop(const LoopStmt& stmt, State entry);
  State CompileIfCurrent(const IfCurrentStmt& stmt, State entry);
  State CompileMatch(const MatchStmt& stmt, State entry);

  // Expression compilation - evaluates expr, stores result in dest_var
  // Returns the exit state
  State CompileExpr(const ExprPtr& expr, const std::string& dest_var, State entry);
  State CompileCount(const Count& expr, const std::string& dest_var, State entry);
  State CompileBinExpr(const BinExpr& expr, const std::string& dest_var, State entry);

  // Primitive tape operations
  State EmitScanTo(State entry, Symbol target, Dir dir);
  State EmitScanToRegion(State entry, int region_index);
  State EmitScanToInputStart(State entry);
  State EmitWriteAndMove(State entry, Symbol write, Dir dir);
  State EmitCopyRegion(State entry, int src_region, int dest_region);
  State EmitCompareRegions(State entry, int region_a, int region_b,
                           State if_equal, State if_not_equal);
  State EmitIncrementRegion(State entry, int region);
  State EmitCompareRegionToRegion(State entry, int region_a, int region_b,
                                  State if_le, State if_gt);
};

// Convenience function
TM CompileProgram(const Program& program);

}  // namespace tmc
