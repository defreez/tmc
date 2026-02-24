#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/codegen.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/optimizer.hpp"
#include "tmc/simulator.hpp"

#include <iostream>
#include <fstream>
#include <sstream>

void PrintUsage(const char* prog) {
  std::cerr << "TMC - Turing Machine Compiler\n\n";
  std::cerr << "Usage: " << prog << " [options] <source.tmc>\n";
  std::cerr << "\nOptions:\n";
  std::cerr << "  -o <file>     Output YAML file (default: stdout)\n";
  std::cerr << "  -t <string>   Test input string after compilation\n";
  std::cerr << "  -v            Verbose output\n";
  std::cerr << "  --no-opt      Disable optimizations\n";
  std::cerr << "  --precompute <n>  Precompute results for inputs up to length n\n";
  std::cerr << "  --max-states <n>  Maximum states to generate\n";
  std::cerr << "  --max-symbols <n> Maximum tape alphabet size\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string test_input;
  bool verbose = false;
  bool optimize = true;
  int precompute_len = 0;
  int max_states = 0;
  int max_symbols = 0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-t" && i + 1 < argc) {
      test_input = argv[++i];
    } else if (arg == "-v") {
      verbose = true;
    } else if (arg == "--no-opt") {
      optimize = false;
    } else if (arg == "--precompute" && i + 1 < argc) {
      precompute_len = std::stoi(argv[++i]);
    } else if (arg == "--max-states" && i + 1 < argc) {
      max_states = std::stoi(argv[++i]);
    } else if (arg == "--max-symbols" && i + 1 < argc) {
      max_symbols = std::stoi(argv[++i]);
    } else if (arg[0] != '-') {
      input_file = arg;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (input_file.empty()) {
    std::cerr << "Error: No input file specified\n";
    PrintUsage(argv[0]);
    return 1;
  }

  // Read input file
  std::ifstream ifs(input_file);
  if (!ifs) {
    std::cerr << "Error: Cannot open input file: " << input_file << "\n";
    return 1;
  }
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  std::string source = buffer.str();

  try {
    // Detect DSL type: high-level uses "alphabet input:", low-level uses "states:"
    bool high_level = source.find("alphabet input:") != std::string::npos;

    if (verbose) std::cerr << "Parsing " << input_file
                           << " (" << (high_level ? "high-level" : "low-level IR") << ")...\n";

    tmc::TM tm = [&]() {
      if (high_level) {
        tmc::Program program = tmc::ParseHL(source);
        if (verbose) std::cerr << "Compiling to TM...\n";
        return tmc::CompileProgram(program);
      } else {
        tmc::IRProgram program = tmc::Parse(source);
        if (verbose) std::cerr << "Compiling to TM...\n";
        return tmc::CompileIR(program);
      }
    }();

    // Optimize
    if (optimize) {
      if (verbose) std::cerr << "Optimizing...\n";
      tmc::OptConfig config;
      config.max_states = max_states;
      config.max_tape_symbols = max_symbols;
      config.precompute_max_input_len = precompute_len;
      tmc::Optimize(tm, config);
    }

    // Validate
    std::string error;
    if (!tm.Validate(&error)) {
      std::cerr << "Error: Invalid TM: " << error << "\n";
      return 1;
    }

    // Output YAML
    std::string yaml = tmc::ToYAML(tm);

    if (output_file.empty()) {
      std::cout << yaml;
    } else {
      std::ofstream ofs(output_file);
      if (!ofs) {
        std::cerr << "Error: Cannot open output file: " << output_file << "\n";
        return 1;
      }
      ofs << yaml;
      if (verbose) std::cerr << "Wrote " << output_file << "\n";
    }

    // Test if requested
    if (!test_input.empty()) {
      if (verbose) std::cerr << "Testing on input: \"" << test_input << "\"\n";
      tmc::Simulator sim(tm);
      tmc::RunResult result = sim.Run(test_input);

      std::cout << "Input: \"" << test_input << "\"\n";
      std::cout << "Result: " << (result.accepted ? "ACCEPT" : "REJECT") << "\n";
      std::cout << "Steps: " << result.steps << "\n";
      if (!result.final_tape.empty()) {
        std::cout << "Final tape: " << result.final_tape << "\n";
      }
      if (result.hit_limit) {
        std::cout << "WARNING: Hit step limit\n";
      }
    }

    // Print stats
    if (verbose) {
      std::cerr << "Stats:\n";
      std::cerr << "  States: " << tm.states.size() << "\n";
      std::cerr << "  Tape alphabet: " << tm.tape_alphabet.size() << "\n";
      int transitions = 0;
      for (const auto& [state, trans_map] : tm.delta) {
        transitions += trans_map.size();
      }
      std::cerr << "  Transitions: " << transitions << "\n";
    }

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
