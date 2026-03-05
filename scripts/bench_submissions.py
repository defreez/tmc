#!/usr/bin/env python3
"""Run each TM submission against the HW3A test suite.

tmc writes the CSV results directly via --csv. This script launches
all submissions in parallel and collects results.
"""

import argparse
import glob
import os
import subprocess
import sys
import concurrent.futures
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMC = os.path.join(ROOT, "build", "tmc")
FIXTURE_PUBLIC = os.path.join(ROOT, "tests", "fixtures", "hw3a_public.txt")
FIXTURE_FULL = os.path.join(ROOT, "tests", "fixtures", "triangle_large.txt")
SUBMISSIONS = sorted(glob.glob(os.path.join(ROOT, "submissions", "*.tm")))

# Lock for serializing CSV writes (tmc appends, but we need atomic access)
csv_lock = threading.Lock()
print_lock = threading.Lock()


def run_student(tm_path, fixture, output, timeout_per_case):
    name = os.path.splitext(os.path.basename(tm_path))[0]
    try:
        proc = subprocess.run(
            [TMC, "--bench", fixture, "--timeout", str(timeout_per_case),
             "--csv", output, tm_path],
            capture_output=True, text=True, timeout=1200,  # 20 min hard kill
        )
        if proc.returncode != 0:
            err = proc.stderr.strip().split("\n")[-1] if proc.stderr else "unknown"
            return name, f"ERROR: {err}"
        else:
            for line in proc.stdout.splitlines():
                if line.startswith("Passed:"):
                    return name, line.strip()
            return name, "done"
    except subprocess.TimeoutExpired:
        return name, "TIMEOUT (20 min)"


def main():
    parser = argparse.ArgumentParser(description="Benchmark TM submissions")
    parser.add_argument("--output", required=True, help="CSV output file path")
    parser.add_argument("--full", action="store_true",
                        help="Use full test suite (triangle_large.txt) instead of public (hw3a_public.txt)")
    args = parser.parse_args()

    if not os.path.exists(TMC):
        print(f"Error: {TMC} not found. Run: cmake -B build && cmake --build build",
              file=sys.stderr)
        sys.exit(1)

    fixture = FIXTURE_FULL if args.full else FIXTURE_PUBLIC
    output = args.output

    if not os.path.exists(fixture):
        print(f"Error: {fixture} not found", file=sys.stderr)
        sys.exit(1)

    # Remove old results so tmc writes a fresh header
    if os.path.exists(output):
        os.remove(output)

    suite_name = "full" if args.full else "public"
    print(f"Suite: {suite_name} ({fixture})")
    print(f"Output: {output}")
    print(f"Students: {len(SUBMISSIONS)}")
    print(f"Timeout: 20 min per student\n")

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(SUBMISSIONS)) as pool:
        futures = {
            pool.submit(run_student, tm_path, fixture, output, 60): tm_path
            for tm_path in SUBMISSIONS
        }
        for future in concurrent.futures.as_completed(futures):
            name, result = future.result()
            print(f"  {name}: {result}")

    print(f"\nResults: {output}")


if __name__ == "__main__":
    main()
