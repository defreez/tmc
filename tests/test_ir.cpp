#include <gtest/gtest.h>
#include "tmc/ir.hpp"

namespace tmc {
namespace {

TEST(TMTest, AddTransition) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};

  tm.AddTransition("q0", 'a', 'A', Dir::R, "q1");
  tm.AddTransition("q1", 'b', 'B', Dir::L, "q0");

  EXPECT_EQ(tm.states.size(), 2);  // q0, q1 (accept/reject not auto-added)
  EXPECT_TRUE(tm.states.count("q0"));
  EXPECT_TRUE(tm.states.count("q1"));

  EXPECT_TRUE(tm.tape_alphabet.count('a'));
  EXPECT_TRUE(tm.tape_alphabet.count('A'));
  EXPECT_TRUE(tm.tape_alphabet.count('b'));
  EXPECT_TRUE(tm.tape_alphabet.count('B'));
}

TEST(TMTest, Finalize) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a', 'b'};

  tm.Finalize();

  EXPECT_TRUE(tm.tape_alphabet.count('a'));
  EXPECT_TRUE(tm.tape_alphabet.count('b'));
  EXPECT_TRUE(tm.tape_alphabet.count(kBlank));
  EXPECT_TRUE(tm.states.count("q0"));
  EXPECT_TRUE(tm.states.count("qA"));
  EXPECT_TRUE(tm.states.count("qR"));
}

TEST(TMTest, Validate) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  tm.input_alphabet = {'a'};
  tm.Finalize();

  std::string error;
  EXPECT_TRUE(tm.Validate(&error)) << error;
}

TEST(TMTest, ValidateFails) {
  TM tm;
  tm.start = "q0";
  tm.accept = "qA";
  tm.reject = "qR";
  // Don't finalize - states won't be registered

  std::string error;
  EXPECT_FALSE(tm.Validate(&error));
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace tmc
