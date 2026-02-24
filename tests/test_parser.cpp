#include <gtest/gtest.h>
#include "tmc/parser.hpp"

namespace tmc {
namespace {

// Tests for high-level DSL parser

TEST(ParserTest, ParseAlphabet) {
  std::string source = R"(
alphabet input: [a, b]
)";

  Program program = ParseHL(source);

  EXPECT_TRUE(program.input_alphabet.count('a'));
  EXPECT_TRUE(program.input_alphabet.count('b'));
}

TEST(ParserTest, ParseLet) {
  std::string source = R"(
alphabet input: [a, b]
n = count(a)
)";

  Program program = ParseHL(source);

  EXPECT_EQ(program.body.size(), 1);
  EXPECT_EQ(program.body[0]->kind(), "LetStmt");
}

TEST(ParserTest, ParseReturn) {
  std::string source = R"(
alphabet input: [a, b]
n = count(a)
return count(b) == n
)";

  Program program = ParseHL(source);

  EXPECT_EQ(program.body.size(), 2);
  EXPECT_EQ(program.body[1]->kind(), "ReturnStmt");
}

TEST(ParserTest, ParseFor) {
  std::string source = R"(
alphabet input: [a, b]
n = count(a)
for i in 1..n {
  accept
}
)";

  Program program = ParseHL(source);

  EXPECT_EQ(program.body.size(), 2);  // n = ..., for
  EXPECT_EQ(program.body[1]->kind(), "ForStmt");
}

TEST(ParserTest, ParseArithmetic) {
  std::string source = R"(
alphabet input: [a]
n = count(a)
m = n + 1
)";

  Program program = ParseHL(source);

  EXPECT_EQ(program.body.size(), 2);
}

TEST(ParserTest, ParseComparison) {
  std::string source = R"(
alphabet input: [a, b]
return count(a) == count(b)
)";

  Program program = ParseHL(source);

  EXPECT_EQ(program.body.size(), 1);
  auto* ret = dynamic_cast<ReturnStmt*>(program.body[0].get());
  ASSERT_NE(ret, nullptr);
  auto* cmp = dynamic_cast<BinExpr*>(ret->value.get());
  ASSERT_NE(cmp, nullptr);
  EXPECT_EQ(cmp->op, BinOp::Eq);
}

TEST(ParserTest, ParseTriangular) {
  std::string source = R"(
alphabet input: [a, b]

n = count(a)
sum = 0
for i in 1..n {
  sum = sum + i
}
return count(b) == sum
)";

  Program program = ParseHL(source);

  EXPECT_TRUE(program.input_alphabet.count('a'));
  EXPECT_TRUE(program.input_alphabet.count('b'));
  EXPECT_EQ(program.body.size(), 4);  // n=, sum=, for, return
}

// Tests for legacy low-level IR parser

TEST(IRParserTest, ParseScan) {
  std::string source = R"(
alphabet input: [a, b]
scan right until _
)";

  IRProgram program = Parse(source);

  EXPECT_EQ(program.body.size(), 1);
  EXPECT_EQ(program.body[0]->Kind(), "ScanUntil");
}

TEST(IRParserTest, ParseWrite) {
  std::string source = R"(
alphabet input: [a]
alphabet tape: [X]
write X
)";

  IRProgram program = Parse(source);

  EXPECT_EQ(program.body.size(), 1);
  EXPECT_EQ(program.body[0]->Kind(), "WriteSymbol");
}

TEST(IRParserTest, ParseAcceptReject) {
  std::string source = R"(
alphabet input: [a]
accept
)";

  IRProgram program = Parse(source);

  EXPECT_EQ(program.body.size(), 1);
  EXPECT_EQ(program.body[0]->Kind(), "Accept");
}

}  // namespace
}  // namespace tmc
