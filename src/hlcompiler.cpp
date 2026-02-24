#include "tmc/hlcompiler.hpp"
#include <stdexcept>
#include <cassert>

namespace tmc {

// Tape layout (left-bounded, Sipser model):
// >[input]#[var0]#[var1]#...
// > = left-end marker at cell 0, input starts at cell 1
// Variables stored as unary: value 3 = "111"

constexpr Symbol kSep = '#';
constexpr Symbol kOne = '1';
constexpr Symbol kMarked = 'I';
constexpr Symbol kLeftEnd = '>';

HLCompiler::HLCompiler() = default;

State HLCompiler::NewState(const std::string& hint) {
  return hint + std::to_string(state_counter_++);
}

HLCompiler::VarInfo& HLCompiler::DeclareVar(const std::string& name) {
  if (vars_.count(name)) {
    return vars_[name];  // Already declared, return existing
  }
  VarInfo info;
  info.index = next_var_index_++;
  info.one_symbol = kOne;
  info.mark_symbol = kMarked;
  vars_[name] = info;
  return vars_[name];
}

HLCompiler::VarInfo& HLCompiler::GetVar(const std::string& name) {
  auto it = vars_.find(name);
  if (it == vars_.end()) {
    // Auto-declare
    return DeclareVar(name);
  }
  return it->second;
}

void HLCompiler::SetupAlphabet(const Program& program) {
  tm_.input_alphabet = program.input_alphabet;
  tm_.tape_alphabet = program.input_alphabet;
  tm_.tape_alphabet.insert(kBlank);
  tm_.tape_alphabet.insert(kSep);
  tm_.tape_alphabet.insert(kOne);
  tm_.tape_alphabet.insert(kMarked);
  tm_.tape_alphabet.insert(kLeftEnd);

  // Marked versions of input symbols
  for (Symbol s : program.input_alphabet) {
    if (s >= 'a' && s <= 'z') {
      tm_.tape_alphabet.insert(static_cast<Symbol>(s - 'a' + 'A'));
    }
  }

  // Add marker symbols from program
  for (Symbol s : program.markers) {
    tm_.tape_alphabet.insert(s);
  }
}

TM HLCompiler::Compile(const Program& program) {
  tm_ = TM{};
  vars_.clear();
  next_var_index_ = 0;
  state_counter_ = 0;

  SetupAlphabet(program);

  tm_.start = NewState("start");
  tm_.accept = "qA";
  tm_.reject = "qR";
  tm_.states.insert(tm_.accept);
  tm_.states.insert(tm_.reject);

  State current = EmitPreamble(tm_.start);
  current = CompileStmts(program.body, current);

  // Default: accept at end
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[current].find(s) == tm_.delta[current].end()) {
      tm_.AddTransition(current, s, s, Dir::S, tm_.accept);
    }
  }

  tm_.Finalize();
  return tm_;
}

State HLCompiler::CompileStmts(const std::vector<StmtPtr>& stmts, State entry) {
  State current = entry;
  for (const auto& stmt : stmts) {
    current = CompileStmt(stmt, current);
  }
  return current;
}

State HLCompiler::CompileStmt(const StmtPtr& stmt, State entry) {
  if (auto* let = dynamic_cast<LetStmt*>(stmt.get())) {
    return CompileLet(*let, entry);
  } else if (auto* assign = dynamic_cast<AssignStmt*>(stmt.get())) {
    return CompileAssign(*assign, entry);
  } else if (auto* for_stmt = dynamic_cast<ForStmt*>(stmt.get())) {
    return CompileFor(*for_stmt, entry);
  } else if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt.get())) {
    return CompileIf(*if_stmt, entry);
  } else if (auto* ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
    return CompileReturn(*ret, entry);
  } else if (dynamic_cast<AcceptStmt*>(stmt.get())) {
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(entry, s, s, Dir::S, tm_.accept);
    }
    return tm_.accept;
  } else if (dynamic_cast<RejectStmt*>(stmt.get())) {
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(entry, s, s, Dir::S, tm_.reject);
    }
    return tm_.reject;
  } else if (auto* scan = dynamic_cast<ScanStmt*>(stmt.get())) {
    return CompileScan(*scan, entry);
  } else if (auto* write = dynamic_cast<WriteStmt*>(stmt.get())) {
    return CompileWrite(*write, entry);
  } else if (auto* move = dynamic_cast<MoveStmt*>(stmt.get())) {
    return CompileMove(*move, entry);
  } else if (auto* loop = dynamic_cast<LoopStmt*>(stmt.get())) {
    return CompileLoop(*loop, entry);
  } else if (auto* if_cur = dynamic_cast<IfCurrentStmt*>(stmt.get())) {
    return CompileIfCurrent(*if_cur, entry);
  } else if (auto* inc = dynamic_cast<IncStmt*>(stmt.get())) {
    return CompileInc(*inc, entry);
  } else if (auto* app = dynamic_cast<AppendStmt*>(stmt.get())) {
    return CompileAppend(*app, entry);
  } else if (auto* brk = dynamic_cast<BreakStmt*>(stmt.get())) {
    return CompileBreak(*brk, entry);
  } else if (auto* rw = dynamic_cast<RewindStmt*>(stmt.get())) {
    return CompileRewind(*rw, entry);
  } else if (auto* ifeq = dynamic_cast<IfEqStmt*>(stmt.get())) {
    return CompileIfEq(*ifeq, entry);
  }
  throw std::runtime_error("Unknown statement type");
}

State HLCompiler::CompileLet(const LetStmt& stmt, State entry) {
  DeclareVar(stmt.name);

  // Go to end of tape, add separator
  State scan_end = NewState("let_scan");
  State add_sep = NewState("let_sep");
  State go_back = NewState("let_back");

  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(scan_end, s, kSep, Dir::L, go_back);
    } else {
      tm_.AddTransition(scan_end, s, s, Dir::R, scan_end);
    }
  }

  // Go back to start of tape before evaluating expression
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(go_back, s, s, Dir::R, add_sep);
    } else {
      tm_.AddTransition(go_back, s, s, Dir::L, go_back);
    }
  }

  // Connect entry to scan
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, scan_end);
  }

  // Evaluate expression (now at start of tape)
  State expr_done = CompileExpr(stmt.init, stmt.name, add_sep);
  return EmitRewindToStart(expr_done);
}

State HLCompiler::CompileAssign(const AssignStmt& stmt, State entry) {
  // Handle: sum = sum + i
  auto* bin = dynamic_cast<BinExpr*>(stmt.value.get());
  if (bin && bin->op == BinOp::Add) {
    auto* left_var = dynamic_cast<Var*>(bin->left.get());
    if (left_var && left_var->name == stmt.name) {
      // sum = sum + something -> append to sum
      auto* right_var = dynamic_cast<Var*>(bin->right.get());
      if (right_var) {
        // sum = sum + i: copy i's value to end of sum's region
        return EmitCopyRegion(entry, GetVar(right_var->name).index,
                              GetVar(stmt.name).index);
      }
    }
  }

  throw std::runtime_error("Unsupported assignment: " + stmt.name);
}

State HLCompiler::CompileFor(const ForStmt& stmt, State entry) {
  // for i in 1..n { body }
  //
  // Naive approach:
  // 1. Create region for i
  // 2. Loop: increment i, compare to n, if i > n exit, else run body

  auto* start_lit = dynamic_cast<IntLit*>(stmt.start.get());
  if (!start_lit || start_lit->value != 1) {
    throw std::runtime_error("For loop must start at 1");
  }

  auto* end_var = dynamic_cast<Var*>(stmt.end.get());
  if (!end_var) {
    throw std::runtime_error("For loop end must be a variable");
  }

  DeclareVar(stmt.var);
  VarInfo& i_info = GetVar(stmt.var);
  VarInfo& n_info = GetVar(end_var->name);

  // Setup: add separator for i
  State setup = NewState("for_setup");
  State loop_head = NewState("for_head");
  State loop_body = NewState("for_body");
  State loop_end = NewState("for_end");

  // Go to end, add separator
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(setup, s, kSep, Dir::L, loop_head);
    } else {
      tm_.AddTransition(setup, s, s, Dir::R, setup);
    }
  }

  // Connect entry
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, setup);
  }

  // Loop head: increment i, then compare
  // Increment: go to i's region end, add 1
  State incr = EmitIncrementRegion(loop_head, i_info.index);

  // Compare i <= n using one-to-one matching
  // If i <= n, go to body; else exit
  State after_cmp = EmitCompareRegionToRegion(incr, i_info.index, n_info.index,
                                               loop_body, loop_end);

  // Compile body
  State body_done = CompileStmts(stmt.body, loop_body);

  // Go back to loop head (rewind to start first)
  State body_rewind = EmitRewindToStart(body_done);
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(body_rewind, s, s, Dir::S, loop_head);
  }

  // Rewind at loop exit too
  return EmitRewindToStart(loop_end);
}

State HLCompiler::CompileIf(const IfStmt& stmt, State entry) {
  State then_st = NewState("then");
  State else_st = NewState("else");
  State end_st = NewState("endif");

  auto* cmp = dynamic_cast<BinExpr*>(stmt.condition.get());
  if (!cmp || cmp->op != BinOp::Eq) {
    throw std::runtime_error("If condition must be == comparison");
  }

  // Handle: count(b) == sum
  auto* left_count = dynamic_cast<Count*>(cmp->left.get());
  auto* right_var = dynamic_cast<Var*>(cmp->right.get());

  if (left_count && right_var) {
    Symbol sym = left_count->symbol;
    Symbol marked_sym = (sym >= 'a' && sym <= 'z')
                        ? static_cast<Symbol>(sym - 'a' + 'A') : sym;
    VarInfo& var = GetVar(right_var->name);

    // One-to-one matching algorithm
    State go_start = NewState("match_rewind");
    State match_loop = NewState("match");
    State find_var = NewState("find_var");
    State back = NewState("back");
    State verify = NewState("verify");

    // First, rewind to start of tape (scan left for >)
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kLeftEnd) {
        tm_.AddTransition(go_start, s, s, Dir::R, match_loop);
      } else {
        tm_.AddTransition(go_start, s, s, Dir::L, go_start);
      }
    }

    // Connect entry to rewind
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(entry, s, s, Dir::S, go_start);
    }

    // Loop: find unmarked input symbol
    for (Symbol s : tm_.tape_alphabet) {
      if (s == sym) {
        // Mark it, go find var
        tm_.AddTransition(match_loop, s, marked_sym, Dir::R, find_var);
      } else if (s == marked_sym) {
        // Skip already marked
        tm_.AddTransition(match_loop, s, s, Dir::R, match_loop);
      } else if (s == kSep) {
        // Hit end of input, verify var is empty
        tm_.AddTransition(match_loop, s, s, Dir::R, verify);
      } else if (s == kBlank) {
        // Blank before separator means empty input - go verify
        tm_.AddTransition(match_loop, s, s, Dir::R, verify);
      } else {
        // Other input symbols (not the one we're counting)
        tm_.AddTransition(match_loop, s, s, Dir::R, match_loop);
      }
    }

    // Find unmarked 1 in var region
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kOne) {
        // Mark it, go back
        tm_.AddTransition(find_var, s, kMarked, Dir::L, back);
      } else if (s == kMarked || s == kSep) {
        tm_.AddTransition(find_var, s, s, Dir::R, find_var);
      } else if (s == kBlank) {
        // No match - not equal
        tm_.AddTransition(find_var, s, s, Dir::S, else_st);
      } else {
        tm_.AddTransition(find_var, s, s, Dir::R, find_var);
      }
    }

    // Go back to start (scan left for >)
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kLeftEnd) {
        tm_.AddTransition(back, s, s, Dir::R, match_loop);
      } else {
        tm_.AddTransition(back, s, s, Dir::L, back);
      }
    }

    // Verify var has no unmarked 1s
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kOne) {
        // Extra in var - not equal
        tm_.AddTransition(verify, s, s, Dir::S, else_st);
      } else if (s == kMarked || s == kSep) {
        tm_.AddTransition(verify, s, s, Dir::R, verify);
      } else if (s == kBlank) {
        // All matched
        tm_.AddTransition(verify, s, s, Dir::S, then_st);
      } else {
        tm_.AddTransition(verify, s, s, Dir::R, verify);
      }
    }

    // Entry already connected to go_start above

  } else {
    throw std::runtime_error("Unsupported if condition");
  }

  // Compile branches
  State then_done = CompileStmts(stmt.then_body, then_st);
  State else_done = stmt.else_body.empty() ? else_st : CompileStmts(stmt.else_body, else_st);

  // Join
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[then_done].find(s) == tm_.delta[then_done].end()) {
      tm_.AddTransition(then_done, s, s, Dir::S, end_st);
    }
    if (tm_.delta[else_done].find(s) == tm_.delta[else_done].end()) {
      tm_.AddTransition(else_done, s, s, Dir::S, end_st);
    }
  }

  return EmitRewindToStart(end_st);
}

State HLCompiler::CompileReturn(const ReturnStmt& stmt, State entry) {
  auto if_stmt = std::make_shared<IfStmt>();
  if_stmt->condition = stmt.value;
  if_stmt->then_body.push_back(std::make_shared<AcceptStmt>());
  if_stmt->else_body.push_back(std::make_shared<RejectStmt>());
  return CompileIf(*if_stmt, entry);
}

State HLCompiler::CompileScan(const ScanStmt& stmt, State entry) {
  // Scan in direction until one of the stop symbols
  State scan = NewState("scan");
  State done = NewState("scan_done");

  // Connect entry
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, scan);
  }

  // Scan loop
  for (Symbol s : tm_.tape_alphabet) {
    if (stmt.stop_symbols.count(s)) {
      // Stop here
      tm_.AddTransition(scan, s, s, Dir::S, done);
    } else {
      // Keep going
      tm_.AddTransition(scan, s, s, stmt.direction, scan);
    }
  }

  return done;
}

State HLCompiler::CompileWrite(const WriteStmt& stmt, State entry) {
  State done = NewState("write_done");

  // Write the symbol without moving
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, stmt.symbol, Dir::S, done);
  }

  return done;
}

State HLCompiler::CompileMove(const MoveStmt& stmt, State entry) {
  State done = NewState("move_done");

  // Move in the specified direction
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, stmt.direction, done);
  }

  return done;
}

State HLCompiler::CompileLoop(const LoopStmt& stmt, State entry) {
  // Infinite loop - exit via accept/reject/break
  State loop_head = NewState("loop_head");
  State loop_exit = NewState("loop_exit");

  // Push break target
  break_targets_.push(loop_exit);

  // Connect entry to loop head
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, loop_head);
  }

  // Compile body
  State body_end = CompileStmts(stmt.body, loop_head);

  // Loop back (unless body ended at accept/reject/break target)
  if (body_end != tm_.accept && body_end != tm_.reject && body_end != loop_exit) {
    for (Symbol s : tm_.tape_alphabet) {
      if (tm_.delta[body_end].find(s) == tm_.delta[body_end].end()) {
        tm_.AddTransition(body_end, s, s, Dir::S, loop_head);
      }
    }
  }

  // Pop break target
  break_targets_.pop();

  return loop_exit;
}

State HLCompiler::CompileIfCurrent(const IfCurrentStmt& stmt, State entry) {
  State end = NewState("if_cur_end");

  // Branch based on current symbol
  for (auto& [sym, body] : stmt.branches) {
    State branch_head = NewState("branch");
    tm_.AddTransition(entry, sym, sym, Dir::S, branch_head);

    State branch_end = CompileStmts(body, branch_head);
    if (branch_end != tm_.accept && branch_end != tm_.reject) {
      for (Symbol s : tm_.tape_alphabet) {
        if (tm_.delta[branch_end].find(s) == tm_.delta[branch_end].end()) {
          tm_.AddTransition(branch_end, s, s, Dir::S, end);
        }
      }
    }
  }

  // Handle else branch
  if (!stmt.else_body.empty()) {
    State else_head = NewState("else");
    for (Symbol s : tm_.tape_alphabet) {
      if (!stmt.branches.count(s) && tm_.delta[entry].find(s) == tm_.delta[entry].end()) {
        tm_.AddTransition(entry, s, s, Dir::S, else_head);
      }
    }
    State else_end = CompileStmts(stmt.else_body, else_head);
    if (else_end != tm_.accept && else_end != tm_.reject) {
      for (Symbol s : tm_.tape_alphabet) {
        if (tm_.delta[else_end].find(s) == tm_.delta[else_end].end()) {
          tm_.AddTransition(else_end, s, s, Dir::S, end);
        }
      }
    }
  } else {
    // No else - unhandled symbols go straight to end
    for (Symbol s : tm_.tape_alphabet) {
      if (!stmt.branches.count(s) && tm_.delta[entry].find(s) == tm_.delta[entry].end()) {
        tm_.AddTransition(entry, s, s, Dir::S, end);
      }
    }
  }

  return end;
}

State HLCompiler::EmitPreamble(State start) {
  // Shift input right by 1 cell and write > at cell 0.
  // Input starts at cell 0 (doty convention). After preamble:
  //   cell 0: >   cell 1..n: input   cell n+1: _
  //
  // One shared carry state per symbol. Each carry state "carries" that symbol:
  // on reading the next cell, it writes the carried symbol and transitions to
  // the carry state for whatever was displaced.

  State at_input = NewState("pre_done");

  // Create one carry state per non-blank tape symbol
  std::map<Symbol, State> carry_states;
  for (Symbol s : tm_.tape_alphabet) {
    if (s != kBlank && s != kLeftEnd) {
      carry_states[s] = NewState("pre_c");
    }
  }

  // From start: read cell 0, write >, move R, enter carry for that symbol
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      // Empty input: write >, move R, head at cell 1 (blank)
      tm_.AddTransition(start, kBlank, kLeftEnd, Dir::R, at_input);
    } else if (s != kLeftEnd) {
      tm_.AddTransition(start, s, kLeftEnd, Dir::R, carry_states[s]);
    }
  }

  // Each carry state: "I'm carrying symbol C"
  // On reading next cell:
  //   - if blank: write C, rewind to >, move R -> done
  //   - if non-blank D: write C, move R, enter carry[D]
  State done_rewind = NewState("pre_rw");
  for (auto& [carried, carry_st] : carry_states) {
    for (Symbol next : tm_.tape_alphabet) {
      if (next == kBlank) {
        // Deposit carried symbol at blank, rewind
        tm_.AddTransition(carry_st, kBlank, carried, Dir::L, done_rewind);
      } else if (next != kLeftEnd) {
        // Deposit carried, pick up displaced
        tm_.AddTransition(carry_st, next, carried, Dir::R, carry_states[next]);
      }
    }
  }

  // Rewind from end of carry back to >
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(done_rewind, s, s, Dir::R, at_input);
    } else {
      tm_.AddTransition(done_rewind, s, s, Dir::L, done_rewind);
    }
  }

  return at_input;
}

State HLCompiler::EmitRewindToStart(State entry) {
  State rewind = NewState("rewind");
  State at_start = NewState("at_start");

  // Move left one cell to start scanning
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::L, rewind);
  }

  // Scan left until > (left-end marker at cell 0)
  // On left-bounded tape, L from cell 0 stays at cell 0, so > always stops the scan
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      // Found left edge, move right onto first input symbol
      tm_.AddTransition(rewind, s, s, Dir::R, at_start);
    } else {
      tm_.AddTransition(rewind, s, s, Dir::L, rewind);
    }
  }

  return at_start;
}

State HLCompiler::CompileExpr(const ExprPtr& expr, const std::string& dest_var, State entry) {
  if (auto* count = dynamic_cast<Count*>(expr.get())) {
    return CompileCount(*count, dest_var, entry);
  } else if (auto* lit = dynamic_cast<IntLit*>(expr.get())) {
    if (lit->value == 0) {
      // Zero - empty region, just return
      return entry;
    }
    // Write lit->value many 1s
    State current = entry;
    for (int i = 0; i < lit->value; ++i) {
      State next = NewState("lit");
      tm_.AddTransition(current, kBlank, kOne, Dir::R, next);
      for (Symbol s : tm_.tape_alphabet) {
        if (s != kBlank) {
          tm_.AddTransition(current, s, s, Dir::R, current);
        }
      }
      current = next;
    }
    return current;
  } else if (auto* var = dynamic_cast<Var*>(expr.get())) {
    return EmitCopyRegion(entry, GetVar(var->name).index, GetVar(dest_var).index);
  }
  throw std::runtime_error("Unknown expression type");
}

State HLCompiler::CompileCount(const Count& expr, const std::string& dest_var, State entry) {
  Symbol sym = expr.symbol;
  Symbol marked = (sym >= 'a' && sym <= 'z')
                  ? static_cast<Symbol>(sym - 'a' + 'A') : sym;

  State scan = NewState("cnt_scan");
  State write = NewState("cnt_write");
  State back = NewState("cnt_back");
  State done = NewState("cnt_done");

  // Scan input for sym
  for (Symbol s : tm_.tape_alphabet) {
    if (s == sym) {
      tm_.AddTransition(scan, s, marked, Dir::R, write);
    } else if (s == kSep || s == kBlank) {
      tm_.AddTransition(scan, s, s, Dir::S, done);
    } else {
      tm_.AddTransition(scan, s, s, Dir::R, scan);
    }
  }

  // Go to end, write 1
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(write, s, kOne, Dir::L, back);
    } else {
      tm_.AddTransition(write, s, s, Dir::R, write);
    }
  }

  // Go back to start (scan left for >)
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(back, s, s, Dir::R, scan);
    } else {
      tm_.AddTransition(back, s, s, Dir::L, back);
    }
  }

  // Connect entry
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, scan);
  }

  // Restore input: rewind to start, then sweep right replacing marked→original
  State restore_rewind = NewState("cnt_rrewind");
  State restore_scan = NewState("cnt_restore");
  State restore_done = NewState("cnt_rdone");

  // done → rewind to start
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(done, s, s, Dir::L, restore_rewind);
  }

  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(restore_rewind, s, s, Dir::R, restore_scan);
    } else {
      tm_.AddTransition(restore_rewind, s, s, Dir::L, restore_rewind);
    }
  }

  // Sweep right: restore marked symbols, stop at # or blank
  for (Symbol s : tm_.tape_alphabet) {
    if (s == marked) {
      tm_.AddTransition(restore_scan, s, sym, Dir::R, restore_scan);
    } else if (s == kSep || s == kBlank) {
      tm_.AddTransition(restore_scan, s, s, Dir::S, restore_done);
    } else {
      tm_.AddTransition(restore_scan, s, s, Dir::R, restore_scan);
    }
  }

  return restore_done;
}

State HLCompiler::CompileBinExpr(const BinExpr& expr, const std::string& dest_var, State entry) {
  throw std::runtime_error("BinExpr compilation not implemented");
}

State HLCompiler::EmitCopyRegion(State entry, int src_region, int dest_region) {
  // Copy one 1 at a time from src to dest
  State find_src = NewState("cpy_src");
  State find_dest = NewState("cpy_dest");
  State back = NewState("cpy_back");
  State done = NewState("cpy_done");

  // Find an unmarked 1 in src
  int sep_count = 0;
  State current = entry;

  // Navigate to src region (skip sep_count separators)
  for (int i = 0; i <= src_region; ++i) {
    State next = NewState("cpy_nav");
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kSep) {
        tm_.AddTransition(current, s, s, Dir::R, next);
      } else {
        tm_.AddTransition(current, s, s, Dir::R, current);
      }
    }
    current = next;
  }

  // Now in src region, find unmarked 1
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(current, s, kMarked, Dir::R, find_dest);
    } else if (s == kMarked) {
      tm_.AddTransition(current, s, s, Dir::R, current);
    } else if (s == kSep || s == kBlank) {
      // Done copying from this region
      tm_.AddTransition(current, s, s, Dir::S, done);
    } else {
      tm_.AddTransition(current, s, s, Dir::R, current);
    }
  }

  // Find dest region end, write 1
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(find_dest, s, kOne, Dir::L, back);
    } else {
      tm_.AddTransition(find_dest, s, s, Dir::R, find_dest);
    }
  }

  // Go back to start (scan left for >), continue
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(back, s, s, Dir::R, entry);
    } else {
      tm_.AddTransition(back, s, s, Dir::L, back);
    }
  }

  return done;
}

State HLCompiler::EmitIncrementRegion(State entry, int region) {
  // Go to end of tape, add 1
  State done = NewState("inc_done");

  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(entry, s, kOne, Dir::L, done);
    } else {
      tm_.AddTransition(entry, s, s, Dir::R, entry);
    }
  }

  return done;
}

State HLCompiler::EmitCompareRegionToRegion(State entry, int region_a, int region_b,
                                             State if_le, State if_gt) {
  // Compare |a| <= |b|
  // Match 1s: for each 1 in a, mark one in b
  // If a exhausted first: a <= b
  // If b exhausted first: a > b

  State match_a = NewState("cmp_a");
  State find_b = NewState("cmp_b");
  State back = NewState("cmp_back");
  State check_a = NewState("cmp_chk");

  // Go to region a, find unmarked 1
  State to_a = entry;
  for (int i = 0; i <= region_a; ++i) {
    State next = NewState("cmp_nav");
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kSep) {
        tm_.AddTransition(to_a, s, s, Dir::R, next);
      } else {
        tm_.AddTransition(to_a, s, s, Dir::R, to_a);
      }
    }
    to_a = next;
  }

  // In region a, find unmarked 1
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      // Mark, go find b
      tm_.AddTransition(to_a, s, kMarked, Dir::R, find_b);
    } else if (s == kMarked) {
      tm_.AddTransition(to_a, s, s, Dir::R, to_a);
    } else if (s == kSep || s == kBlank) {
      // a exhausted, a <= b
      tm_.AddTransition(to_a, s, s, Dir::S, if_le);
    } else {
      tm_.AddTransition(to_a, s, s, Dir::R, to_a);
    }
  }

  // Find unmarked 1 in b
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      // Mark, go back
      tm_.AddTransition(find_b, s, kMarked, Dir::L, back);
    } else if (s == kMarked || s == kSep) {
      tm_.AddTransition(find_b, s, s, Dir::R, find_b);
    } else if (s == kBlank) {
      // b exhausted, a > b
      tm_.AddTransition(find_b, s, s, Dir::S, if_gt);
    } else {
      tm_.AddTransition(find_b, s, s, Dir::R, find_b);
    }
  }

  // Go back to start (scan left for >)
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kLeftEnd) {
      tm_.AddTransition(back, s, s, Dir::R, entry);
    } else {
      tm_.AddTransition(back, s, s, Dir::L, back);
    }
  }

  return if_le;  // Default path
}

State HLCompiler::EmitCompareRegions(State entry, int region_a, int region_b,
                                      State if_equal, State if_not_equal) {
  return EmitCompareRegionToRegion(entry, region_a, region_b, if_equal, if_not_equal);
}

// ==========================================================================
// VM INSTRUCTION COMPILATION
// ==========================================================================

State HLCompiler::EmitInsertInRegion(State entry, int region) {
  // Insert a 1 into the specified region.
  // Strategy: navigate to the end of the region (the # separator after it,
  // or blank if it's the last region), write 1 there, then shift everything
  // after it one cell to the right.
  //
  // For the last region: just scan to blank and write 1. No shift needed.
  // For non-last regions: write 1 at the #, then carry the displaced #
  // and all subsequent data one cell right.

  // First, navigate to end of target region.
  // Region 0 is after the 1st #, region 1 after the 2nd #, etc.
  // We need to find the (region+2)th separator-or-blank.
  // Actually: tape is _ [input] # [var0] # [var1] # ...
  // Region 0 starts after 1st #. Region 0 ends at 2nd #.
  // So end of region N is at separator #(N+2) (or blank if last).

  State nav = NewState("ins_nav");
  // Connect entry
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::R, nav);
  }

  // Skip (region + 1) separators to get past input and preceding regions
  State current = nav;
  for (int i = 0; i <= region; ++i) {
    State next = NewState("ins_sep");
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kSep) {
        tm_.AddTransition(current, s, s, Dir::R, next);
      } else if (s == kBlank) {
        // Hit end of tape before finding enough separators - this means
        // the region is the last one (or beyond). Write 1 here.
        // But this shouldn't happen if regions are set up correctly.
        tm_.AddTransition(current, s, s, Dir::S, next);
      } else {
        tm_.AddTransition(current, s, s, Dir::R, current);
      }
    }
    current = next;
  }

  // Now current is just past the last separator before our region's data.
  // Scan through the region's data (1s and Is) to find the end.
  State scan_data = NewState("ins_data");
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(current, s, s, Dir::S, scan_data);
  }

  State at_end = NewState("ins_at_end");
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne || s == kMarked) {
      // Still in region data, keep going
      tm_.AddTransition(scan_data, s, s, Dir::R, scan_data);
    } else {
      // Hit # or blank - this is where we insert
      tm_.AddTransition(scan_data, s, s, Dir::S, at_end);
    }
  }

  // at_end: head is on the # (or blank) at the end of the region.
  // Write 1 here, pick up what was here, shift right.
  State done = NewState("ins_done");

  // If it's blank, just write 1 - no shifting needed (last region)
  tm_.AddTransition(at_end, kBlank, kOne, Dir::S, done);

  // If it's #, write 1, carry # rightward
  // We need carry states for each symbol that might be displaced
  // Symbols that can appear after a region: #, 1, I, blank
  State carry_sep = NewState("carry_sep");
  State carry_one = NewState("carry_one");
  State carry_mark = NewState("carry_mark");

  tm_.AddTransition(at_end, kSep, kOne, Dir::R, carry_sep);

  // Carry #: read current, write #, pick up current, move right
  tm_.AddTransition(carry_sep, kBlank, kSep, Dir::S, done);
  tm_.AddTransition(carry_sep, kSep, kSep, Dir::R, carry_sep);
  tm_.AddTransition(carry_sep, kOne, kSep, Dir::R, carry_one);
  tm_.AddTransition(carry_sep, kMarked, kSep, Dir::R, carry_mark);
  // Input symbols and their marks shouldn't appear here, but be safe
  for (Symbol s : tm_.tape_alphabet) {
    if (s != kBlank && s != kSep && s != kOne && s != kMarked) {
      tm_.AddTransition(carry_sep, s, kSep, Dir::R, carry_one);  // treat as data
    }
  }

  // Carry 1
  tm_.AddTransition(carry_one, kBlank, kOne, Dir::S, done);
  tm_.AddTransition(carry_one, kSep, kOne, Dir::R, carry_sep);
  tm_.AddTransition(carry_one, kOne, kOne, Dir::R, carry_one);
  tm_.AddTransition(carry_one, kMarked, kOne, Dir::R, carry_mark);
  for (Symbol s : tm_.tape_alphabet) {
    if (s != kBlank && s != kSep && s != kOne && s != kMarked) {
      tm_.AddTransition(carry_one, s, kOne, Dir::R, carry_one);
    }
  }

  // Carry I (marked)
  tm_.AddTransition(carry_mark, kBlank, kMarked, Dir::S, done);
  tm_.AddTransition(carry_mark, kSep, kMarked, Dir::R, carry_sep);
  tm_.AddTransition(carry_mark, kOne, kMarked, Dir::R, carry_one);
  tm_.AddTransition(carry_mark, kMarked, kMarked, Dir::R, carry_mark);
  for (Symbol s : tm_.tape_alphabet) {
    if (s != kBlank && s != kSep && s != kOne && s != kMarked) {
      tm_.AddTransition(carry_mark, s, kMarked, Dir::R, carry_one);
    }
  }

  // Rewind from done back to start
  return EmitRewindToStart(done);
}

State HLCompiler::EmitRestoreRegion(State entry, int region) {
  // Rewind to start first, then navigate to region, convert all I -> 1, then rewind
  State at_start = EmitRewindToStart(entry);

  // Navigate from position 0: skip (region+1) separators
  State nav = NewState("rst_nav");
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(at_start, s, s, Dir::R, nav);
  }

  State current = nav;
  for (int i = 0; i <= region; ++i) {
    State next = NewState("rst_sep");
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kSep) {
        tm_.AddTransition(current, s, s, Dir::R, next);
      } else if (s == kBlank) {
        // Past end of tape - treat as if region found (empty)
        tm_.AddTransition(current, s, s, Dir::S, next);
      } else {
        tm_.AddTransition(current, s, s, Dir::R, current);
      }
    }
    current = next;
  }

  // Now in the region - sweep, converting I to 1
  State sweep = NewState("rst_sweep");
  State done = NewState("rst_done");
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(current, s, s, Dir::S, sweep);
  }

  for (Symbol s : tm_.tape_alphabet) {
    if (s == kMarked) {
      tm_.AddTransition(sweep, s, kOne, Dir::R, sweep);
    } else if (s == kOne) {
      tm_.AddTransition(sweep, s, s, Dir::R, sweep);
    } else {
      // Hit # or blank - done with this region
      tm_.AddTransition(sweep, s, s, Dir::S, done);
    }
  }

  return EmitRewindToStart(done);
}

State HLCompiler::EmitCompareEqual(State entry, int reg_a, int reg_b,
                                    State if_eq, State if_neq) {
  // One-to-one matching between two unary regions.
  // Algorithm: mark pairs of 1s (one from each region), repeat until
  // one region exhausts. If both exhaust simultaneously: equal.
  //
  // Helper lambda to emit "navigate from pos 0 to region R":
  // skips (R+1) separators, handles blank as implicit stop.
  // Returns the state positioned at start of region data.
  auto emitNavToRegion = [&](State start, int region) -> State {
    State nav = NewState("nav");
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(start, s, s, Dir::R, nav);
    }
    State cur = nav;
    for (int i = 0; i <= region; ++i) {
      State next = NewState("navsep");
      for (Symbol s : tm_.tape_alphabet) {
        if (s == kSep) {
          tm_.AddTransition(cur, s, s, Dir::R, next);
        } else if (s == kBlank) {
          tm_.AddTransition(cur, s, s, Dir::S, next);
        } else {
          tm_.AddTransition(cur, s, s, Dir::R, cur);
        }
      }
      cur = next;
    }
    return cur;
  };

  State restore_eq = NewState("ceq_req");
  State restore_neq = NewState("ceq_rneq");
  State a_done = NewState("ceq_adone");

  // Phase 1: Find unmarked 1 in region a
  State in_a = emitNavToRegion(entry, reg_a);

  State find_b = NewState("ceq_fb");
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(in_a, s, kMarked, Dir::S, find_b);
    } else if (s == kMarked) {
      tm_.AddTransition(in_a, s, s, Dir::R, in_a);
    } else {
      // Region a exhausted
      tm_.AddTransition(in_a, s, s, Dir::S, a_done);
    }
  }

  // Phase 2: Rewind, navigate to region b, find unmarked 1
  State rw_b = EmitRewindToStart(find_b);
  State in_b = emitNavToRegion(rw_b, reg_b);

  State back_to_a = NewState("ceq_back");
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(in_b, s, kMarked, Dir::S, back_to_a);
    } else if (s == kMarked) {
      tm_.AddTransition(in_b, s, s, Dir::R, in_b);
    } else {
      // Region b exhausted first: not equal
      tm_.AddTransition(in_b, s, s, Dir::S, restore_neq);
    }
  }

  // Phase 3: Rewind, go back to region a for next pair
  State rw_a = EmitRewindToStart(back_to_a);
  State in_a2 = emitNavToRegion(rw_a, reg_a);

  // Same logic as in_a but reuse find_b
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(in_a2, s, kMarked, Dir::S, find_b);
    } else if (s == kMarked) {
      tm_.AddTransition(in_a2, s, s, Dir::R, in_a2);
    } else {
      tm_.AddTransition(in_a2, s, s, Dir::S, a_done);
    }
  }

  // Phase 4: Region a exhausted. Check if region b has remaining unmarked 1s.
  State rw_chk = EmitRewindToStart(a_done);
  State in_b_chk = emitNavToRegion(rw_chk, reg_b);

  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(in_b_chk, s, s, Dir::S, restore_neq);
    } else if (s == kMarked) {
      tm_.AddTransition(in_b_chk, s, s, Dir::R, in_b_chk);
    } else {
      // Both exhausted: equal
      tm_.AddTransition(in_b_chk, s, s, Dir::S, restore_eq);
    }
  }

  // Restore both regions then branch
  State after_ra_eq = EmitRestoreRegion(restore_eq, reg_a);
  State after_rb_eq = EmitRestoreRegion(after_ra_eq, reg_b);
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[after_rb_eq].find(s) == tm_.delta[after_rb_eq].end()) {
      tm_.AddTransition(after_rb_eq, s, s, Dir::S, if_eq);
    }
  }

  State after_ra_neq = EmitRestoreRegion(restore_neq, reg_a);
  State after_rb_neq = EmitRestoreRegion(after_ra_neq, reg_b);
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[after_rb_neq].find(s) == tm_.delta[after_rb_neq].end()) {
      tm_.AddTransition(after_rb_neq, s, s, Dir::S, if_neq);
    }
  }

  return if_eq;
}

State HLCompiler::EmitAppendNonDestructive(State entry, int src, int dst) {
  // Copy src region to dst region without destroying src.
  // 1. Navigate to src, find unmarked 1, mark as I
  // 2. Insert a 1 into dst (using EmitInsertInRegion)
  // 3. Rewind, repeat from 1
  // 4. When src exhausted: restore src marks (I -> 1)

  State loop_start = NewState("appnd_loop");
  State find_src = NewState("appnd_find");
  State insert = NewState("appnd_ins");
  State src_done = NewState("appnd_done");

  // Connect entry to loop start
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, loop_start);
  }

  // Navigate to src region from position 0
  State nav = loop_start;
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(nav, s, s, Dir::R, find_src);
  }

  State cur = find_src;
  for (int i = 0; i <= src; ++i) {
    State next = NewState("appnd_nav");
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kSep) {
        tm_.AddTransition(cur, s, s, Dir::R, next);
      } else if (s == kBlank) {
        tm_.AddTransition(cur, s, s, Dir::S, next);
      } else {
        tm_.AddTransition(cur, s, s, Dir::R, cur);
      }
    }
    cur = next;
  }

  // In src region: find unmarked 1
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kOne) {
      tm_.AddTransition(cur, s, kMarked, Dir::S, insert);
    } else if (s == kMarked) {
      tm_.AddTransition(cur, s, s, Dir::R, cur);
    } else {
      // # or blank: src exhausted
      tm_.AddTransition(cur, s, s, Dir::S, src_done);
    }
  }

  // insert: rewind to start, then insert 1 into dst
  State pre_insert = EmitRewindToStart(insert);
  State after_insert = EmitInsertInRegion(pre_insert, dst);

  // after_insert returns at position 0. Loop back.
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[after_insert].find(s) == tm_.delta[after_insert].end()) {
      tm_.AddTransition(after_insert, s, s, Dir::S, loop_start);
    }
  }

  // src_done: restore src marks
  State pre_restore = EmitRewindToStart(src_done);
  State after_restore = EmitRestoreRegion(pre_restore, src);

  return after_restore;
}

State HLCompiler::CompileInc(const IncStmt& stmt, State entry) {
  VarInfo& var = GetVar(stmt.reg);
  State after = EmitInsertInRegion(entry, var.index);
  return after;  // EmitInsertInRegion already rewinds
}

State HLCompiler::CompileAppend(const AppendStmt& stmt, State entry) {
  VarInfo& src = GetVar(stmt.src);
  VarInfo& dst = GetVar(stmt.dst);
  return EmitAppendNonDestructive(entry, src.index, dst.index);
}

State HLCompiler::CompileBreak(const BreakStmt& stmt, State entry) {
  if (break_targets_.empty()) {
    throw std::runtime_error("break outside of loop");
  }
  State target = break_targets_.top();
  // Wire entry to break target
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, target);
  }
  return target;
}

State HLCompiler::CompileRewind(const RewindStmt& stmt, State entry) {
  State scan = NewState("rw");
  State done = NewState("rw_done");

  if (stmt.direction == Dir::L) {
    // Scan left until > (left-end marker), stay on it
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kLeftEnd) {
        tm_.AddTransition(scan, s, s, Dir::S, done);
      } else {
        tm_.AddTransition(scan, s, s, Dir::L, scan);
      }
    }
  } else {
    // Scan right until _ (blank), stay on it
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kBlank) {
        tm_.AddTransition(scan, s, s, Dir::S, done);
      } else {
        tm_.AddTransition(scan, s, s, Dir::R, scan);
      }
    }
  }

  // Wire entry to scan
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, scan);
  }

  return done;
}

State HLCompiler::CompileIfEq(const IfEqStmt& stmt, State entry) {
  VarInfo& a = GetVar(stmt.reg_a);
  VarInfo& b = GetVar(stmt.reg_b);

  State then_st = NewState("ifeq_then");
  State else_st = NewState("ifeq_else");
  State end_st = NewState("ifeq_end");

  EmitCompareEqual(entry, a.index, b.index, then_st, else_st);

  // Compile branches
  State then_done = CompileStmts(stmt.then_body, then_st);
  State else_done = stmt.else_body.empty() ? else_st : CompileStmts(stmt.else_body, else_st);

  // Join (skip if ended at accept/reject/break)
  for (Symbol s : tm_.tape_alphabet) {
    if (tm_.delta[then_done].find(s) == tm_.delta[then_done].end()) {
      tm_.AddTransition(then_done, s, s, Dir::S, end_st);
    }
    if (tm_.delta[else_done].find(s) == tm_.delta[else_done].end()) {
      tm_.AddTransition(else_done, s, s, Dir::S, end_st);
    }
  }

  return EmitRewindToStart(end_st);
}

TM CompileProgram(const Program& program) {
  HLCompiler compiler;
  return compiler.Compile(program);
}

}  // namespace tmc
