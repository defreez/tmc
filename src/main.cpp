#include "tmc/ir.hpp"
#include "tmc/parser.hpp"
#include "tmc/codegen.hpp"
#include "tmc/hlcompiler.hpp"
#include "tmc/optimizer.hpp"
#include "tmc/simulator.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <chrono>

// Parse test suite: # comments, (empty) for empty string, one input per line
std::vector<std::string> ParseTestSuite(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    std::cerr << "Error: Cannot open test suite: " << path << "\n";
    return {};
  }
  std::vector<std::string> inputs;
  std::string line;
  while (std::getline(ifs, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line == "(empty)") {
      inputs.push_back("");
    } else {
      inputs.push_back(line);
    }
  }
  return inputs;
}

// Oracle for { a^n b^m | m = n*(n+1)/2 }
bool IsTriangular(const std::string& s) {
  int n = 0, m = 0;
  bool in_b = false;
  for (char c : s) {
    if (c == 'a') {
      if (in_b) return false;
      ++n;
    } else if (c == 'b') {
      in_b = true;
      ++m;
    } else {
      return false;
    }
  }
  return m == n * (n + 1) / 2;
}

// Extract student name from path: "/foo/bar/kai-fagundes.tm" -> "kai-fagundes"
std::string StudentName(const std::string& path) {
  size_t slash = path.rfind('/');
  std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
  size_t dot = base.rfind('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  return base;
}

void PrintUsage(const char* prog) {
  std::cerr << "TMC - Turing Machine Compiler\n\n";
  std::cerr << "Usage: " << prog << " [options] <source.tmc|source.tm>\n";
  std::cerr << "\nOptions:\n";
  std::cerr << "  -o <file>         Output YAML file (default: stdout)\n";
  std::cerr << "  -t <string>       Test input string after compilation\n";
  std::cerr << "  -v                Verbose output\n";
  std::cerr << "  --no-opt          Disable optimizations\n";
  std::cerr << "  --precompute <n>  Precompute results for inputs up to length n\n";
  std::cerr << "  --max-states <n>  Maximum states to generate\n";
  std::cerr << "  --max-symbols <n> Maximum tape alphabet size\n";
  std::cerr << "  --bench <file>    Benchmark against test suite file\n";
  std::cerr << "  --timeout <secs>  Wall clock timeout per test case (default: 60)\n";
  std::cerr << "  --csv <file>      Write CSV results (use with --bench)\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string input_file;
  std::string output_file;
  std::string test_input;
  std::string bench_file;
  std::string csv_file;
  bool verbose = false;
  bool optimize = true;
  int precompute_len = 0;
  int max_states = 0;
  int max_symbols = 0;
  double timeout_secs = 60.0;

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
    } else if (arg == "--bench" && i + 1 < argc) {
      bench_file = argv[++i];
    } else if (arg == "--timeout" && i + 1 < argc) {
      timeout_secs = std::stod(argv[++i]);
    } else if (arg == "--csv" && i + 1 < argc) {
      csv_file = argv[++i];
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
    // Detect if input is a pre-compiled .tm YAML file
    bool is_yaml = input_file.size() >= 3 &&
                   input_file.substr(input_file.size() - 3) == ".tm";

    tmc::TM tm;

    if (is_yaml) {
      if (verbose) std::cerr << "Loading YAML TM from " << input_file << "...\n";
      tm = tmc::FromYAML(source);
    } else {
      // Detect DSL type: high-level uses "alphabet input:", low-level uses "states:"
      bool high_level = source.find("alphabet input:") != std::string::npos;

      if (verbose) std::cerr << "Parsing " << input_file
                             << " (" << (high_level ? "high-level" : "low-level IR") << ")...\n";

      if (high_level) {
        tmc::Program program = tmc::ParseHL(source);
        if (verbose) std::cerr << "Compiling to TM...\n";
        tm = tmc::CompileProgram(program);
      } else {
        tmc::IRProgram program = tmc::Parse(source);
        if (verbose) std::cerr << "Compiling to TM...\n";
        tm = tmc::CompileIR(program);
      }

      // Optimize (only for compiled TMs, not pre-compiled YAML)
      if (optimize) {
        if (verbose) std::cerr << "Optimizing...\n";
        tmc::OptConfig config;
        config.max_states = max_states;
        config.max_tape_symbols = max_symbols;
        config.precompute_max_input_len = precompute_len;
        tmc::Optimize(tm, config);
      }
    }

    // Validate
    std::string error;
    if (!tm.Validate(&error)) {
      std::cerr << "Error: Invalid TM: " << error << "\n";
      return 1;
    }

    // Benchmark mode
    if (!bench_file.empty()) {
      auto inputs = ParseTestSuite(bench_file);
      if (inputs.empty()) {
        std::cerr << "Error: No test inputs loaded from " << bench_file << "\n";
        return 1;
      }

      int num_transitions = 0;
      for (const auto& [state, trans_map] : tm.delta) {
        num_transitions += static_cast<int>(trans_map.size());
      }
      int num_states = static_cast<int>(tm.states.size());

      std::cerr << "TM: " << num_states << " states, "
                << num_transitions << " transitions\n\n";

      tmc::Simulator sim(tm, 86000000000LL);
      using Clock = std::chrono::high_resolution_clock;

      std::string student = StudentName(input_file);
      int passed = 0, failed = 0;
      int64_t total_steps = 0;
      int64_t best_max_steps = 0;
      int max_steps_n = 0;
      int max_steps_len = 0;
      bool abort_remaining = false;

      auto bench_start = Clock::now();

      for (size_t i = 0; i < inputs.size(); ++i) {
        const std::string& input = inputs[i];
        bool expected = IsTriangular(input);

        int n = 0;
        for (char c : input) {
          if (c == 'a') ++n;
          else break;
        }

        bool timed_out = false;
        bool correct = false;
        tmc::RunResult result;
        double ms = 0;

        if (abort_remaining) {
          result.accepted = false;
          result.steps = 0;
          result.hit_limit = true;
          timed_out = true;
        } else {
          auto t0 = Clock::now();
          result = sim.Run(input);
          auto t1 = Clock::now();
          ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

          // Check wall clock timeout
          timed_out = (ms / 1000.0) >= timeout_secs;
          correct = (result.accepted == expected) && !result.hit_limit && !timed_out;

          // If hit step limit or timed out, abort all remaining cases
          if (result.hit_limit || timed_out) {
            abort_remaining = true;
          }
        }

        total_steps += result.steps;
        if (result.steps > best_max_steps) {
          best_max_steps = result.steps;
          max_steps_n = n;
          max_steps_len = static_cast<int>(input.size());
        }

        double case_rate = ms > 0 ? result.steps / (ms / 1000.0) : 0;
        double elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - bench_start).count();
        double cumul_rate = elapsed_ms > 0 ? total_steps / (elapsed_ms / 1000.0) : 0;

        std::cout << "[" << std::setw(2) << (i + 1) << "/" << inputs.size() << "] "
                  << "n=" << std::setw(2) << n
                  << " |w|=" << std::setw(4) << input.size()
                  << " " << (expected ? "ACC" : "REJ")
                  << (correct ? " " : " FAIL ")
                  << " steps=" << std::setw(8) << result.steps
                  << std::fixed << std::setprecision(1)
                  << "  " << std::setw(7) << ms << "ms"
                  << "  " << std::setprecision(1) << std::setw(5) << case_rate / 1e6 << "M st/s"
                  << "  cumul " << std::setw(5) << cumul_rate / 1e6 << "M st/s";
        if (result.hit_limit) std::cout << " HIT_LIMIT";
        if (timed_out) std::cout << " TIMEOUT";
        std::cout << "\n";

        if (correct) ++passed;
        else ++failed;
      }

      auto bench_end = Clock::now();
      double total_ms = std::chrono::duration<double, std::milli>(bench_end - bench_start).count();
      double avg_steps = static_cast<double>(total_steps) / inputs.size();
      double steps_per_sec = total_ms > 0 ? total_steps / (total_ms / 1000.0) : 0;

      std::cout << "\n=== Summary ===\n";
      std::cout << "Passed:  " << passed << "/" << inputs.size() << "\n";
      if (failed > 0) std::cout << "Failed:  " << failed << "\n";
      std::cout << "Total:   " << total_steps << " steps\n";
      std::cout << "Average: " << std::fixed << std::setprecision(1) << avg_steps << " steps\n";
      std::cout << "Max:     " << best_max_steps << " steps"
                << " (n=" << max_steps_n << ", |w|=" << max_steps_len << ")\n";
      std::cout << "Wall:    " << std::fixed << std::setprecision(1) << total_ms << "ms"
                << " (" << std::setprecision(0) << steps_per_sec / 1e6 << "M steps/sec)\n";

      // Write CSV if requested
      if (!csv_file.empty()) {
        // Append mode: if file doesn't exist, write header first
        bool write_header = true;
        {
          std::ifstream check(csv_file);
          if (check.good()) {
            // File exists, check if it has content
            check.seekg(0, std::ios::end);
            write_header = (check.tellg() == 0);
          }
        }

        std::ofstream csv(csv_file, std::ios::app);
        if (!csv) {
          std::cerr << "Error: Cannot open CSV file: " << csv_file << "\n";
          return 1;
        }
        if (write_header) {
          csv << "student,states,transitions,passed,failed,total_steps,max_steps\n";
        }
        csv << student << ","
            << num_states << ","
            << num_transitions << ","
            << passed << ","
            << failed << ","
            << total_steps << ","
            << best_max_steps << "\n";
      }

      return failed > 0 ? 1 : 0;
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
