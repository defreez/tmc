#!/usr/bin/env python3
"""Run TM benchmarks for all submissions against a test fixture.

Features:
  - Progress output with per-submission timing
  - Subprocess timeout (kills hangs like brent-monning)
  - Resume support (skips students already in output CSV)
  - Streams tmc stdout/stderr live so you can watch progress

Usage:
    # Public fixture (fast, ~30 seconds total)
    python3 scripts/run_benchmarks.py tests/fixtures/hw3a_public.txt -o results/public.csv

    # Large fixture (slow, may take hours for cubic machines)
    python3 scripts/run_benchmarks.py tests/fixtures/triangle_large.txt -o results/large.csv

    # Resume an interrupted run
    python3 scripts/run_benchmarks.py tests/fixtures/triangle_large.txt -o results/large.csv --resume

    # Custom step limit and timeout
    python3 scripts/run_benchmarks.py tests/fixtures/triangle_large.txt -o results/large.csv \
        --step-limit 100000000 --timeout 300
"""

import argparse
import csv
import glob
import os
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMC = os.path.join(ROOT, "build", "tmc")
SUBMISSIONS_DIR = os.path.join(ROOT, "submissions")


def load_completed(csv_path):
    """Return set of student names already in the CSV."""
    if not os.path.exists(csv_path):
        return set()
    completed = set()
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            completed.add(row["student"])
    return completed


def run_one(tm_path, fixture, step_limit, timeout_secs):
    """Run tmc --bench for one submission. Returns (stdout, stderr, success)."""
    cmd = [
        TMC,
        "--bench", fixture,
        "--step-limit", str(step_limit),
        "--timeout", str(timeout_secs),
        tm_path,
    ]
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid,
        )
        stdout_lines = []
        # Stream stdout live
        for line in proc.stdout:
            sys.stdout.write("    " + line)
            sys.stdout.flush()
            stdout_lines.append(line)
        proc.wait(timeout=timeout_secs * 2)
        stderr = proc.stderr.read()
        return "".join(stdout_lines), stderr, proc.returncode
    except subprocess.TimeoutExpired:
        # Kill the entire process group
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        proc.wait()
        return "", "", -1


def parse_summary(stdout):
    """Parse the === Summary === block from tmc --bench stdout."""
    lines = stdout.strip().split("\n")
    result = {}
    in_summary = False
    for line in lines:
        if "=== Summary ===" in line:
            in_summary = True
            continue
        if not in_summary:
            continue
        if line.startswith("Passed:"):
            parts = line.split()
            frac = parts[1].split("/")
            result["passed"] = int(frac[0])
            result["total_cases"] = int(frac[1])
        elif line.startswith("Failed:"):
            result["failed"] = int(line.split()[1])
        elif line.startswith("Total:"):
            # "Total:   7186258 steps"
            result["total_steps"] = int(line.split()[1])
        elif line.startswith("Max:"):
            # "Max:     746323 steps (n=40, |w|=860)"
            result["max_steps"] = int(line.split()[1])
    if "failed" not in result:
        result["failed"] = 0
    return result


def parse_tm_info(stderr):
    """Parse 'TM: X states, Y transitions' from stderr."""
    for line in stderr.split("\n"):
        if line.startswith("TM:"):
            parts = line.split()
            return int(parts[1]), int(parts[3])
    return 0, 0


def main():
    parser = argparse.ArgumentParser(description="Run TM benchmarks for all submissions")
    parser.add_argument("fixture", help="Test fixture file")
    parser.add_argument("-o", "--output", required=True, help="Output CSV file")
    parser.add_argument("--step-limit", type=int, default=100_000_000,
                        help="Max steps per test case (default: 100M)")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="Wall clock timeout per test case in seconds (default: 120)")
    parser.add_argument("--resume", action="store_true",
                        help="Skip students already in the output CSV")
    args = parser.parse_args()

    if not os.path.exists(TMC):
        print(f"ERROR: {TMC} not found. Run: cmake -B build && cmake --build build",
              file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(args.fixture):
        print(f"ERROR: Fixture not found: {args.fixture}", file=sys.stderr)
        sys.exit(1)

    submissions = sorted(glob.glob(os.path.join(SUBMISSIONS_DIR, "*.tm")))
    if not submissions:
        print("ERROR: No submissions found in submissions/", file=sys.stderr)
        sys.exit(1)

    # Load already-completed students if resuming
    completed = load_completed(args.output) if args.resume else set()
    if completed:
        print(f"Resuming: {len(completed)} students already done, skipping them\n")

    remaining = [s for s in submissions
                 if os.path.splitext(os.path.basename(s))[0] not in completed]

    fixture_name = os.path.basename(args.fixture)
    print(f"Fixture:    {fixture_name}")
    print(f"Output:     {args.output}")
    print(f"Step limit: {args.step_limit:,}")
    print(f"Timeout:    {args.timeout}s per case")
    print(f"Students:   {len(remaining)} remaining of {len(submissions)} total")
    print()

    # Ensure output directory exists
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    # Write CSV header if file doesn't exist
    write_header = not os.path.exists(args.output) or os.path.getsize(args.output) == 0
    if write_header:
        with open(args.output, "w") as f:
            f.write("student,states,transitions,passed,failed,total_steps,max_steps\n")

    total_start = time.time()
    results = []

    for i, tm_path in enumerate(remaining):
        name = os.path.splitext(os.path.basename(tm_path))[0]
        print(f"[{i+1}/{len(remaining)}] {name}")

        t0 = time.time()
        stdout, stderr, returncode = run_one(
            tm_path, args.fixture, args.step_limit, args.timeout
        )
        elapsed = time.time() - t0

        if returncode == -1:
            print(f"  KILLED (subprocess timeout after {elapsed:.1f}s)")
            states, transitions = 0, 0
            row = {"passed": 0, "failed": 0, "total_steps": 0, "max_steps": 0}
        else:
            states, transitions = parse_tm_info(stderr)
            row = parse_summary(stdout)

        if not row:
            print(f"  ERROR: Could not parse results")
            continue

        # Append to CSV
        with open(args.output, "a") as f:
            f.write(f"{name},{states},{transitions},"
                    f"{row.get('passed',0)},{row.get('failed',0)},"
                    f"{row.get('total_steps',0)},{row.get('max_steps',0)}\n")

        passed = row.get("passed", 0)
        total_cases = row.get("total_cases", passed + row.get("failed", 0))
        status = "PASS" if row.get("failed", 0) == 0 else "FAIL"
        print(f"  {status} {passed}/{total_cases}  "
              f"steps={row.get('total_steps',0):,}  "
              f"max={row.get('max_steps',0):,}  "
              f"({elapsed:.1f}s)\n")

    total_elapsed = time.time() - total_start
    print(f"Done. {len(remaining)} students in {total_elapsed:.1f}s")
    print(f"Results: {args.output}")


if __name__ == "__main__":
    main()
