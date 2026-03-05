#!/usr/bin/env python3
"""Run TM benchmarks and generate competition report.

All students run in parallel per fixture. Results, LaTeX, and PDF are
grouped in a timestamped folder under results/.

Usage:
    # Full run: both fixtures, generate report
    python3 scripts/run_benchmarks.py

    # Custom timeout and step limit
    python3 scripts/run_benchmarks.py --timeout 600 --step-limit 500000000

Output:
    results/YYYY-MM-DD_HHMMSS/
        public.csv
        large.csv
        hw3a_report.tex
        hw3a_report.pdf   (if pdflatex available)
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
        return (e.stdout or ""), (e.stderr or ""), True


def parse_summary(stdout):
    """Parse the === Summary === block from tmc --bench stdout."""
    result = {}
    in_summary = False
    for line in stdout.strip().split("\n"):
        if "=== Summary ===" in line:
            in_summary = True
            continue
        if not in_summary:
            continue
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
        row = parse_summary(stdout) if stdout.strip() else {}

        info = {
            "student": name,
            "states": states,
            "transitions": transitions,
            "passed": row.get("passed", 0),
            "failed": row.get("failed", 0) or (row.get("total_cases", 0) - row.get("passed", 0)),
            "total_steps": row.get("total_steps", 0),
            "max_steps": row.get("max_steps", 0),
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
                         "total_steps", "max_steps"])
        for name in sorted(results):
            r = results[name]
            writer.writerow([r["student"], r["states"], r["transitions"],
                             r["passed"], r["failed"], r["total_steps"], r["max_steps"]])

    print(f"\n  Wrote {output_csv}")
    return results


def main():
    parser = argparse.ArgumentParser(description="Run TM benchmarks and generate report")
    parser.add_argument("--step-limit", type=int, default=100_000_000,
                        help="Max steps per test case (default: 100M)")
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
        run_suite(fixture, output_csv, submissions, args.step_limit, args.timeout)

    bench_elapsed = time.time() - total_start
    print(f"\nBenchmarks done in {bench_elapsed:.1f}s")

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

    # Compile PDF
    tex_file = os.path.join(run_dir, "hw3a_report.tex")
    if os.path.exists(tex_file):
        print(f"\nCompiling PDF...")
        result = subprocess.run(
            ["pdflatex", "-interaction=nonstopmode", "hw3a_report.tex"],
            cwd=run_dir, capture_output=True, text=True,
        )
        if result.returncode == 0:
            # Second pass for references
            subprocess.run(
                ["pdflatex", "-interaction=nonstopmode", "hw3a_report.tex"],
                cwd=run_dir, capture_output=True, text=True,
            )
            print(f"  Wrote {os.path.join(run_dir, 'hw3a_report.pdf')}")
        else:
            print("WARNING: pdflatex failed", file=sys.stderr)
            # Show last 20 lines of log for debugging
            log_file = os.path.join(run_dir, "hw3a_report.log")
            if os.path.exists(log_file):
                with open(log_file) as f:
                    lines = f.readlines()
                    for line in lines[-20:]:
                        print(f"  {line.rstrip()}", file=sys.stderr)

    total_elapsed = time.time() - total_start
    print(f"\nAll done in {total_elapsed:.1f}s")
    print(f"Results: {run_dir}")


if __name__ == "__main__":
    main()
