#include "tmc/hlcompiler.hpp"
#include <stdexcept>
#include <cassert>

namespace tmc {

// Tape layout:
// [input]#[var0]#[var1]#...
// Variables stored as unary: value 3 = "111"

constexpr Symbol kSep = '#';
constexpr Symbol kOne = '1';
constexpr Symbol kMarked = 'I';

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

  // Marked versions of input symbols
  for (Symbol s : program.input_alphabet) {
    if (s >= 'a' && s <= 'z') {
      tm_.tape_alphabet.insert(static_cast<Symbol>(s - 'a' + 'A'));
    }
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

  State current = tm_.start;
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
    if (s == kBlank) {
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
  return CompileExpr(stmt.init, stmt.name, add_sep);
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

  // Go back to loop head
  State rewind = NewState("for_rewind");
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(body_done, s, s, Dir::R, loop_head);
    } else {
      tm_.AddTransition(body_done, s, s, Dir::L, body_done);
    }
  }

  return loop_end;
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

    // First, rewind to start of tape
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kBlank) {
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

    // Go back to start
    for (Symbol s : tm_.tape_alphabet) {
      if (s == kBlank) {
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

  return end_st;
}

State HLCompiler::CompileReturn(const ReturnStmt& stmt, State entry) {
  auto if_stmt = std::make_shared<IfStmt>();
  if_stmt->condition = stmt.value;
  if_stmt->then_body.push_back(std::make_shared<AcceptStmt>());
  if_stmt->else_body.push_back(std::make_shared<RejectStmt>());
  return CompileIf(*if_stmt, entry);
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

  // Go back to start
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
      tm_.AddTransition(back, s, s, Dir::R, scan);
    } else {
      tm_.AddTransition(back, s, s, Dir::L, back);
    }
  }

  // Connect entry
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, s, Dir::S, scan);
  }

  return done;
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

  // Go back to start, continue
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
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

  // Go back to start
  for (Symbol s : tm_.tape_alphabet) {
    if (s == kBlank) {
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

TM CompileProgram(const Program& program) {
  HLCompiler compiler;
  return compiler.Compile(program);
}

}  // namespace tmc
