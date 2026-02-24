#include "tmc/codegen.hpp"
#include <algorithm>

namespace tmc {

namespace {

std::string DirToStr(Dir d) {
  switch (d) {
    case Dir::L: return "L";
    case Dir::R: return "R";
    case Dir::S: return "S";
  }
  return "S";
}

std::string EscapeYAML(const std::string& s) {
  // Quote if contains special characters
  bool needs_quote = false;
  for (char c : s) {
    if (c == ':' || c == '#' || c == '\'' || c == '"' ||
        c == '[' || c == ']' || c == '{' || c == '}' ||
        c == '!' || c == '|' || c == '>' || c == '*' || c == '&') {
      needs_quote = true;
      break;
    }
  }
  if (needs_quote) {
    return "'" + s + "'";
  }
  return s;
}

std::string SymbolToStr(Symbol s) {
  if (s == kBlank) return "_";
  if (s == kWildcard) return "'?'";
  std::string str(1, s);
  return EscapeYAML(str);
}

}  // namespace

std::string ToYAML(const TM& tm) {
  std::ostringstream out;

  // States
  out << "states: [";
  bool first = true;
  for (const auto& state : tm.states) {
    if (!first) out << ", ";
    out << EscapeYAML(state);
    first = false;
  }
  out << "]\n";

  // Input alphabet
  out << "input_alphabet: [";
  first = true;
  for (Symbol s : tm.input_alphabet) {
    if (!first) out << ", ";
    out << SymbolToStr(s);
    first = false;
  }
  out << "]\n";

  // Tape alphabet extra (symbols not in input alphabet or blank)
  std::set<Symbol> extra;
  for (Symbol s : tm.tape_alphabet) {
    if (s != kBlank && tm.input_alphabet.find(s) == tm.input_alphabet.end()) {
      extra.insert(s);
    }
  }
  if (!extra.empty()) {
    out << "tape_alphabet_extra: [";
    first = true;
    for (Symbol s : extra) {
      if (!first) out << ", ";
      out << SymbolToStr(s);
      first = false;
    }
    out << "]\n";
  }

  // Start, accept, reject states
  out << "start_state: " << EscapeYAML(tm.start) << "\n";
  out << "accept_state: " << EscapeYAML(tm.accept) << "\n";
  out << "reject_state: " << EscapeYAML(tm.reject) << "\n";

  // Delta (skip accept/reject — they're halt states with no outgoing transitions)
  out << "\ndelta:\n";
  for (const auto& [state, trans_map] : tm.delta) {
    if (state == tm.accept || state == tm.reject) continue;
    out << "  " << EscapeYAML(state) << ":\n";
    for (const auto& [sym, trans] : trans_map) {
      out << "    " << SymbolToStr(sym) << ": ["
          << EscapeYAML(trans.next) << ", "
          << SymbolToStr(trans.write) << ", "
          << DirToStr(trans.dir) << "]\n";
    }
  }

  return out.str();
}

// StateGen implementation
State StateGen::Next(const std::string& prefix) {
  return prefix + std::to_string(counter_++);
}

void StateGen::Reset() {
  counter_ = 0;
}

// Compiler implementation
Compiler::Compiler() = default;

TM Compiler::Compile(const IRProgram& program) {
  tm_ = TM{};
  tm_.input_alphabet = program.input_alphabet;
  for (Symbol s : program.tape_alphabet_extra) {
    tm_.tape_alphabet.insert(s);
  }
  gen_.Reset();

  tm_.start = gen_.Next("start");
  tm_.accept = "qA";
  tm_.reject = "qR";
  tm_.states.insert(tm_.accept);
  tm_.states.insert(tm_.reject);

  if (program.body.empty()) {
    // Empty program accepts everything
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(tm_.start, s, s, Dir::S, tm_.accept);
    }
    tm_.AddTransition(tm_.start, kBlank, kBlank, Dir::S, tm_.accept);
  } else {
    auto result = CompileBlock(program.body);
    // Link start to entry
    // Actually, entry IS start for the first block
    // We need to wire the exit to accept
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(result.exit, s, s, Dir::S, tm_.accept);
    }
    tm_.AddTransition(result.exit, kBlank, kBlank, Dir::S, tm_.accept);
  }

  tm_.Finalize();
  return tm_;
}

Compiler::CompileResult Compiler::CompileNode(const IRNodePtr& node) {
  if (auto* scan = dynamic_cast<ScanUntil*>(node.get())) {
    return CompileScanUntil(*scan);
  } else if (auto* write = dynamic_cast<WriteSymbol*>(node.get())) {
    return CompileWriteSymbol(*write);
  } else if (auto* move = dynamic_cast<Move*>(node.get())) {
    return CompileMove(*move);
  } else if (auto* if_sym = dynamic_cast<IfSymbol*>(node.get())) {
    return CompileIfSymbol(*if_sym);
  } else if (auto* while_sym = dynamic_cast<WhileSymbol*>(node.get())) {
    return CompileWhileSymbol(*while_sym);
  } else if (auto* mark = dynamic_cast<Mark*>(node.get())) {
    return CompileMark(*mark);
  } else if (dynamic_cast<Accept*>(node.get())) {
    State s = gen_.Next("acc");
    tm_.states.insert(s);
    // Transition to accept on any symbol
    for (Symbol sym : tm_.tape_alphabet) {
      tm_.AddTransition(s, sym, sym, Dir::S, tm_.accept);
    }
    tm_.AddTransition(s, kBlank, kBlank, Dir::S, tm_.accept);
    return {s, tm_.accept};
  } else if (dynamic_cast<Reject*>(node.get())) {
    State s = gen_.Next("rej");
    tm_.states.insert(s);
    for (Symbol sym : tm_.tape_alphabet) {
      tm_.AddTransition(s, sym, sym, Dir::S, tm_.reject);
    }
    tm_.AddTransition(s, kBlank, kBlank, Dir::S, tm_.reject);
    return {s, tm_.reject};
  }

  // Unknown node type - should not happen
  State s = gen_.Next("err");
  return {s, s};
}

Compiler::CompileResult Compiler::CompileScanUntil(const ScanUntil& scan) {
  State entry = gen_.Next("scan");
  State exit = gen_.Next("found");
  tm_.states.insert(entry);
  tm_.states.insert(exit);

  for (Symbol s : tm_.tape_alphabet) {
    if (scan.stop_symbols.count(s)) {
      // Found stop symbol - stay and exit
      tm_.AddTransition(entry, s, s, Dir::S, exit);
    } else {
      // Not a stop symbol - keep scanning
      tm_.AddTransition(entry, s, s, scan.direction, entry);
    }
  }
  // Handle blank
  if (scan.stop_symbols.count(kBlank)) {
    tm_.AddTransition(entry, kBlank, kBlank, Dir::S, exit);
  } else {
    tm_.AddTransition(entry, kBlank, kBlank, scan.direction, entry);
  }

  return {entry, exit};
}

Compiler::CompileResult Compiler::CompileWriteSymbol(const WriteSymbol& write) {
  State entry = gen_.Next("write");
  State exit = gen_.Next("wrote");
  tm_.states.insert(entry);
  tm_.states.insert(exit);

  // On any symbol, write the target symbol and stay
  for (Symbol s : tm_.tape_alphabet) {
    tm_.AddTransition(entry, s, write.symbol, Dir::S, exit);
  }
  tm_.AddTransition(entry, kBlank, write.symbol, Dir::S, exit);

  return {entry, exit};
}

Compiler::CompileResult Compiler::CompileMove(const Move& move) {
  if (move.count <= 0) {
    State s = gen_.Next("nop");
    tm_.states.insert(s);
    return {s, s};
  }

  State entry = gen_.Next("mv");
  State current = entry;
  tm_.states.insert(entry);

  for (int i = 0; i < move.count; ++i) {
    State next = (i == move.count - 1) ? gen_.Next("moved") : gen_.Next("mv");
    tm_.states.insert(next);

    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(current, s, s, move.direction, next);
    }
    tm_.AddTransition(current, kBlank, kBlank, move.direction, next);
    current = next;
  }

  return {entry, current};
}

Compiler::CompileResult Compiler::CompileIfSymbol(const IfSymbol& if_sym) {
  State entry = gen_.Next("if");
  State exit = gen_.Next("endif");
  tm_.states.insert(entry);
  tm_.states.insert(exit);

  std::set<Symbol> handled;

  for (const auto& [sym, body] : if_sym.branches) {
    handled.insert(sym);
    if (body.empty()) {
      tm_.AddTransition(entry, sym, sym, Dir::S, exit);
    } else {
      auto result = CompileBlock(body);
      tm_.AddTransition(entry, sym, sym, Dir::S, result.entry);
      // Wire exit of branch to the if exit
      for (Symbol s : tm_.tape_alphabet) {
        tm_.AddTransition(result.exit, s, s, Dir::S, exit);
      }
      tm_.AddTransition(result.exit, kBlank, kBlank, Dir::S, exit);
    }
  }

  // Else branch for unhandled symbols
  if (!if_sym.else_branch.empty()) {
    auto result = CompileBlock(if_sym.else_branch);
    for (Symbol s : tm_.tape_alphabet) {
      if (!handled.count(s)) {
        tm_.AddTransition(entry, s, s, Dir::S, result.entry);
      }
    }
    if (!handled.count(kBlank)) {
      tm_.AddTransition(entry, kBlank, kBlank, Dir::S, result.entry);
    }
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(result.exit, s, s, Dir::S, exit);
    }
    tm_.AddTransition(result.exit, kBlank, kBlank, Dir::S, exit);
  } else {
    // No else - just go to exit
    for (Symbol s : tm_.tape_alphabet) {
      if (!handled.count(s)) {
        tm_.AddTransition(entry, s, s, Dir::S, exit);
      }
    }
    if (!handled.count(kBlank)) {
      tm_.AddTransition(entry, kBlank, kBlank, Dir::S, exit);
    }
  }

  return {entry, exit};
}

Compiler::CompileResult Compiler::CompileWhileSymbol(const WhileSymbol& while_sym) {
  State entry = gen_.Next("while");
  State exit = gen_.Next("endwhile");
  tm_.states.insert(entry);
  tm_.states.insert(exit);

  if (while_sym.body.empty()) {
    // Empty body - infinite loop if condition met, exit otherwise
    for (Symbol s : while_sym.continue_symbols) {
      tm_.AddTransition(entry, s, s, Dir::S, entry);
    }
    for (Symbol s : tm_.tape_alphabet) {
      if (!while_sym.continue_symbols.count(s)) {
        tm_.AddTransition(entry, s, s, Dir::S, exit);
      }
    }
    if (!while_sym.continue_symbols.count(kBlank)) {
      tm_.AddTransition(entry, kBlank, kBlank, Dir::S, exit);
    }
  } else {
    auto body_result = CompileBlock(while_sym.body);

    // Entry: if continue symbol, go to body; else exit
    for (Symbol s : while_sym.continue_symbols) {
      tm_.AddTransition(entry, s, s, Dir::S, body_result.entry);
    }
    for (Symbol s : tm_.tape_alphabet) {
      if (!while_sym.continue_symbols.count(s)) {
        tm_.AddTransition(entry, s, s, Dir::S, exit);
      }
    }
    if (!while_sym.continue_symbols.count(kBlank)) {
      tm_.AddTransition(entry, kBlank, kBlank, Dir::S, exit);
    }

    // Body exit: loop back to entry (check condition again)
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(body_result.exit, s, s, Dir::S, entry);
    }
    tm_.AddTransition(body_result.exit, kBlank, kBlank, Dir::S, entry);
  }

  return {entry, exit};
}

Compiler::CompileResult Compiler::CompileMark(const Mark& mark) {
  State entry = gen_.Next("mark");
  State exit = gen_.Next("marked");
  tm_.states.insert(entry);
  tm_.states.insert(exit);

  for (Symbol s : tm_.tape_alphabet) {
    auto it = mark.mark_map.find(s);
    if (it != mark.mark_map.end()) {
      tm_.AddTransition(entry, s, it->second, Dir::S, exit);
      tm_.tape_alphabet.insert(it->second);
    } else {
      tm_.AddTransition(entry, s, s, Dir::S, exit);
    }
  }
  tm_.AddTransition(entry, kBlank, kBlank, Dir::S, exit);

  return {entry, exit};
}

Compiler::CompileResult Compiler::CompileBlock(const std::vector<IRNodePtr>& body) {
  if (body.empty()) {
    State s = gen_.Next("empty");
    tm_.states.insert(s);
    return {s, s};
  }

  auto first = CompileNode(body[0]);
  State entry = first.entry;
  State current_exit = first.exit;

  for (size_t i = 1; i < body.size(); ++i) {
    auto next = CompileNode(body[i]);

    // Wire current exit to next entry
    for (Symbol s : tm_.tape_alphabet) {
      tm_.AddTransition(current_exit, s, s, Dir::S, next.entry);
    }
    tm_.AddTransition(current_exit, kBlank, kBlank, Dir::S, next.entry);

    current_exit = next.exit;
  }

  return {entry, current_exit};
}

TM CompileIR(const IRProgram& program) {
  Compiler compiler;
  return compiler.Compile(program);
}

}  // namespace tmc
