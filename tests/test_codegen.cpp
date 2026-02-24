#include <gtest/gtest.h>
#include "tmc/ir.hpp"
#include "tmc/codegen.hpp"

namespace tmc {
namespace {

TEST(CodegenTest, ToYAMLBasic) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};
  tm.AddTransition("q0", 'a', 'A', Dir::R, "q1");
  tm.AddTransition("q0", 'b', 'b', Dir::S, "qR");
  tm.AddTransition("q1", kBlank, kBlank, Dir::S, "qA");
  tm.Finalize();

  std::string yaml = ToYAML(tm);

  EXPECT_FALSE(yaml.empty());
  EXPECT_NE(yaml.find("states:"), std::string::npos);
  EXPECT_NE(yaml.find("delta:"), std::string::npos);
  EXPECT_NE(yaml.find("start_state:"), std::string::npos);
  EXPECT_NE(yaml.find("accept_state:"), std::string::npos);
  EXPECT_NE(yaml.find("reject_state:"), std::string::npos);
}

TEST(CodegenTest, StateGen) {
  StateGen gen;
  EXPECT_EQ(gen.Next(), "q0");
  EXPECT_EQ(gen.Next(), "q1");
  EXPECT_EQ(gen.Next("s"), "s2");
  gen.Reset();
  EXPECT_EQ(gen.Next(), "q0");
}

TEST(CodegenTest, CompileScanUntil) {
  IRProgram program;
  program.input_alphabet = {'a', 'b'};

  auto scan = std::make_shared<ScanUntil>();
  scan->direction = Dir::R;
  scan->stop_symbols = {kBlank};
  program.body.push_back(scan);

  TM tm = CompileIR(program);
  tm.Finalize();

  std::string error;
  EXPECT_TRUE(tm.Validate(&error)) << error;
}

TEST(CodegenTest, CompileSequence) {
  IRProgram program;
  program.input_alphabet = {'a'};
  program.tape_alphabet_extra = {'A'};

  // Write A, move right
  auto write = std::make_shared<WriteSymbol>();
  write->symbol = 'A';
  program.body.push_back(write);

  auto move = std::make_shared<Move>();
  move->direction = Dir::R;
  program.body.push_back(move);

  program.body.push_back(std::make_shared<Accept>());

  TM tm = CompileIR(program);
  tm.Finalize();

  std::string error;
  EXPECT_TRUE(tm.Validate(&error)) << error;
}

}  // namespace
}  // namespace tmc
