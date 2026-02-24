#include "tmc/parser.hpp"
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace tmc {

namespace {

class Lexer {
public:
  enum class Tok {
    Eof, Newline, Ident, Number, Symbol, String,
    LBrace, RBrace, LParen, RParen, LBracket, RBracket,
    Colon, Comma, Semicolon, Equals, DoubleEquals,
    Plus, Minus, Star, Slash, Lt, Le, Gt, Ge, Ne,
    DotDot,  // ..
  };

  struct Token {
    Tok type;
    std::string text;
    int line, col;
  };

  explicit Lexer(const std::string& src) : src_(src), pos_(0), line_(1), col_(1) {}

  Token Next() {
    SkipWS();
    if (pos_ >= src_.size()) return {Tok::Eof, "", line_, col_};

    int l = line_, c = col_;
    char ch = src_[pos_];

    if (ch == '\n') { Adv(); return {Tok::Newline, "\n", l, c}; }
    if (ch == '{') { Adv(); return {Tok::LBrace, "{", l, c}; }
    if (ch == '}') { Adv(); return {Tok::RBrace, "}", l, c}; }
    if (ch == '(') { Adv(); return {Tok::LParen, "(", l, c}; }
    if (ch == ')') { Adv(); return {Tok::RParen, ")", l, c}; }
    if (ch == '[') { Adv(); return {Tok::LBracket, "[", l, c}; }
    if (ch == ']') { Adv(); return {Tok::RBracket, "]", l, c}; }
    if (ch == ':') { Adv(); return {Tok::Colon, ":", l, c}; }
    if (ch == ',') { Adv(); return {Tok::Comma, ",", l, c}; }
    if (ch == ';') { Adv(); return {Tok::Semicolon, ";", l, c}; }
    if (ch == '+') { Adv(); return {Tok::Plus, "+", l, c}; }
    if (ch == '-') { Adv(); return {Tok::Minus, "-", l, c}; }
    if (ch == '*') { Adv(); return {Tok::Star, "*", l, c}; }
    if (ch == '/') { Adv(); return {Tok::Slash, "/", l, c}; }

    if (ch == '=' && Peek(1) == '=') { Adv(); Adv(); return {Tok::DoubleEquals, "==", l, c}; }
    if (ch == '=') { Adv(); return {Tok::Equals, "=", l, c}; }
    if (ch == '!' && Peek(1) == '=') { Adv(); Adv(); return {Tok::Ne, "!=", l, c}; }
    if (ch == '<' && Peek(1) == '=') { Adv(); Adv(); return {Tok::Le, "<=", l, c}; }
    if (ch == '<') { Adv(); return {Tok::Lt, "<", l, c}; }
    if (ch == '>' && Peek(1) == '=') { Adv(); Adv(); return {Tok::Ge, ">=", l, c}; }
    if (ch == '>') { Adv(); return {Tok::Gt, ">", l, c}; }
    if (ch == '.' && Peek(1) == '.') { Adv(); Adv(); return {Tok::DotDot, "..", l, c}; }

    if (ch == '\'' || ch == '"') return ReadStr(ch);
    if (std::isdigit(ch)) return ReadNum();
    if (std::isalpha(ch) || ch == '_') return ReadIdent();

    Adv();
    return {Tok::Symbol, std::string(1, ch), l, c};
  }

  Token Peek() {
    size_t p = pos_; int li = line_, co = col_;
    Token t = Next();
    pos_ = p; line_ = li; col_ = co;
    return t;
  }

private:
  void Adv() {
    if (pos_ < src_.size()) {
      if (src_[pos_] == '\n') { ++line_; col_ = 1; }
      else { ++col_; }
      ++pos_;
    }
  }

  char Peek(int offset) {
    return (pos_ + offset < src_.size()) ? src_[pos_ + offset] : '\0';
  }

  void SkipWS() {
    while (pos_ < src_.size()) {
      char c = src_[pos_];
      if (c == ' ' || c == '\t' || c == '\r') { Adv(); }
      else if (c == '#') { while (pos_ < src_.size() && src_[pos_] != '\n') Adv(); }
      else break;
    }
  }

  Token ReadStr(char q) {
    int l = line_, c = col_;
    Adv();
    std::string v;
    while (pos_ < src_.size() && src_[pos_] != q) {
      if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
        Adv();
        char e = src_[pos_];
        if (e == 'n') v += '\n';
        else if (e == 't') v += '\t';
        else v += e;
      } else {
        v += src_[pos_];
      }
      Adv();
    }
    if (pos_ < src_.size()) Adv();
    return {Tok::String, v, l, c};
  }

  Token ReadNum() {
    int l = line_, c = col_;
    std::string v;
    while (pos_ < src_.size() && std::isdigit(src_[pos_])) {
      v += src_[pos_]; Adv();
    }
    return {Tok::Number, v, l, c};
  }

  Token ReadIdent() {
    int l = line_, c = col_;
    std::string v;
    while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_')) {
      v += src_[pos_]; Adv();
    }
    return {Tok::Ident, v, l, c};
  }

  const std::string& src_;
  size_t pos_;
  int line_, col_;
};

class Parser {
public:
  explicit Parser(const std::string& src) : lex_(src) {}

  Program ParseProgram() {
    Program prog;
    while (true) {
      auto t = lex_.Peek();
      if (t.type == Lexer::Tok::Eof) break;
      if (t.type == Lexer::Tok::Newline) { lex_.Next(); continue; }

      if (t.type == Lexer::Tok::Ident && t.text == "alphabet") {
        ParseAlphabet(prog);
      } else if (t.type == Lexer::Tok::Ident && t.text == "markers") {
        ParseMarkers(prog);
      } else {
        prog.body.push_back(ParseStmt());
      }
    }
    return prog;
  }

  // Legacy IR parser
  IRProgram ParseIRProgram() {
    IRProgram prog;
    while (true) {
      auto t = lex_.Peek();
      if (t.type == Lexer::Tok::Eof) break;
      if (t.type == Lexer::Tok::Newline) { lex_.Next(); continue; }

      if (t.type == Lexer::Tok::Ident && t.text == "alphabet") {
        ParseAlphabetIR(prog);
      } else {
        prog.body.push_back(ParseIRStmt());
      }
    }
    return prog;
  }

private:
  void ParseAlphabet(Program& prog) {
    Expect(Lexer::Tok::Ident, "alphabet");
    auto kind = lex_.Next();
    Expect(Lexer::Tok::Colon);
    Expect(Lexer::Tok::LBracket);
    while (lex_.Peek().type != Lexer::Tok::RBracket) {
      auto t = lex_.Next();
      if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Symbol) {
        prog.input_alphabet.insert(t.text[0]);
      }
      if (lex_.Peek().type == Lexer::Tok::Comma) lex_.Next();
    }
    Expect(Lexer::Tok::RBracket);
  }

  void ParseMarkers(Program& prog) {
    Expect(Lexer::Tok::Ident, "markers");
    Expect(Lexer::Tok::Colon);
    Expect(Lexer::Tok::LBracket);
    while (lex_.Peek().type != Lexer::Tok::RBracket) {
      auto t = lex_.Next();
      if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Symbol) {
        prog.markers.insert(t.text[0]);
      }
      if (lex_.Peek().type == Lexer::Tok::Comma) lex_.Next();
    }
    Expect(Lexer::Tok::RBracket);
  }

  void ParseAlphabetIR(IRProgram& prog) {
    Expect(Lexer::Tok::Ident, "alphabet");
    auto kind = lex_.Next();
    Expect(Lexer::Tok::Colon);
    Expect(Lexer::Tok::LBracket);
    while (lex_.Peek().type != Lexer::Tok::RBracket) {
      auto t = lex_.Next();
      if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Symbol) {
        if (kind.text == "input") prog.input_alphabet.insert(t.text[0]);
        else prog.tape_alphabet_extra.insert(t.text[0]);
      }
      if (lex_.Peek().type == Lexer::Tok::Comma) lex_.Next();
    }
    Expect(Lexer::Tok::RBracket);
  }

  StmtPtr ParseStmt() {
    auto t = lex_.Peek();

    if (t.type == Lexer::Tok::Ident) {
      if (t.text == "return") {
        lex_.Next();
        auto expr = ParseExpr();
        return std::make_shared<ReturnStmt>(expr);
      }
      if (t.text == "accept") {
        lex_.Next();
        return std::make_shared<AcceptStmt>();
      }
      if (t.text == "reject") {
        lex_.Next();
        return std::make_shared<RejectStmt>();
      }
      if (t.text == "for") {
        return ParseFor();
      }
      if (t.text == "if") {
        return ParseIf();
      }
      if (t.text == "loop") {
        return ParseLoop();
      }
      if (t.text == "scan") {
        return ParseScan();
      }
      if (t.text == "write") {
        lex_.Next();
        auto sym = lex_.Next();
        return std::make_shared<WriteStmt>(sym.text[0]);
      }
      if (t.text == "left" || t.text == "L") {
        lex_.Next();
        return std::make_shared<MoveStmt>(Dir::L);
      }
      if (t.text == "right" || t.text == "R") {
        lex_.Next();
        return std::make_shared<MoveStmt>(Dir::R);
      }
      if (t.text == "inc") {
        lex_.Next();
        auto reg = lex_.Next().text;
        return std::make_shared<IncStmt>(reg);
      }
      if (t.text == "append") {
        lex_.Next();
        auto src = lex_.Next().text;
        Expect(Lexer::Tok::Minus);  // ->
        Expect(Lexer::Tok::Gt);
        auto dst = lex_.Next().text;
        return std::make_shared<AppendStmt>(src, dst);
      }
      if (t.text == "break") {
        lex_.Next();
        return std::make_shared<BreakStmt>();
      }

      // Variable declaration or assignment
      std::string name = lex_.Next().text;
      Expect(Lexer::Tok::Equals);
      auto expr = ParseExpr();

      // Check if variable exists - if not, it's a let
      // For simplicity, always treat first occurrence as let
      return std::make_shared<LetStmt>(name, expr);
    }

    throw std::runtime_error("Unexpected token: " + t.text);
  }

  StmtPtr ParseFor() {
    Expect(Lexer::Tok::Ident, "for");
    auto var = lex_.Next().text;
    Expect(Lexer::Tok::Ident, "in");
    auto start = ParseExpr();
    Expect(Lexer::Tok::DotDot);
    auto end = ParseExpr();
    Expect(Lexer::Tok::LBrace);

    auto stmt = std::make_shared<ForStmt>();
    stmt->var = var;
    stmt->start = start;
    stmt->end = end;
    stmt->body = ParseBlock();

    return stmt;
  }

  StmtPtr ParseIf() {
    Expect(Lexer::Tok::Ident, "if");

    // Check if this is a symbol-based if (checking current tape cell)
    auto t = lex_.Peek();
    bool is_symbol_if = false;

    // Symbol-based if: single identifier/symbol before {
    if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Symbol) {
      // Peek ahead to see if next is LBrace
      lex_.Next();  // consume the potential symbol
      auto next = lex_.Peek();
      if (next.type == Lexer::Tok::LBrace) {
        is_symbol_if = true;
        // Parse as IfCurrentStmt
        auto stmt = std::make_shared<IfCurrentStmt>();
        Symbol sym = (t.text == "_") ? kBlank : t.text[0];

        Expect(Lexer::Tok::LBrace);
        stmt->branches[sym] = ParseBlock();

        // Handle else if / else
        while (lex_.Peek().type == Lexer::Tok::Ident && lex_.Peek().text == "else") {
          lex_.Next();
          auto peek = lex_.Peek();
          if (peek.type == Lexer::Tok::Ident && peek.text == "if") {
            // else if symbol { ... }
            lex_.Next();
            auto sym_tok = lex_.Next();
            Symbol s = (sym_tok.text == "_") ? kBlank : sym_tok.text[0];
            Expect(Lexer::Tok::LBrace);
            stmt->branches[s] = ParseBlock();
          } else {
            // else { ... }
            Expect(Lexer::Tok::LBrace);
            stmt->else_body = ParseBlock();
            break;
          }
        }

        return stmt;
      }
      // Not a symbol-based if, need to put back the token (conceptually)
      // We'll parse it as an expression starting with this identifier
    }

    // Expression-based if (original behavior)
    // Note: t was already consumed if we fell through from symbol check
    // We need to construct the expression properly
    ExprPtr cond;
    if (is_symbol_if) {
      // Should not reach here
      throw std::runtime_error("Parser error in if");
    } else {
      // The token t was consumed but wasn't a symbol-if
      // Need to construct expression starting with it
      if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Number) {
        // Build expression from t
        ExprPtr left;
        if (t.type == Lexer::Tok::Number) {
          left = std::make_shared<IntLit>(std::stoi(t.text));
        } else if (t.text == "count") {
          Expect(Lexer::Tok::LParen);
          auto sym = lex_.Next();
          Expect(Lexer::Tok::RParen);
          left = std::make_shared<Count>(sym.text[0]);
        } else {
          left = std::make_shared<Var>(t.text);
        }

        // Now continue parsing comparison/binary ops
        auto next = lex_.Peek();
        if (next.type == Lexer::Tok::DoubleEquals) {
          lex_.Next();
          auto right = ParseAddSub();
          // If both sides are simple vars, produce IfEqStmt (VM instruction)
          auto* lv = dynamic_cast<Var*>(left.get());
          auto* rv = dynamic_cast<Var*>(right.get());
          if (lv && rv) {
            Expect(Lexer::Tok::LBrace);
            auto stmt = std::make_shared<IfEqStmt>();
            stmt->reg_a = lv->name;
            stmt->reg_b = rv->name;
            stmt->then_body = ParseBlock();
            if (lex_.Peek().type == Lexer::Tok::Ident && lex_.Peek().text == "else") {
              lex_.Next();
              Expect(Lexer::Tok::LBrace);
              stmt->else_body = ParseBlock();
            }
            return stmt;
          }
          cond = std::make_shared<BinExpr>(BinOp::Eq, left, right);
        } else if (next.type == Lexer::Tok::Ne) {
          lex_.Next();
          auto right = ParseAddSub();
          cond = std::make_shared<BinExpr>(BinOp::Ne, left, right);
        } else if (next.type == Lexer::Tok::Lt) {
          lex_.Next();
          auto right = ParseAddSub();
          cond = std::make_shared<BinExpr>(BinOp::Lt, left, right);
        } else if (next.type == Lexer::Tok::Le) {
          lex_.Next();
          auto right = ParseAddSub();
          cond = std::make_shared<BinExpr>(BinOp::Le, left, right);
        } else if (next.type == Lexer::Tok::Gt) {
          lex_.Next();
          auto right = ParseAddSub();
          cond = std::make_shared<BinExpr>(BinOp::Gt, left, right);
        } else if (next.type == Lexer::Tok::Ge) {
          lex_.Next();
          auto right = ParseAddSub();
          cond = std::make_shared<BinExpr>(BinOp::Ge, left, right);
        } else {
          cond = left;  // Just a var/literal as condition
        }
      } else {
        throw std::runtime_error("Unexpected token in if condition");
      }
    }

    Expect(Lexer::Tok::LBrace);

    auto stmt = std::make_shared<IfStmt>();
    stmt->condition = cond;
    stmt->then_body = ParseBlock();

    if (lex_.Peek().type == Lexer::Tok::Ident && lex_.Peek().text == "else") {
      lex_.Next();
      Expect(Lexer::Tok::LBrace);
      stmt->else_body = ParseBlock();
    }

    return stmt;
  }

  StmtPtr ParseLoop() {
    Expect(Lexer::Tok::Ident, "loop");
    Expect(Lexer::Tok::LBrace);

    auto stmt = std::make_shared<LoopStmt>();
    stmt->body = ParseBlock();
    return stmt;
  }

  StmtPtr ParseScan() {
    Expect(Lexer::Tok::Ident, "scan");
    auto dir_tok = lex_.Next();
    Dir dir = (dir_tok.text == "left" || dir_tok.text == "L") ? Dir::L : Dir::R;
    Expect(Lexer::Tok::Ident, "for");

    auto stmt = std::make_shared<ScanStmt>();
    stmt->direction = dir;

    // Parse symbol list: [a, b, _] or single symbol
    if (lex_.Peek().type == Lexer::Tok::LBracket) {
      lex_.Next();
      while (lex_.Peek().type != Lexer::Tok::RBracket) {
        auto t = lex_.Next();
        if (t.type == Lexer::Tok::Ident || t.type == Lexer::Tok::Symbol) {
          Symbol s = (t.text == "_") ? kBlank : t.text[0];
          stmt->stop_symbols.insert(s);
        }
        if (lex_.Peek().type == Lexer::Tok::Comma) lex_.Next();
      }
      Expect(Lexer::Tok::RBracket);
    } else {
      auto t = lex_.Next();
      Symbol s = (t.text == "_") ? kBlank : t.text[0];
      stmt->stop_symbols.insert(s);
    }

    return stmt;
  }

  std::vector<StmtPtr> ParseBlock() {
    std::vector<StmtPtr> body;
    while (true) {
      auto t = lex_.Peek();
      if (t.type == Lexer::Tok::RBrace) { lex_.Next(); break; }
      if (t.type == Lexer::Tok::Newline) { lex_.Next(); continue; }
      if (t.type == Lexer::Tok::Eof) throw std::runtime_error("Unexpected EOF in block");
      body.push_back(ParseStmt());
    }
    return body;
  }

  ExprPtr ParseExpr() {
    return ParseComparison();
  }

  ExprPtr ParseComparison() {
    auto left = ParseAddSub();
    auto t = lex_.Peek();
    if (t.type == Lexer::Tok::DoubleEquals) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Eq, left, right);
    }
    if (t.type == Lexer::Tok::Ne) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Ne, left, right);
    }
    if (t.type == Lexer::Tok::Lt) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Lt, left, right);
    }
    if (t.type == Lexer::Tok::Le) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Le, left, right);
    }
    if (t.type == Lexer::Tok::Gt) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Gt, left, right);
    }
    if (t.type == Lexer::Tok::Ge) {
      lex_.Next();
      auto right = ParseAddSub();
      return std::make_shared<BinExpr>(BinOp::Ge, left, right);
    }
    return left;
  }

  ExprPtr ParseAddSub() {
    auto left = ParsePrimary();
    while (true) {
      auto t = lex_.Peek();
      if (t.type == Lexer::Tok::Plus) {
        lex_.Next();
        auto right = ParsePrimary();
        left = std::make_shared<BinExpr>(BinOp::Add, left, right);
      } else if (t.type == Lexer::Tok::Minus) {
        lex_.Next();
        auto right = ParsePrimary();
        left = std::make_shared<BinExpr>(BinOp::Sub, left, right);
      } else {
        break;
      }
    }
    return left;
  }

  ExprPtr ParsePrimary() {
    auto t = lex_.Peek();

    if (t.type == Lexer::Tok::Number) {
      lex_.Next();
      return std::make_shared<IntLit>(std::stoi(t.text));
    }

    if (t.type == Lexer::Tok::Ident) {
      lex_.Next();
      if (t.text == "count") {
        Expect(Lexer::Tok::LParen);
        auto sym = lex_.Next();
        Expect(Lexer::Tok::RParen);
        return std::make_shared<Count>(sym.text[0]);
      }
      return std::make_shared<Var>(t.text);
    }

    if (t.type == Lexer::Tok::LParen) {
      lex_.Next();
      auto expr = ParseExpr();
      Expect(Lexer::Tok::RParen);
      return expr;
    }

    throw std::runtime_error("Unexpected token in expression: " + t.text);
  }

  IRNodePtr ParseIRStmt() {
    auto t = lex_.Peek();

    if (t.type == Lexer::Tok::Ident) {
      if (t.text == "scan") {
        lex_.Next();
        auto dir_tok = lex_.Next();
        Dir dir = (dir_tok.text == "left" || dir_tok.text == "L") ? Dir::L : Dir::R;
        Expect(Lexer::Tok::Ident, "until");

        auto scan = std::make_shared<ScanUntil>();
        scan->direction = dir;

        auto sym = lex_.Next();
        scan->stop_symbols.insert(sym.text[0]);
        return scan;
      }
      if (t.text == "write") {
        lex_.Next();
        auto sym = lex_.Next();
        auto w = std::make_shared<WriteSymbol>();
        w->symbol = sym.text[0];
        return w;
      }
      if (t.text == "left" || t.text == "L") {
        lex_.Next();
        auto m = std::make_shared<Move>();
        m->direction = Dir::L;
        return m;
      }
      if (t.text == "right" || t.text == "R") {
        lex_.Next();
        auto m = std::make_shared<Move>();
        m->direction = Dir::R;
        return m;
      }
      if (t.text == "accept") {
        lex_.Next();
        return std::make_shared<Accept>();
      }
      if (t.text == "reject") {
        lex_.Next();
        return std::make_shared<Reject>();
      }
    }

    throw std::runtime_error("Unknown IR statement: " + t.text);
  }

  void Expect(Lexer::Tok type, const std::string& text = "") {
    auto t = lex_.Next();
    if (t.type != type) {
      throw std::runtime_error("Expected token at line " + std::to_string(t.line));
    }
    if (!text.empty() && t.text != text) {
      throw std::runtime_error("Expected '" + text + "' at line " + std::to_string(t.line));
    }
  }

  Lexer lex_;
};

}  // namespace

IRProgram Parse(const std::string& source) {
  Parser parser(source);
  return parser.ParseIRProgram();
}

Program ParseHL(const std::string& source) {
  Parser parser(source);
  return parser.ParseProgram();
}

}  // namespace tmc
