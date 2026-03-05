#!/usr/bin/env python3
"""Run TM benchmarks and generate competition report.

All students run in parallel per fixture. Results, LaTeX, and PDF are
grouped in a timestamped folder under results/.

Usage:
    python3 scripts/run_benchmarks.py
    python3 scripts/run_benchmarks.py --timeout 600

Output:
    results/YYYY-MM-DD_HHMMSS/
        public.csv
        large.csv
        traces/
        hw3a_report.tex
        hw3a_report.pdf   (if pdflatex available)
        mapping.txt
"""

import argparse
import csv
import glob
import os
import subprocess
import sys
import threading
import time
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMC = os.path.join(ROOT, "build", "tmc")
SUBMISSIONS_DIR = os.path.join(ROOT, "submissions")

SUITES = [
    ("tests/fixtures/hw3a_public.txt", "public.csv"),
    ("tests/fixtures/triangle_large.txt", "large.csv"),
]


def run_one(tm_path, fixture, step_limit, timeout):
    """Run tmc --bench for one submission. Returns (stdout, stderr, killed)."""
    cmd = [
        TMC,
        "--bench", fixture,
        "--step-limit", str(step_limit),
        tm_path,
    ]
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout,
        )
        return proc.stdout, proc.stderr, False
    except subprocess.TimeoutExpired as e:
        stdout = e.stdout.decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        stderr = e.stderr.decode() if isinstance(e.stderr, bytes) else (e.stderr or "")
        return stdout, stderr, True


def parse_summary(stdout, step_limit):
    """Parse tmc --bench stdout: per-test lines and summary block.

    Returns a dict with:
      passed, failed, total_steps, max_steps  (from summary block)
      completed       - tests that halted within the step limit
      timed_out       - tests that hit the step limit or were aborted
      completed_steps - total steps for completed tests only
    """
    result = {"completed": 0, "timed_out": 0, "completed_steps": 0}
    in_summary = False
    for line in stdout.strip().split("\n"):
        if "=== Summary ===" in line:
            in_summary = True
            continue
        if in_summary:
            if line.startswith("Passed:"):
                frac = line.split()[1].split("/")
                result["passed"] = int(frac[0])
                result["total_cases"] = int(frac[1])
            elif line.startswith("Failed:"):
                result["failed"] = int(line.split()[1])
            elif line.startswith("Total:"):
                result["total_steps"] = int(line.split()[1])
            elif line.startswith("Max:"):
                result["max_steps"] = int(line.split()[1])
            continue

        # Parse per-test lines: "[  1/82] n= 1 ... steps=     12 ..."
        if line.strip().startswith("[") and "steps=" in line:
            hit_limit = "HIT_LIMIT" in line or "TIMEOUT" in line
            # Extract steps value
            idx = line.index("steps=")
            after = line[idx + 6:].split()[0]
            steps = int(after)
            if hit_limit or steps >= step_limit:
                result["timed_out"] += 1
            else:
                result["completed"] += 1
                result["completed_steps"] += steps

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


def student_name(path):
    return os.path.splitext(os.path.basename(path))[0]


def run_suite(fixture, output_csv, submissions, step_limit, timeout):
    """Run all submissions against one fixture in parallel. Write CSV."""
    fixture_name = os.path.basename(fixture)
    print(f"\n{'='*60}")
    print(f"  {fixture_name} -> {os.path.basename(output_csv)}")
    print(f"  step-limit={step_limit:,}  timeout={timeout}s  students={len(submissions)}")
    print(f"{'='*60}\n")

    lock = threading.Lock()
    results = {}

    def worker(tm_path):
        name = student_name(tm_path)
        t0 = time.time()
        stdout, stderr, killed = run_one(tm_path, fixture, step_limit, timeout)
        elapsed = time.time() - t0

        states, transitions = parse_tm_info(stderr)
        row = parse_summary(stdout, step_limit) if stdout.strip() else {}

        info = {
            "student": name,
            "states": states,
            "transitions": transitions,
            "passed": row.get("passed", 0),
            "failed": row.get("failed", 0) or (row.get("total_cases", 0) - row.get("passed", 0)),
            "total_steps": row.get("total_steps", 0),
            "max_steps": row.get("max_steps", 0),
            "completed": row.get("completed", 0),
            "timed_out": row.get("timed_out", 0),
            "completed_steps": row.get("completed_steps", 0),
            "elapsed": elapsed,
            "killed": killed,
        }

        with lock:
            results[name] = info
            done = len(results)
            status = "KILLED" if killed else ("PASS" if info["failed"] == 0 else "FAIL")
            total_cases = info["passed"] + info["failed"]
            print(f"  [{done}/{len(submissions)}] {name:25s}  {status:6s}  "
                  f"{info['passed']}/{total_cases}  "
                  f"steps={info['total_steps']:>12,}  "
                  f"({elapsed:.1f}s)")

    threads = []
    for tm_path in submissions:
        t = threading.Thread(target=worker, args=(tm_path,))
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    # Write CSV sorted by student name
    with open(output_csv, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["student", "states", "transitions", "passed", "failed",
                         "total_steps", "max_steps", "completed", "timed_out",
                         "completed_steps"])
        for name in sorted(results):
            r = results[name]
            writer.writerow([r["student"], r["states"], r["transitions"],
                             r["passed"], r["failed"], r["total_steps"], r["max_steps"],
                             r["completed"], r["timed_out"], r["completed_steps"]])

    print(f"\n  Wrote {output_csv}")
    return results


STEP_LIMIT = 86_000_000_000


def main():
    parser = argparse.ArgumentParser(description="Run TM benchmarks and generate report")
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="Wall clock timeout per student in seconds (default: 300 = 5 min)")
    args = parser.parse_args()

    if not os.path.exists(TMC):
        print(f"ERROR: {TMC} not found. Run: cmake -B build && cmake --build build",
              file=sys.stderr)
        sys.exit(1)

    submissions = sorted(glob.glob(os.path.join(SUBMISSIONS_DIR, "*.tm")))
    if not submissions:
        print("ERROR: No submissions found in submissions/", file=sys.stderr)
        sys.exit(1)

    # Create timestamped output directory
    timestamp = datetime.now().strftime("%Y-%m-%d_%H%M%S")
    run_dir = os.path.join(ROOT, "results", timestamp)
    os.makedirs(run_dir, exist_ok=True)

    print(f"Output: {run_dir}")
    print(f"Students: {len(submissions)}")

    total_start = time.time()

    # Run benchmarks
    for fixture_rel, csv_name in SUITES:
        fixture = os.path.join(ROOT, fixture_rel)
        if not os.path.exists(fixture):
            print(f"\nWARNING: Fixture not found, skipping: {fixture_rel}")
            continue
        output_csv = os.path.join(run_dir, csv_name)
        run_suite(fixture, output_csv, submissions, STEP_LIMIT, args.timeout)

    bench_elapsed = time.time() - total_start
    print(f"\nBenchmarks done in {bench_elapsed:.1f}s")

    # Run traces for space-time diagrams
    trace_dir = os.path.join(run_dir, "traces")
    os.makedirs(trace_dir, exist_ok=True)
    fixture = os.path.join(ROOT, SUITES[0][0])  # public fixture for traces
    print(f"\nRunning traces (public fixture, max input len 10)...")
    for tm_path in submissions:
        name = student_name(tm_path)
        print(f"  {name}...", end=" ", flush=True)
        try:
            proc = subprocess.run(
                [TMC, "--trace", fixture, "--trace-max-len", "10", tm_path],
                capture_output=True, text=True, timeout=600,
            )
            if proc.stdout.strip():
                trace_path = os.path.join(trace_dir, f"{name}.json")
                with open(trace_path, "w") as f:
                    f.write(proc.stdout)
                print("OK")
            else:
                print("no output")
        except subprocess.TimeoutExpired:
            print("TIMEOUT")
        except Exception as e:
            print(f"ERROR: {e}")
    print(f"  Wrote traces to {trace_dir}")

    # Generate report
    gen_report = os.path.join(ROOT, "scripts", "gen_report.py")
    if os.path.exists(gen_report):
        print(f"\nGenerating report...")
        csv_files = sorted(glob.glob(os.path.join(run_dir, "*.csv")))
        cmd = [sys.executable, gen_report, "--output-dir", run_dir]
        for csv_file in csv_files:
            cmd.extend(["--results-csv", csv_file])
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode != 0:
            print("WARNING: Report generation failed", file=sys.stderr)

    total_elapsed = time.time() - total_start
    print(f"\nAll done in {total_elapsed:.1f}s")
    print(f"Results: {run_dir}")


if __name__ == "__main__":
    main()
