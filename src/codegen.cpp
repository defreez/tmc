#include "tmc/codegen.hpp"
#include <algorithm>
#include <stdexcept>

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

// Parse a symbol token from YAML (handles quoting like '#', '>')
namespace {

std::string Trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Strip surrounding single quotes from a YAML token
std::string Unquote(const std::string& s) {
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

Symbol ParseSymbol(const std::string& raw) {
  std::string s = Trim(raw);
  s = Unquote(s);
  if (s == "_") return kBlank;
  if (s.size() != 1) {
    throw std::runtime_error("Invalid symbol in YAML: '" + raw + "'");
  }
  return s[0];
}

Dir ParseDir(const std::string& raw) {
  std::string s = Trim(raw);
  if (s == "L") return Dir::L;
  if (s == "R") return Dir::R;
  if (s == "S") return Dir::S;
  throw std::runtime_error("Invalid direction in YAML: '" + raw + "'");
}

// Parse a YAML inline list like [a, b, c] into tokens
std::vector<std::string> ParseList(const std::string& line) {
  std::vector<std::string> result;
  size_t open = line.find('[');
  size_t close = line.rfind(']');
  if (open == std::string::npos || close == std::string::npos) return result;

  std::string inner = line.substr(open + 1, close - open - 1);

  // Split on commas, but respect single quotes
  std::string token;
  bool in_quote = false;
  for (char c : inner) {
    if (c == '\'' ) {
      in_quote = !in_quote;
      token += c;
    } else if (c == ',' && !in_quote) {
      result.push_back(Trim(token));
      token.clear();
    } else {
      token += c;
    }
  }
  if (!Trim(token).empty()) {
    result.push_back(Trim(token));
  }
  return result;
}

// Get value after "key: value"
std::string ValueAfterColon(const std::string& line) {
  size_t pos = line.find(':');
  if (pos == std::string::npos) return "";
  return Trim(line.substr(pos + 1));
}

// Parse inline transitions: {sym:[next,write,dir],sym:[next,write,dir],...}
// content is the string inside the outer braces
void ParseInlineTransitions(TM& tm, const std::string& state_name,
                            const std::string& content) {
  size_t pos = 0;
  while (pos < content.size()) {
    // Skip whitespace and commas between entries
    while (pos < content.size() && (content[pos] == ' ' || content[pos] == ','))
      ++pos;
    if (pos >= content.size()) break;

    // Find ":[" which separates symbol key from value list
    size_t bracket_colon = content.find(":[", pos);
    if (bracket_colon == std::string::npos) break;

    std::string sym_str = Trim(content.substr(pos, bracket_colon - pos));

    // Find closing ']'
    size_t bracket_close = content.find(']', bracket_colon + 2);
    if (bracket_close == std::string::npos) break;

    // Extract [next,write,dir] and parse with ParseList
    std::string list_str = content.substr(bracket_colon + 1,
                                          bracket_close - bracket_colon);
    auto tokens = ParseList(list_str);
    if (tokens.size() != 3) {
      throw std::runtime_error(
          "Expected 3 elements in inline transition for state " + state_name +
          ": " + list_str);
    }

    Symbol read_sym = ParseSymbol(sym_str);
    State next_state = Unquote(tokens[0]);
    Symbol write_sym = ParseSymbol(tokens[1]);
    Dir dir = ParseDir(tokens[2]);
    tm.AddTransition(state_name, read_sym, write_sym, dir, next_state);

    pos = bracket_close + 1;
  }
}

}  // namespace

TM FromYAML(const std::string& yaml) {
  TM tm;
  std::istringstream in(yaml);
  std::string line;

  std::string current_state;
  bool in_delta = false;

  // State for YAML block sequence format (adan-blanco style):
  //   sym:
  //   - next
  //   - write
  //   - dir
  std::string pending_read_sym;
  std::vector<std::string> pending_values;

  while (std::getline(in, line)) {
    // Strip trailing \r for Windows line endings
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;  // skip comments/blanks

    // Detect indentation level
    size_t indent = line.find_first_not_of(' ');

    if (trimmed.rfind("states:", 0) == 0 && indent == 0) {
      in_delta = false;
      continue;
    } else if (trimmed.rfind("input_alphabet:", 0) == 0 && indent == 0) {
      in_delta = false;
      auto tokens = ParseList(trimmed);
      for (const auto& t : tokens) {
        tm.input_alphabet.insert(ParseSymbol(t));
      }
    } else if (trimmed.rfind("tape_alphabet_extra:", 0) == 0 && indent == 0) {
      in_delta = false;
      auto tokens = ParseList(trimmed);
      for (const auto& t : tokens) {
        tm.tape_alphabet.insert(ParseSymbol(t));
      }
    } else if (trimmed.rfind("start_state:", 0) == 0 && indent == 0) {
      in_delta = false;
      tm.start = Unquote(ValueAfterColon(trimmed));
    } else if (trimmed.rfind("accept_state:", 0) == 0 && indent == 0) {
      in_delta = false;
      tm.accept = Unquote(ValueAfterColon(trimmed));
    } else if (trimmed.rfind("reject_state:", 0) == 0 && indent == 0) {
      in_delta = false;
      tm.reject = Unquote(ValueAfterColon(trimmed));
    } else if (trimmed.rfind("delta:", 0) == 0 && indent == 0) {
      in_delta = true;
    } else if (in_delta) {
      // Inside delta section

      // Handle YAML block sequence lines: "    - value"
      if (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ') {
        if (!pending_read_sym.empty() && !current_state.empty()) {
          pending_values.push_back(Trim(trimmed.substr(2)));
          if (pending_values.size() == 3) {
            State next_state = Unquote(pending_values[0]);
            Symbol write_sym = ParseSymbol(pending_values[1]);
            Dir dir = ParseDir(pending_values[2]);
            tm.AddTransition(current_state, ParseSymbol(pending_read_sym),
                             write_sym, dir, next_state);
            pending_read_sym.clear();
            pending_values.clear();
          }
        }
        continue;
      }

      // Non-list-item line: discard any incomplete block sequence
      pending_read_sym.clear();
      pending_values.clear();

      size_t colon_pos = trimmed.find(':');
      if (colon_pos == std::string::npos) continue;

      if (indent >= 1 && indent < 4) {
        // State name line (indent 1 or 2)
        std::string state_name = Unquote(Trim(trimmed.substr(0, colon_pos)));
        std::string rest = Trim(trimmed.substr(colon_pos + 1));

        current_state = state_name;

        if (!rest.empty() && rest[0] == '{') {
          // Inline format: state: {sym:[next,write,dir],...}
          std::string inner = rest.substr(1);
          if (!inner.empty() && inner.back() == '}') inner.pop_back();
          ParseInlineTransitions(tm, current_state, inner);
        }
        // else: standard multi-line format, just set current_state
      } else if (indent >= 4 && !current_state.empty()) {
        // Transition line
        std::string sym_str = Trim(trimmed.substr(0, colon_pos));
        std::string rest = Trim(trimmed.substr(colon_pos + 1));

        if (rest.empty()) {
          // Block sequence format: bare "sym:" followed by "- value" lines
          pending_read_sym = sym_str;
          pending_values.clear();
        } else {
          // Standard format: "sym: [next, write, dir]"
          Symbol read_sym = ParseSymbol(sym_str);
          auto tokens = ParseList(trimmed);
          if (tokens.size() != 3) {
            throw std::runtime_error("Expected 3 elements in transition: " +
                                     trimmed);
          }

          State next_state = Unquote(tokens[0]);
          Symbol write_sym = ParseSymbol(tokens[1]);
          Dir dir = ParseDir(tokens[2]);

          tm.AddTransition(current_state, read_sym, write_sym, dir, next_state);
        }
      }
    }
  }

  // Add input alphabet to tape alphabet
  for (Symbol s : tm.input_alphabet) {
    tm.tape_alphabet.insert(s);
  }

  tm.Finalize();
  return tm;
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
