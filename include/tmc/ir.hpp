#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace tmc {

//=============================================================================
// LOW-LEVEL: TM Representation
//=============================================================================

enum class Dir { L, R, S };

using Symbol = char;
constexpr Symbol kBlank = '_';
constexpr Symbol kWildcard = '?';

using State = std::string;

struct Transition {
  Symbol read;
  Symbol write;
  Dir dir;
  State next;

  bool operator==(const Transition& other) const {
    return read == other.read && write == other.write &&
           dir == other.dir && next == other.next;
  }
};

using TransitionMap = std::map<Symbol, Transition>;

struct TM {
  std::set<State> states;
  std::set<Symbol> input_alphabet;
  std::set<Symbol> tape_alphabet;
  State start;
  State accept;
  State reject;
  std::map<State, TransitionMap> delta;

  void AddTransition(const State& from, Symbol read, Symbol write, Dir dir, const State& to);
  void Finalize();
  bool Validate(std::string* error = nullptr) const;
};

//=============================================================================
// HIGH-LEVEL DSL: Expressions
//=============================================================================

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
  virtual ~Expr() = default;
  virtual std::string kind() const = 0;
};

// Integer literal: 0, 1, 42
struct IntLit : Expr {
  int value;
  explicit IntLit(int v) : value(v) {}
  std::string kind() const override { return "IntLit"; }
};

// Variable reference: n, sum, i
struct Var : Expr {
  std::string name;
  explicit Var(const std::string& n) : name(n) {}
  std::string kind() const override { return "Var"; }
};

// Count occurrences: count(a), count(b)
struct Count : Expr {
  Symbol symbol;
  explicit Count(Symbol s) : symbol(s) {}
  std::string kind() const override { return "Count"; }
};

// Binary operations
enum class BinOp { Add, Sub, Eq, Ne, Lt, Le, Gt, Ge };

struct BinExpr : Expr {
  BinOp op;
  ExprPtr left;
  ExprPtr right;
  BinExpr(BinOp o, ExprPtr l, ExprPtr r) : op(o), left(std::move(l)), right(std::move(r)) {}
  std::string kind() const override { return "BinExpr"; }
};

// Helper constructors
inline ExprPtr make_int(int v) { return std::make_shared<IntLit>(v); }
inline ExprPtr make_var(const std::string& n) { return std::make_shared<Var>(n); }
inline ExprPtr make_count(Symbol s) { return std::make_shared<Count>(s); }
inline ExprPtr make_add(ExprPtr l, ExprPtr r) { return std::make_shared<BinExpr>(BinOp::Add, l, r); }
inline ExprPtr make_sub(ExprPtr l, ExprPtr r) { return std::make_shared<BinExpr>(BinOp::Sub, l, r); }
inline ExprPtr make_eq(ExprPtr l, ExprPtr r) { return std::make_shared<BinExpr>(BinOp::Eq, l, r); }
inline ExprPtr make_lt(ExprPtr l, ExprPtr r) { return std::make_shared<BinExpr>(BinOp::Lt, l, r); }
inline ExprPtr make_le(ExprPtr l, ExprPtr r) { return std::make_shared<BinExpr>(BinOp::Le, l, r); }

//=============================================================================
// HIGH-LEVEL DSL: Statements
//=============================================================================

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::string kind() const = 0;
};

// Variable declaration: let n = count(a)
struct LetStmt : Stmt {
  std::string name;
  ExprPtr init;
  LetStmt(const std::string& n, ExprPtr e) : name(n), init(std::move(e)) {}
  std::string kind() const override { return "LetStmt"; }
};

// Assignment: sum = sum + i
struct AssignStmt : Stmt {
  std::string name;
  ExprPtr value;
  AssignStmt(const std::string& n, ExprPtr e) : name(n), value(std::move(e)) {}
  std::string kind() const override { return "AssignStmt"; }
};

// For loop: for i in 1..n { body }
struct ForStmt : Stmt {
  std::string var;      // loop variable name
  ExprPtr start;        // start value (inclusive)
  ExprPtr end;          // end value (inclusive)
  std::vector<StmtPtr> body;
  std::string kind() const override { return "ForStmt"; }
};

// If statement: if condition { body } else { else_body }
struct IfStmt : Stmt {
  ExprPtr condition;
  std::vector<StmtPtr> then_body;
  std::vector<StmtPtr> else_body;
  std::string kind() const override { return "IfStmt"; }
};

// Return: return expr (accept if true, reject if false)
struct ReturnStmt : Stmt {
  ExprPtr value;
  explicit ReturnStmt(ExprPtr e) : value(std::move(e)) {}
  std::string kind() const override { return "ReturnStmt"; }
};

// Accept unconditionally
struct AcceptStmt : Stmt {
  std::string kind() const override { return "AcceptStmt"; }
};

// Reject unconditionally
struct RejectStmt : Stmt {
  std::string kind() const override { return "RejectStmt"; }
};

// Match regex pattern against input: match a*b*
struct MatchStmt : Stmt {
  std::string pattern;  // e.g., "a*b*"
  explicit MatchStmt(const std::string& p) : pattern(p) {}
  std::string kind() const override { return "MatchStmt"; }
};

//=============================================================================
// IMPERATIVE TAPE STATEMENTS
//=============================================================================

// Scan left/right until one of the stop symbols: scan right for [a, b, _]
struct ScanStmt : Stmt {
  Dir direction;
  std::set<Symbol> stop_symbols;
  std::string kind() const override { return "ScanStmt"; }
};

// Write a symbol at current position: write A
struct WriteStmt : Stmt {
  Symbol symbol;
  explicit WriteStmt(Symbol s) : symbol(s) {}
  std::string kind() const override { return "WriteStmt"; }
};

// Move head: left, right
struct MoveStmt : Stmt {
  Dir direction;
  explicit MoveStmt(Dir d) : direction(d) {}
  std::string kind() const override { return "MoveStmt"; }
};

// Infinite loop: loop { body } - exit via accept/reject
struct LoopStmt : Stmt {
  std::vector<StmtPtr> body;
  std::string kind() const override { return "LoopStmt"; }
};

// Check current symbol: if a { ... } else if b { ... } else { ... }
struct IfCurrentStmt : Stmt {
  std::map<Symbol, std::vector<StmtPtr>> branches;  // symbol -> body
  std::vector<StmtPtr> else_body;
  std::string kind() const override { return "IfCurrentStmt"; }
};

//=============================================================================
// PROGRAM
//=============================================================================

struct Program {
  std::set<Symbol> input_alphabet;
  std::set<Symbol> markers;  // extra tape symbols
  std::vector<StmtPtr> body;
};

//=============================================================================
// LEGACY: Low-level IR (for direct tape manipulation)
//=============================================================================

struct IRNode;
using IRNodePtr = std::shared_ptr<IRNode>;

struct IRNode {
  virtual ~IRNode() = default;
  virtual std::string Kind() const = 0;
};

struct ScanUntil : IRNode {
  Dir direction;
  std::set<Symbol> stop_symbols;
  std::string Kind() const override { return "ScanUntil"; }
};

struct WriteSymbol : IRNode {
  Symbol symbol;
  std::string Kind() const override { return "WriteSymbol"; }
};

struct Move : IRNode {
  Dir direction;
  int count = 1;
  std::string Kind() const override { return "Move"; }
};

struct IfSymbol : IRNode {
  std::map<Symbol, std::vector<IRNodePtr>> branches;
  std::vector<IRNodePtr> else_branch;
  std::string Kind() const override { return "IfSymbol"; }
};

struct WhileSymbol : IRNode {
  std::set<Symbol> continue_symbols;
  std::vector<IRNodePtr> body;
  std::string Kind() const override { return "WhileSymbol"; }
};

struct Accept : IRNode {
  std::string Kind() const override { return "Accept"; }
};

struct Reject : IRNode {
  std::string Kind() const override { return "Reject"; }
};

struct Mark : IRNode {
  std::map<Symbol, Symbol> mark_map;
  std::string Kind() const override { return "Mark"; }
};

struct Goto : IRNode {
  std::string label;
  std::string Kind() const override { return "Goto"; }
};

struct Block : IRNode {
  std::string label;
  std::vector<IRNodePtr> body;
  std::string Kind() const override { return "Block"; }
};

struct IRProgram {
  std::set<Symbol> input_alphabet;
  std::set<Symbol> tape_alphabet_extra;
  std::vector<IRNodePtr> body;
  std::map<std::string, std::shared_ptr<Block>> blocks;
};

}  // namespace tmc
