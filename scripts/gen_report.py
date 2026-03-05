#!/usr/bin/env python3
"""Generate a LaTeX competition report for HW3A TM submissions.

Usage:
    python scripts/gen_report.py [--output-dir reports/]

Runs build/tmc --trace for each submission, collects JSON data,
and generates a single LaTeX report with leaderboard, comparative
analysis, and tape visualizations for all machines.
"""

import argparse
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMC = os.path.join(ROOT, "build", "tmc")
FIXTURE = os.path.join(ROOT, "tests", "fixtures", "hw3a_public.txt")
SUBMISSIONS_DIR = os.path.join(ROOT, "submissions")


def run_trace(tm_path, fixture, trace_max_len=10):
    """Run tmc --trace on a submission and return parsed JSON."""
    name = os.path.splitext(os.path.basename(tm_path))[0]
    try:
        proc = subprocess.run(
            [TMC, "--trace", fixture, "--trace-max-len", str(trace_max_len), tm_path],
            capture_output=True, text=True, timeout=600,
        )
        if proc.returncode != 0:
            print(f"  WARNING: {name} exited with code {proc.returncode}", file=sys.stderr)
            # Still try to parse - some machines fail tests but produce valid JSON
        return json.loads(proc.stdout)
    except subprocess.TimeoutExpired:
        print(f"  ERROR: {name} timed out", file=sys.stderr)
        return None
    except json.JSONDecodeError as e:
        print(f"  ERROR: {name} invalid JSON: {e}", file=sys.stderr)
        return None


def create_mapping(data_list):
    """Create alphabetical de-identification mapping."""
    names = sorted(d["student"] for d in data_list)
    mapping = {}
    for i, name in enumerate(names):
        mapping[name] = chr(ord('A') + i)
    return mapping


def latex_escape(s):
    """Escape special LaTeX characters."""
    replacements = [
        ('\\', r'\textbackslash{}'),
        ('&', r'\&'),
        ('%', r'\%'),
        ('$', r'\$'),
        ('#', r'\#'),
        ('_', r'\_'),
        ('{', r'\{'),
        ('}', r'\}'),
        ('~', r'\textasciitilde{}'),
        ('^', r'\textasciicircum{}'),
    ]
    for old, new in replacements:
        s = s.replace(old, new)
    return s


def format_number(n):
    """Format a number with commas."""
    return f"{n:,}"


def preamble():
    return r"""\documentclass[11pt,letterpaper]{article}
\usepackage[margin=1in]{geometry}
\usepackage{tikz}
\usepackage{pgfplots}
\usepackage{booktabs}
\usepackage[table]{xcolor}
\usepackage{hyperref}
\usepackage{float}
\usepackage{amsmath}
\usepackage{amssymb}

\pgfplotsset{compat=1.18}

\definecolor{headcell}{HTML}{4A90D9}
\definecolor{headcelllight}{HTML}{D6E8F7}
\definecolor{winnerrow}{HTML}{E8F5E9}
\definecolor{failrow}{HTML}{FFF3E0}

% Tape cell TikZ styles
\tikzset{
  cell/.style={draw, minimum width=5.5mm, minimum height=5.5mm,
               font=\ttfamily\small, inner sep=0pt, anchor=west},
  headcell/.style={cell, fill=headcelllight, draw=headcell, line width=0.8pt},
  blankcell/.style={cell, fill=gray!5},
}

% Space-time diagram symbol colors
\definecolor{sym-a}{HTML}{3B82F6}
\definecolor{sym-b}{HTML}{F59E0B}
\definecolor{sym-blank}{HTML}{F3F4F6}
\definecolor{sym-head}{HTML}{111827}

\title{\textbf{Theory of Computation: HW3A}\\[0.3em]
       \Large Competition Results}
\author{CS 418 --- Winter 2026}
\date{March 5, 2026}

\begin{document}
\maketitle
"""


def introduction():
    return r"""
\section{Problem}

Let $T(n) = \frac{n(n+1)}{2}$ denote the $n$-th triangular number. The language is:
\[
A = \{ a^n b^m \mid n \geq 0 \text{ and } m = T(n) \}
\]
Each student built a single-tape Turing machine to decide $A$.
Machines were scored by average step count across a test suite of 41 cases
with inputs ranging from $n=0$ to $n=39$.

\medskip
\noindent\textbf{Scoring.} A step is one application of the transition function $\delta$.
Given a test suite $S = \{w_1, \ldots, w_k\}$, the score is
$\text{score}(M) = \frac{1}{k}\sum_{i=1}^k \text{steps}(M, w_i)$.
The lowest score wins.

"""


def leaderboard(data_list, mapping):
    """Generate the leaderboard table."""
    # Sort: passing machines first (by total steps), then failing (by passed desc)
    passing = [d for d in data_list if d["_failed"] == 0]
    failing = [d for d in data_list if d["_failed"] > 0]
    passing.sort(key=lambda d: d["_total_steps"])
    failing.sort(key=lambda d: -d["_passed"])

    ranked = passing + failing

    lines = [r"\section{Leaderboard}", ""]
    lines.append(r"\begin{table}[H]")
    lines.append(r"\centering")
    lines.append(r"\begin{tabular}{r l r r r r r}")
    lines.append(r"\toprule")
    lines.append(r"Rank & Machine & States & Transitions & Passed & Score (avg steps) & Max Steps \\")
    lines.append(r"\midrule")

    for i, d in enumerate(ranked):
        machine_id = f"Machine {mapping[d['student']]}"
        rank = i + 1
        passed_str = f"{d['_passed']}/{d['_total_cases']}"
        avg_steps = d["_total_steps"] / d["_total_cases"] if d["_total_cases"] > 0 else 0

        row_prefix = ""
        row_suffix = ""
        if i == 0 and d["_failed"] == 0:
            row_prefix = r"\rowcolor{winnerrow}"
        elif d["_failed"] > 0:
            row_prefix = r"\rowcolor{failrow}"

        rank_str = str(rank) if d["_failed"] == 0 else "---"

        lines.append(
            f"{row_prefix}"
            f"{rank_str} & {machine_id} & "
            f"{format_number(d['states'])} & {format_number(d['transitions'])} & "
            f"{passed_str} & "
            f"{format_number(int(avg_steps))} & "
            f"{format_number(d['_max_steps'])} \\\\"
        )

    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    lines.append(r"\caption{All submissions ranked by average step count. "
                 r"\colorbox{winnerrow}{Green} = winner. "
                 r"\colorbox{failrow}{Orange} = did not pass all test cases.}")
    lines.append(r"\end{table}")
    lines.append("")

    # Winner announcement
    if passing:
        winner = passing[0]
        winner_id = f"Machine {mapping[winner['student']]}"
        avg = winner["_total_steps"] / winner["_total_cases"]
        lines.append(f"\\noindent\\textbf{{Winner: {winner_id}}} with an average of "
                     f"{format_number(int(avg))} steps per test case "
                     f"using {format_number(winner['states'])} states and "
                     f"{format_number(winner['transitions'])} transitions.")
    lines.append("")

    return "\n".join(lines)


def comparative_analysis(data_list, mapping):
    """Generate scatter plot and growth curves."""
    passing = [d for d in data_list if d["_failed"] == 0]
    passing.sort(key=lambda d: d["_total_steps"])

    lines = [r"\section{Comparative Analysis}", ""]

    # Size vs Speed scatter (log-log)
    lines.append(r"\subsection{Size vs.\ Speed}")
    lines.append("")
    lines.append(r"\begin{figure}[H]")
    lines.append(r"\centering")
    lines.append(r"\begin{tikzpicture}")
    lines.append(r"\begin{axis}[")
    lines.append(r"  xlabel={States (log scale)},")
    lines.append(r"  ylabel={Total Steps (log scale)},")
    lines.append(r"  xmode=log, ymode=log,")
    lines.append(r"  width=0.85\textwidth,")
    lines.append(r"  height=0.5\textwidth,")
    lines.append(r"  grid=major,")
    lines.append(r"  nodes near coords,")
    lines.append(r"  nodes near coords style={font=\tiny, anchor=south west},")
    lines.append(r"  point meta=explicit symbolic,")
    lines.append(r"  scatter/classes={")
    lines.append(r"    pass={mark=*, blue!70!black, mark size=3pt},")
    lines.append(r"    fail={mark=x, red!70!black, mark size=3pt}},")
    lines.append(r"]")

    lines.append(r"\addplot[scatter, only marks, scatter src=explicit symbolic] coordinates {")
    for d in data_list:
        cls = "pass" if d["_failed"] == 0 else "fail"
        mid = mapping[d["student"]]
        lines.append(f"  ({d['states']}, {d['_total_steps']}) [{cls}] %% {mid}")
    lines.append(r"};")

    lines.append(r"\end{axis}")
    lines.append(r"\end{tikzpicture}")
    lines.append(r"\caption{Machine size (states) vs.\ total execution steps. "
                 r"Two distinct clusters emerge: compact hand-written machines (10--36 states) "
                 r"and precomputed lookup tables (100K+ states).}")
    lines.append(r"\end{figure}")
    lines.append("")

    # Step count growth curves
    lines.append(r"\subsection{Step Count Growth}")
    lines.append("")
    lines.append(r"\begin{figure}[H]")
    lines.append(r"\centering")
    lines.append(r"\begin{tikzpicture}")
    lines.append(r"\begin{axis}[")
    lines.append(r"  xlabel={$n$ (number of $a$'s in input)},")
    lines.append(r"  ylabel={Steps},")
    lines.append(r"  ymode=log,")
    lines.append(r"  width=0.85\textwidth,")
    lines.append(r"  height=0.55\textwidth,")
    lines.append(r"  grid=major,")
    lines.append(r"  legend style={at={(1.02,1)}, anchor=north west, font=\tiny},")
    lines.append(r"  cycle list name=color list,")
    lines.append(r"]")

    # Plot each passing machine's step curve
    colors = [
        "blue", "red", "green!60!black", "orange", "purple",
        "cyan!70!black", "brown", "magenta", "olive", "teal",
        "violet", "lime!60!black",
    ]
    for i, d in enumerate(passing):
        mid = mapping[d["student"]]
        color = colors[i % len(colors)]
        # Extract (n, steps) from results, only accept cases
        coords = []
        for r in d.get("results", []):
            if r["expected"] and r["accepted"]:
                coords.append((r["n"], r["steps"]))
        coords.sort()
        if coords:
            lines.append(f"\\addplot[{color}, thick, mark=none] coordinates {{")
            for n, steps in coords:
                lines.append(f"  ({n}, {steps})")
            lines.append(r"};")
            lines.append(f"\\addlegendentry{{Machine {mid}}}")

    lines.append(r"\end{axis}")
    lines.append(r"\end{tikzpicture}")
    lines.append(r"\caption{Steps vs.\ $n$ for correctly-accepted inputs. "
                 r"Precomputed machines show near-constant cost; "
                 r"hand-written machines show polynomial growth.}")
    lines.append(r"\end{figure}")
    lines.append("")

    # Textual observations
    lines.append(r"\subsection{Observations}")
    lines.append("")

    # Identify precomputed vs hand-written
    precomputed = [d for d in passing if d["states"] > 1000]
    handwritten = [d for d in passing if d["states"] <= 1000]

    if precomputed:
        pc_names = ", ".join(f"Machine {mapping[d['student']]}" for d in precomputed)
        lines.append(f"\\textbf{{Precomputed machines}} ({pc_names}) "
                     f"trade an enormous state space for minimal step counts. "
                     f"These machines effectively encode a lookup table, "
                     f"resolving each input in $O(n)$ steps or fewer.")
        lines.append("")

    if handwritten:
        hw_sorted = sorted(handwritten, key=lambda d: d["_total_steps"])
        hw_names = ", ".join(f"Machine {mapping[d['student']]}" for d in hw_sorted)
        best = hw_sorted[0]
        worst = hw_sorted[-1]
        lines.append(f"\\textbf{{Hand-written machines}} ({hw_names}) "
                     f"use compact representations (11--36 states) but require "
                     f"millions of steps on larger inputs. "
                     f"Machine {mapping[best['student']]} leads this group "
                     f"with {format_number(best['_total_steps'])} total steps, "
                     f"roughly {worst['_total_steps'] // best['_total_steps']}$\\times$ "
                     f"fewer than Machine {mapping[worst['student']]} "
                     f"({format_number(worst['_total_steps'])} total steps).")
        lines.append("")

    # Check for identical machines
    step_groups = {}
    for d in passing:
        key = d["_total_steps"]
        step_groups.setdefault(key, []).append(d)
    for steps, group in step_groups.items():
        if len(group) > 1:
            names = " and ".join(f"Machine {mapping[d['student']]}" for d in group)
            lines.append(f"\\textbf{{Identical behavior:}} {names} "
                         f"produced the exact same step count on every test case, "
                         f"suggesting functionally equivalent machines.")
            lines.append("")

    failing = [d for d in data_list if d["_failed"] > 0]
    if failing:
        for d in failing:
            lines.append(f"Machine {mapping[d['student']]} "
                         f"passed {d['_passed']}/{d['_total_cases']} test cases.")
        lines.append("")

    return "\n".join(lines)


def render_tape_tikz(config, step_label=None):
    """Render a single tape snapshot as TikZ."""
    tape = config["tape"]
    head = config["head"]
    state = config["state"]
    step = config["step"]

    # Pad tape with blanks if head is beyond stored tape
    while len(tape) <= head:
        tape += '_'

    # Determine visible window: from 0 to max(head, last_nonblank) + padding
    last_nonblank = 0
    for i, ch in enumerate(tape):
        if ch != '_':
            last_nonblank = i
    end = max(last_nonblank + 2, head + 2, 2)
    end = min(end, len(tape) + 1)  # allow 1 blank past tape

    # Cap visible tape at 25 cells
    visible_start = 0
    if end > 25:
        visible_start = max(0, head - 12)
        end = min(visible_start + 25, len(tape))

    lines = []
    lines.append(r"  \begin{tikzpicture}")

    for i in range(visible_start, end):
        ch = tape[i] if i < len(tape) else '_'  # blanks beyond tape
        display_ch = latex_escape(ch) if ch != '_' else r'\textvisiblespace'
        x = (i - visible_start) * 0.6
        style = "headcell" if i == head else ("blankcell" if ch == '_' else "cell")
        lines.append(f"    \\node[{style}] (c{i}) at ({x:.1f}, 0) {{{display_ch}}};")

    # Head arrow below
    head_x = (head - visible_start) * 0.6
    escaped_state = latex_escape(state)
    label = f"Step {step}, \\texttt{{{escaped_state}}}"
    if step_label:
        label = step_label
    lines.append(f"    \\draw[->, thick, headcell] ({head_x:.1f}, -0.32) -- ({head_x:.1f}, -0.55)"
                 f" node[below, font=\\tiny] {{{label}}};")

    lines.append(r"  \end{tikzpicture}")
    return "\n".join(lines)


# Color palette for tape symbols in space-time diagrams.
# Maps symbol char -> LaTeX color name.  Unknown symbols get auto-assigned.
_BASE_SYMBOL_COLORS = {
    'a': 'sym-a',
    'b': 'sym-b',
    '_': 'sym-blank',
}

# Extra colors for markers / tape alphabet extras
_EXTRA_COLORS = [
    ("HTML", "10B981"),  # green
    ("HTML", "EF4444"),  # red
    ("HTML", "8B5CF6"),  # purple
    ("HTML", "EC4899"),  # pink
    ("HTML", "06B6D4"),  # cyan
    ("HTML", "84CC16"),  # lime
    ("HTML", "F97316"),  # deep orange
    ("HTML", "6366F1"),  # indigo
    ("HTML", "14B8A6"),  # teal
    ("HTML", "A855F7"),  # violet
    ("HTML", "D946EF"),  # fuchsia
    ("HTML", "78716C"),  # stone
]


def _build_color_map(configs):
    """Collect all symbols in a trace and assign colors."""
    symbols = set()
    for cfg in configs:
        for ch in cfg["tape"]:
            symbols.add(ch)

    color_map = {}
    extra_idx = 0
    color_defs = []  # extra \definecolor lines needed

    for sym in sorted(symbols):
        if sym in _BASE_SYMBOL_COLORS:
            color_map[sym] = _BASE_SYMBOL_COLORS[sym]
        else:
            if extra_idx < len(_EXTRA_COLORS):
                model, value = _EXTRA_COLORS[extra_idx]
                cname = f"sym-extra-{extra_idx}"
                color_defs.append(f"\\definecolor{{{cname}}}{{{model}}}{{{value}}}")
                color_map[sym] = cname
                extra_idx += 1
            else:
                color_map[sym] = "black"

    return color_map, color_defs


def render_spacetime_tikz(trace):
    """Render a space-time diagram (Wolfram-style waterfall) for a trace.

    Each row = one time step.  Each column = one tape position.
    Cells are filled with the symbol color.  Head position is marked
    with a small black dot.
    """
    configs = trace["configs"]
    if not configs:
        return ""

    color_map, extra_defs = _build_color_map(configs)

    # Determine tape window across all steps
    max_tape_len = 0
    max_head = 0
    for cfg in configs:
        tape = cfg["tape"]
        # extend for head past end
        tl = max(len(tape), cfg["head"] + 1)
        max_tape_len = max(max_tape_len, tl)
        max_head = max(max_head, cfg["head"])

    # Visible range: 0 to max used position + 1 padding
    num_cols = min(max_tape_len + 1, 30)  # cap width

    num_rows = len(configs)

    # Cell size in cm
    cs = 0.25  # cell size -- small for dense diagrams
    # If few steps, make cells bigger
    if num_rows <= 20:
        cs = 0.35

    # Sample rows if too many (keep first, last, sample middle)
    max_rows = 200
    if num_rows > max_rows:
        step = num_rows / max_rows
        sampled_indices = [int(i * step) for i in range(max_rows)]
        if num_rows - 1 not in sampled_indices:
            sampled_indices.append(num_rows - 1)
        sampled_configs = [configs[i] for i in sampled_indices]
    else:
        sampled_configs = configs
        sampled_indices = list(range(num_rows))

    actual_rows = len(sampled_configs)

    lines = []
    if extra_defs:
        lines.extend(extra_defs)

    lines.append(r"\begin{tikzpicture}[x=1cm, y=1cm]")

    # Draw cells as filled rectangles
    for row_idx, cfg in enumerate(sampled_configs):
        tape = cfg["tape"]
        head = cfg["head"]
        y = -row_idx * cs

        for col in range(num_cols):
            if col < len(tape):
                sym = tape[col]
            else:
                sym = '_'
            color = color_map.get(sym, 'gray')
            x = col * cs

            lines.append(
                f"  \\fill[{color}] ({x:.3f},{y:.3f}) "
                f"rectangle ({x + cs:.3f},{y + cs:.3f});"
            )

            # Head marker: small black dot
            if col == head:
                cx = x + cs / 2
                cy = y + cs / 2
                lines.append(
                    f"  \\fill[sym-head] ({cx:.3f},{cy:.3f}) circle ({cs * 0.2:.3f});"
                )

    # Border around the whole diagram
    lines.append(
        f"  \\draw[gray, thin] (0,{-actual_rows * cs + cs:.3f}) "
        f"rectangle ({num_cols * cs:.3f},{cs:.3f});"
    )

    # Y-axis label: step numbers at a few points
    label_steps = [0]
    if actual_rows > 1:
        label_steps.append(actual_rows - 1)
    if actual_rows > 10:
        label_steps.append(actual_rows // 2)

    for li in label_steps:
        real_step = sampled_indices[li] if li < len(sampled_indices) else li
        y = -li * cs + cs / 2
        lines.append(
            f"  \\node[left, font=\\tiny, text=gray] at (0,{y:.3f}) {{{real_step}}};"
        )

    # Legend: symbol -> color boxes
    legend_y = -actual_rows * cs - 0.3
    legend_x = 0.0
    for sym in sorted(color_map.keys()):
        color = color_map[sym]
        display = latex_escape(sym) if sym != '_' else r'\textvisiblespace'
        lines.append(
            f"  \\fill[{color}] ({legend_x:.2f},{legend_y:.2f}) "
            f"rectangle ({legend_x + 0.25:.2f},{legend_y + 0.25:.2f});"
        )
        lines.append(
            f"  \\draw[gray, thin] ({legend_x:.2f},{legend_y:.2f}) "
            f"rectangle ({legend_x + 0.25:.2f},{legend_y + 0.25:.2f});"
        )
        lines.append(
            f"  \\node[right, font=\\tiny] at ({legend_x + 0.3:.2f},{legend_y + 0.125:.2f}) "
            f"{{\\texttt{{{display}}}}};"
        )
        legend_x += 0.8
        if legend_x > num_cols * cs - 0.5:
            legend_x = 0.0
            legend_y -= 0.35

    # Head legend
    lines.append(
        f"  \\fill[sym-head] ({legend_x + 0.125:.3f},{legend_y + 0.125:.3f}) circle (0.06);"
    )
    lines.append(
        f"  \\node[right, font=\\tiny] at ({legend_x + 0.3:.2f},{legend_y + 0.125:.2f}) "
        f"{{head}};"
    )

    lines.append(r"\end{tikzpicture}")
    return "\n".join(lines)


def tape_visualizations(trace_data, mapping):
    """Generate space-time diagrams and tape snapshots for a machine's traces."""
    lines = []

    for trace in trace_data.get("traces", []):
        input_str = trace["input"]
        if input_str == "":
            input_label = r"$\varepsilon$"
        else:
            input_label = f"\\texttt{{{latex_escape(input_str)}}}"

        configs = trace["configs"]
        total_steps = trace["total_steps"]
        accepted = trace["accepted"]
        expected = trace["expected"]
        result_str = "Accept" if accepted else "Reject"
        correct_str = "" if accepted == expected else " (INCORRECT)"

        lines.append(f"\\paragraph{{Input: {input_label} "
                     f"({total_steps} steps, {result_str}{correct_str})}}")
        lines.append("")

        # Space-time diagram (waterfall)
        if len(configs) > 1:
            lines.append(r"\noindent")
            lines.append(render_spacetime_tikz(trace))
            lines.append(r"\bigskip")
            lines.append("")

    return "\n".join(lines)


def machine_detail_page(d, mapping, all_data):
    """Generate a detail page for one machine."""
    mid = mapping[d["student"]]
    lines = []

    lines.append(f"\\subsection*{{Machine {mid}}}")
    lines.append("")

    # Summary table
    avg_steps = d["_total_steps"] / d["_total_cases"] if d["_total_cases"] > 0 else 0
    lines.append(r"\begin{tabular}{ll}")
    lines.append(f"States & {format_number(d['states'])} \\\\")
    lines.append(f"Transitions & {format_number(d['transitions'])} \\\\")
    lines.append(f"Test cases passed & {d['_passed']}/{d['_total_cases']} \\\\")
    lines.append(f"Average steps & {format_number(int(avg_steps))} \\\\")
    lines.append(f"Max steps (single case) & {format_number(d['_max_steps'])} \\\\")
    lines.append(r"\end{tabular}")
    lines.append(r"\medskip")
    lines.append("")

    # Tape visualizations
    if d.get("traces"):
        lines.append(tape_visualizations(d, mapping))
    else:
        lines.append("\\textit{No trace data available (machine too large to trace).}")
        lines.append("")

    lines.append(r"\clearpage")
    return "\n".join(lines)


def generate_latex(data_list, mapping):
    """Generate the full LaTeX document."""
    parts = []
    parts.append(preamble())
    parts.append(introduction())
    parts.append(leaderboard(data_list, mapping))
    parts.append(comparative_analysis(data_list, mapping))

    # Machine detail pages
    parts.append(r"\section{Machine Details}")
    parts.append("")
    parts.append("Each machine is shown with space-time diagrams on small inputs. "
                 "Columns represent tape positions, rows represent time steps (top to bottom). "
                 "Black dots mark the head position.")
    parts.append("")

    # Sort: passing by rank (total steps), then failing
    passing = sorted([d for d in data_list if d["_failed"] == 0],
                     key=lambda d: d["_total_steps"])
    failing = sorted([d for d in data_list if d["_failed"] > 0],
                     key=lambda d: -d["_passed"])
    for d in passing + failing:
        parts.append(machine_detail_page(d, mapping, data_list))

    parts.append(r"\end{document}")
    return "\n".join(parts)


def main():
    parser = argparse.ArgumentParser(description="Generate HW3A competition report")
    parser.add_argument("--output-dir", default=os.path.join(ROOT, "reports"),
                        help="Output directory for LaTeX files")
    parser.add_argument("--trace-max-len", type=int, default=10,
                        help="Max input length for full traces")
    args = parser.parse_args()

    if not os.path.exists(TMC):
        print(f"Error: {TMC} not found. Run: cmake -B build && cmake --build build",
              file=sys.stderr)
        sys.exit(1)

    submissions = sorted(glob.glob(os.path.join(SUBMISSIONS_DIR, "*.tm")))
    if not submissions:
        print("Error: No submissions found", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(submissions)} submissions")
    print(f"Fixture: {FIXTURE}")
    print(f"Output: {args.output_dir}")
    print()

    # Run traces for all submissions
    data_list = []
    for tm_path in submissions:
        name = os.path.splitext(os.path.basename(tm_path))[0]
        print(f"  Tracing {name}...", end=" ", flush=True)
        d = run_trace(tm_path, FIXTURE, args.trace_max_len)
        if d is None:
            print("FAILED")
            continue

        # Compute aggregate stats from results
        results = d.get("results", [])
        passed = sum(1 for r in results if r["accepted"] == r["expected"] and not r.get("hit_limit", False))
        failed = len(results) - passed
        total_steps = sum(r["steps"] for r in results)
        max_steps = max((r["steps"] for r in results), default=0)

        d["_passed"] = passed
        d["_failed"] = failed
        d["_total_steps"] = total_steps
        d["_max_steps"] = max_steps
        d["_total_cases"] = len(results)

        data_list.append(d)
        avg = total_steps / len(results) if results else 0
        print(f"OK ({passed}/{len(results)} passed, avg {int(avg)} steps)")

    if not data_list:
        print("Error: No valid data collected", file=sys.stderr)
        sys.exit(1)

    # Create mapping
    mapping = create_mapping(data_list)

    # Generate LaTeX
    print(f"\nGenerating LaTeX...")
    latex = generate_latex(data_list, mapping)

    os.makedirs(args.output_dir, exist_ok=True)
    tex_path = os.path.join(args.output_dir, "hw3a_report.tex")
    with open(tex_path, "w") as f:
        f.write(latex)
    print(f"Wrote {tex_path}")

    # Write mapping file
    mapping_path = os.path.join(args.output_dir, "mapping.txt")
    with open(mapping_path, "w") as f:
        f.write("# HW3A De-identification Mapping (DO NOT DISTRIBUTE)\n")
        for student, machine_id in sorted(mapping.items(), key=lambda x: x[1]):
            f.write(f"Machine {machine_id} = {student}\n")
    print(f"Wrote {mapping_path}")

    print(f"\nTo compile: cd {args.output_dir} && pdflatex hw3a_report.tex")


if __name__ == "__main__":
    main()
