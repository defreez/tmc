#!/usr/bin/env python3
"""Generate a LaTeX competition report from saved benchmark results.

Reads CSVs and trace JSON files produced by run_benchmarks.py.
Does not run tmc — only processes previously saved data.

Usage:
    # Default: auto-discover most recent results/<timestamp>/ directory
    python3 scripts/gen_report.py

    # Explicit: specify CSV files and output directory
    python3 scripts/gen_report.py \\
        --results-csv results/<timestamp>/public.csv \\
        --results-csv results/<timestamp>/large.csv \\
        --output-dir results/<timestamp>

Trace JSON files are loaded automatically from <output-dir>/traces/*.json
if present (produced by run_benchmarks.py).
"""

import argparse
import csv
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# One-paragraph algorithm summaries for each submission, keyed by student name.
# These are inserted on each machine's detail page in the report.
MACHINE_SUMMARIES = {
    "adan-blanco": (
        "Uses a right-shift initialization that copies the input past a "
        "\\texttt{>} marker. In each round $k$, marks one \\texttt{a} as "
        "\\texttt{X}, then performs an inner loop: for every \\texttt{X} on "
        "tape, temporarily marks it as \\texttt{A} and sweeps right to cross "
        "off one \\texttt{b} as \\texttt{Y}. This consumes exactly $k$ "
        "\\texttt{b}'s in round $k$, verifying the triangular sum "
        "$1+2+\\cdots+n$."
    ),
    "alec-clark": (
        "Places a \\texttt{\\$} marker at the left end and shifts the input "
        "rightward. Maintains a growing counter region of \\texttt{c}/\\texttt{d} "
        "symbols at the right end of the tape. Each round marks one "
        "\\texttt{a} as \\texttt{X} and appends one new \\texttt{c} to the "
        "counter, then sweeps back to match every counter tick against one "
        "\\texttt{b}. The counter grows by one each round, so total "
        "\\texttt{b}'s consumed equals $1+2+\\cdots+n$."
    ),
    "brayden-stach": (
        "Nearly identical algorithm to Machine~C (counter-tick approach). "
        "Shifts input past a \\texttt{\\$} marker, then in each round marks "
        "one \\texttt{a} as \\texttt{X}, appends a counter symbol, and matches "
        "each counter tick with one \\texttt{b}. Well-commented implementation "
        "with the same step counts as Machine~C on all test cases."
    ),
    "brent-monning": (
        "Attempts a two-phase approach: Phase~1 marks \\texttt{a}'s as "
        "\\texttt{c} while restructuring the tape layout, then Phase~2 uses "
        "\\texttt{x}/\\texttt{y} counter symbols to verify the triangular "
        "count. However, this machine fails all 41 public test cases, "
        "suggesting a bug in the transition logic."
    ),
    "cory-short": (
        "Precomputed lookup table. For $n \\leq 100$, encodes the answer "
        "directly in states named \\texttt{a$\\langle n\\rangle$b$\\langle m\\rangle$} "
        "that count the exact number of \\texttt{a}'s and \\texttt{b}'s seen "
        "--- producing 171K states. For $n > 100$, falls back to an "
        "algorithmic branch (states \\texttt{s}, \\texttt{q1-*}, \\texttt{q2-*}) "
        "that uses base-9 unary counting to verify the triangular sum."
    ),
    "elliott-glenn": (
        "The most compact hand-written machine at 11~states. Uses a ping-pong "
        "crossing-off strategy: in round $k$, marks the $k$-th remaining "
        "\\texttt{a} as \\texttt{X}, then sweeps right and crosses off one "
        "\\texttt{b} for each \\texttt{X} on tape. Since the number of "
        "\\texttt{X}'s grows by one each round, this consumes $1+2+\\cdots+n$ "
        "\\texttt{b}'s total."
    ),
    "ezra-fader": (
        "A base-10 digit-counting machine. Uses 36 states with names like "
        "\\texttt{1a\\_counted} through \\texttt{9a\\_counted} to track the "
        "current \\texttt{a}-count modulo~9 within the state itself. For each "
        "counted \\texttt{a}, crosses off a batch of \\texttt{b}'s whose size "
        "corresponds to the current digit value. This approach yields fewer "
        "total steps than the simpler sweep-based machines."
    ),
    "julian-loutzenhiser": (
        "Shifts input past a \\texttt{\\$} left marker. Each round $k$ marks "
        "one \\texttt{a} as \\texttt{x}, then enters a nested loop that sweeps "
        "right to find an uncrossed \\texttt{b}, crosses it off, and returns "
        "to a reset state --- repeating $k$ times by re-scanning the \\texttt{x} "
        "markers. Accepts when all \\texttt{a}'s and \\texttt{b}'s are consumed."
    ),
    "kai-fagundes": (
        "Massive precomputed lookup table with 1.6M~states using base-62 "
        "encoded state names (\\texttt{c1}\\ldots\\texttt{cZ}, \\texttt{c10}\\ldots). "
        "Reads the input character by character, transitioning through states "
        "that track the exact count of \\texttt{a}'s and \\texttt{b}'s seen so "
        "far. Accepts or rejects based purely on the final state reached. "
        "Very fast in steps (max 1,681) but enormous in machine size."
    ),
    "minji-kang": (
        "Uses a \\texttt{|} separator to create a scratch region on the right "
        "side of the tape. Each round marks one \\texttt{a} and appends a "
        "\\texttt{1} tick mark to the scratch region. Then matches each tick "
        "against one \\texttt{b}, marking matched ticks as \\texttt{T}. "
        "Passes 22 of 41 public test cases; failures may stem from incorrect "
        "handling of certain input lengths."
    ),
    "sebastian-porto": (
        "Employs an extensive set of tape markers "
        "(\\texttt{S}, \\texttt{V}, \\texttt{Q}, \\texttt{P}, \\texttt{X}, "
        "\\texttt{Y}, \\texttt{Z}) and uses wildcard \\texttt{?} transitions "
        "to handle multiple symbols with shared logic. The algorithm shifts "
        "and marks symbols in phases to verify the triangular relationship. "
        "Passes 40 of 41 public test cases."
    ),
    "tristan-skerritt": (
        "Uses an \\texttt{h} marker to track the current head position in the "
        "\\texttt{a}-region and multiple crossing-off markers "
        "(\\texttt{p}, \\texttt{q}, \\texttt{r}, \\texttt{u}). Each round "
        "advances the \\texttt{h} marker one position and crosses off one "
        "additional \\texttt{b} compared to the previous round, with "
        "conversion states that transform markers between rounds."
    ),
}


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

\subsection{Step Counting}

All machines use the standard single-tape TM model with a left-bounded tape
(Sipser convention). A \textbf{step} is one application of the transition
function:
\[
\delta(q, a) = (q', b, D)
\]
where $q$ is the current state, $a$ is the symbol under the head, $q'$ is the
next state, $b$ is the symbol written, and $D \in \{L, R\}$ is the head
movement direction.

\medskip
\noindent\textbf{Example.} Suppose the machine is in configuration
$(q_3,\; \texttt{a\,b\,\_},\; 1)$---state $q_3$, tape \texttt{ab\_}, head
at position~1 (over~\texttt{b}). If $\delta(q_3, \texttt{b}) = (q_5,
\texttt{X}, R)$, then one step:
\begin{enumerate}
  \item Writes \texttt{X} at position 1, producing tape \texttt{aX\_}.
  \item Moves the head right to position 2.
  \item Transitions to state $q_5$.
\end{enumerate}
The resulting configuration is $(q_5,\; \texttt{a\,X\,\_},\; 2)$.
This single application of $\delta$ counts as \textbf{one step}.

\medskip
\noindent\textbf{Formal definition.}
The \emph{initial configuration} on input $w$ is $(q_0,\; w\sqcup^\omega,\; 0)$:
start state $q_0$, input $w$ written on the tape with blanks extending to the
right, head at position~0. The step count $\mathrm{steps}(M, w)$ is the number
of $\delta$ applications from the initial configuration until $M$ enters
$q_{\mathrm{accept}}$ or $q_{\mathrm{reject}}$.

\medskip
\noindent\textbf{Example.} A TM that decides $\{a\}$ (accepts only the
single-character string \texttt{a}):
\begin{center}
\begin{tabular}{llll}
\toprule
Input $w$ & Initial config & Computation & Steps \\
\midrule
\texttt{a} & $(q_0, \texttt{a\_}, 0)$ &
  $\delta(q_0, \texttt{a}) \to (q_1, \texttt{a}, R)$,\;
  $\delta(q_1, \texttt{\_}) \to (q_{\mathrm{acc}}, \texttt{\_}, R)$ & 2 \\
\texttt{aa} & $(q_0, \texttt{aa\_}, 0)$ &
  $\delta(q_0, \texttt{a}) \to (q_1, \texttt{a}, R)$,\;
  $\delta(q_1, \texttt{a}) \to (q_{\mathrm{rej}}, \texttt{a}, R)$ & 2 \\
$\varepsilon$ & $(q_0, \texttt{\_}, 0)$ &
  $\delta(q_0, \texttt{\_}) \to (q_{\mathrm{rej}}, \texttt{\_}, R)$ & 1 \\
\bottomrule
\end{tabular}
\end{center}

\noindent Note that the step count does not include the initial configuration
itself---only the transitions that follow it.

\subsection{Scoring}

Machines were evaluated on a combined test suite of 123 cases:
41 public cases ($n = 0$ to $39$, testing both accept and reject inputs)
and 82 stress-test cases ($n$ up to $3{,}400$, exponentially spaced).
Each test has a per-case step limit of $86 \times 10^9$ steps.

\medskip
\noindent A test case is \textbf{completed} if the machine halts (reaches
$q_{\mathrm{accept}}$ or $q_{\mathrm{reject}}$) within the step limit.
A test case that exceeds the step limit is \textbf{timed out}; the machine
is not credited with completing it, and all subsequent cases in that fixture
are also marked as timed out.

\medskip
\noindent Machines are ranked by:
\begin{enumerate}
  \item \textbf{Tests completed} (descending): the total number of test cases
        on which the machine halted within the step limit.
  \item \textbf{Average steps on completed tests} (ascending, tie-breaker):
        among the completed tests, the machine with fewer average steps ranks
        higher.
\end{enumerate}
Formally, let $C(M) \subseteq S$ be the set of test cases completed by
machine $M$. The ranking key is:
\[
\bigl(-|C(M)|,\;\; \mathrm{avg\text{-}steps}(M)\bigr)
\quad\text{where}\quad
\mathrm{avg\text{-}steps}(M) = \frac{1}{|C(M)|}\sum_{w \in C(M)} \mathrm{steps}(M, w)
\]
Machines are grouped into \textbf{tiers} by $|C(M)|$. Within each tier,
machines are sorted by average steps on completed tests.

"""


def methodology():
    return r"""
\section{Methodology}

\subsection{TM Model}

All submissions implement deterministic, single-tape Turing machines with:
\begin{itemize}
  \item A finite state set $Q$ with designated $q_0$, $q_{\mathrm{accept}}$,
        $q_{\mathrm{reject}}$.
  \item Input alphabet $\Sigma = \{a, b\}$ and a tape alphabet
        $\Gamma \supseteq \Sigma \cup \{\sqcup\}$ that may include additional
        marker symbols.
  \item A left-bounded tape (Sipser convention): the head cannot move left of
        position~0. If a transition specifies $L$ while the head is at
        position~0, the head remains at position~0.
  \item A transition function
        $\delta : Q' \times \Gamma \to Q \times \Gamma \times \{L, R\}$
        where $Q' = Q \setminus \{q_{\mathrm{accept}}, q_{\mathrm{reject}}\}$.
\end{itemize}

\subsection{Test Suite}

The combined test suite contains 123 inputs divided into two fixtures.

\paragraph{Public suite (41 cases).}
Inputs with $n = 0$ to $39$ (not every $n$ value), including 13 accept
cases ($m = T(n)$) and 28 reject cases ($m \neq T(n)$). All reject
inputs consist of $a^n b^m$ with an incorrect count of $b$'s---no
interleaving, no extraneous symbols. The suite includes the edge case
$\varepsilon$ ($n = 0$, accept). The largest input has $|w| = 1{,}508$.

\paragraph{Large suite (82 cases).}
For each of 41 values of $n$ exponentially spaced from 1 to $3{,}400$, one
accept input ($a^n b^{T(n)}$) and one reject input
($a^n b^{T(n) \pm \lfloor n/2 \rfloor}$). The sign of the offset is
deterministic and fixed. The largest accept input has
$n = 3{,}400$ and $|w| = 3{,}400 + T(3{,}400) = 5{,}783{,}400$.

\subsection{Simulator}

Machines were simulated using a flat transition table compiled from each
submission's YAML specification. The transition function is stored as a
contiguous array indexed by $(\text{state\_id} \times |\Gamma| +
\text{symbol\_id})$, giving $O(1)$ lookup per step with no hash table
overhead. The simulator achieves approximately 300 million steps per second
on a single core (Apple M-series, no parallelism within a simulation).

The tape is represented as a dynamically-resizing byte array with 4{,}096
cells of initial padding. If the head reaches the end, the tape doubles.

\subsection{Benchmarking Procedure}

Each submission was run against both fixtures using the following protocol:
\begin{enumerate}
  \item Load the TM from its YAML file and compile the flat transition table.
  \item For each test input in order:
    \begin{enumerate}
      \item Run the simulator with a per-case step limit of
            $86 \times 10^9$ steps.
      \item Record the step count and whether the machine accepted or rejected.
      \item If the machine halts within the step limit, the test is marked
            \textbf{completed} and its step count is recorded.
      \item If the machine exceeds the step limit, the test and \emph{all
            subsequent cases} are marked as \textbf{timed out}. Timed-out
            cases do not contribute to the completed step count.
    \end{enumerate}
  \item Report per-student totals: completed count, timed-out count,
        passed (correct) count, and step counts for completed tests only.
\end{enumerate}

All 12 submissions were benchmarked in parallel (one thread per submission)
with a 5-minute wall-clock timeout per submission.

\subsection{De-identification}

Submissions are identified by letter (Machine A through L) based on
alphabetical ordering of student names. The mapping is not included in this
report.

"""


def leaderboard(data_list, mapping):
    """Generate the leaderboard table.

    Machines are ranked by:
      1. Number of tests completed (halted within step limit), descending
      2. Average steps on completed tests, ascending (tie-breaker)
    """
    total_cases = data_list[0]["_total_cases"] if data_list else 0

    def sort_key(d):
        avg = d["_completed_steps"] / d["_completed"] if d["_completed"] > 0 else float("inf")
        return (-d["_completed"], avg)

    ranked = sorted(data_list, key=sort_key)

    lines = [r"\section{Leaderboard}", ""]
    lines.append(r"\begin{table}[H]")
    lines.append(r"\centering")
    lines.append(r"\small")
    lines.append(r"\begin{tabular}{r l r r r r r}")
    lines.append(r"\toprule")
    lines.append(f"Rank & Machine & States & Transitions & "
                 f"Completed & Avg Steps & Correct \\\\")
    lines.append(r"\midrule")

    prev_completed = None
    for i, d in enumerate(ranked):
        machine_id = f"Machine {mapping[d['student']]}"
        completed = d["_completed"]
        avg = d["_completed_steps"] / completed if completed > 0 else 0
        passed_str = f"{d['_passed']}/{d['_total_cases']}"

        # Add separator between completion tiers
        if prev_completed is not None and completed != prev_completed:
            lines.append(r"\midrule")
        prev_completed = completed

        row_prefix = ""
        if i == 0:
            row_prefix = r"\rowcolor{winnerrow}"
        elif d["_passed"] < d["_total_cases"]:
            row_prefix = r"\rowcolor{failrow}"

        lines.append(
            f"{row_prefix}"
            f"{i + 1} & {machine_id} & "
            f"{format_number(d['states'])} & {format_number(d['transitions'])} & "
            f"{completed}/{total_cases} & "
            f"{format_number(int(avg))} & "
            f"{passed_str} \\\\"
        )

    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    lines.append(r"\caption{All submissions ranked by tests completed within "
                 r"the step limit, with average steps on completed tests as "
                 r"tie-breaker. "
                 r"\colorbox{winnerrow}{Green} = winner. "
                 r"\colorbox{failrow}{Orange} = did not pass all test cases.}")
    lines.append(r"\end{table}")
    lines.append("")

    # Winner announcement
    winner = ranked[0]
    winner_id = f"Machine {mapping[winner['student']]}"
    avg = winner["_completed_steps"] / winner["_completed"] if winner["_completed"] > 0 else 0
    lines.append(f"\\noindent\\textbf{{Winner: {winner_id}}} completing all "
                 f"{total_cases} tests with an average of "
                 f"{format_number(int(avg))} steps per completed test "
                 f"using {format_number(winner['states'])} states and "
                 f"{format_number(winner['transitions'])} transitions.")
    lines.append("")

    return "\n".join(lines)


def comparative_analysis(data_list, mapping):
    """Generate scatter plot and growth curves."""
    # "passing" = completed all tests within step limit
    total_cases = data_list[0]["_total_cases"] if data_list else 0
    passing = [d for d in data_list if d["_completed"] == total_cases]
    passing.sort(key=lambda d: d["_completed_steps"])

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
        cls = "pass" if d["_completed"] == total_cases else "fail"
        mid = mapping[d["student"]]
        steps = d["_completed_steps"] if d["_completed"] > 0 else 1
        lines.append(f"  ({d['states']}, {steps}) [{cls}] %% {mid}")
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
        hw_sorted = sorted(handwritten, key=lambda d: d["_completed_steps"])
        hw_names = ", ".join(f"Machine {mapping[d['student']]}" for d in hw_sorted)
        best = hw_sorted[0]
        worst = hw_sorted[-1]
        ratio = worst['_completed_steps'] // best['_completed_steps'] if best['_completed_steps'] > 0 else 0
        lines.append(f"\\textbf{{Hand-written machines}} ({hw_names}) "
                     f"use compact representations (11--36 states) but require "
                     f"more steps on larger inputs. "
                     f"Machine {mapping[best['student']]} leads this group "
                     f"with {format_number(best['_completed_steps'])} total steps "
                     f"on completed tests, "
                     f"roughly {ratio}$\\times$ "
                     f"fewer than Machine {mapping[worst['student']]} "
                     f"({format_number(worst['_completed_steps'])} total steps).")
        lines.append("")

    # Check for identical machines
    step_groups = {}
    for d in passing:
        key = d["_completed_steps"]
        step_groups.setdefault(key, []).append(d)
    for steps, group in step_groups.items():
        if len(group) > 1:
            names = " and ".join(f"Machine {mapping[d['student']]}" for d in group)
            lines.append(f"\\textbf{{Identical behavior:}} {names} "
                         f"produced the exact same step count on every test case, "
                         f"suggesting functionally equivalent machines.")
            lines.append("")

    # Machines that did not complete all tests
    incomplete = [d for d in data_list if d["_completed"] < total_cases]
    if incomplete:
        incomplete.sort(key=lambda d: -d["_completed"])
        lines.append(r"\textbf{Incomplete machines:}")
        lines.append(r"\begin{itemize}")
        for d in incomplete:
            mid = mapping[d["student"]]
            avg = d["_completed_steps"] / d["_completed"] if d["_completed"] > 0 else 0
            lines.append(
                f"\\item Machine {mid}: completed {d['_completed']}/{total_cases} "
                f"tests ({d['_timed_out']} timed out), "
                f"{d['_passed']}/{d['_total_cases']} correct, "
                f"avg {format_number(int(avg))} steps on completed tests")
        lines.append(r"\end{itemize}")
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
    avg_completed = d["_completed_steps"] / d["_completed"] if d["_completed"] > 0 else 0
    lines.append(r"\begin{tabular}{ll}")
    lines.append(f"States & {format_number(d['states'])} \\\\")
    lines.append(f"Transitions & {format_number(d['transitions'])} \\\\")
    lines.append(f"Tests completed & {d['_completed']}/{d['_total_cases']} \\\\")
    lines.append(f"Correct & {d['_passed']}/{d['_total_cases']} \\\\")
    lines.append(f"Avg steps (completed) & {format_number(int(avg_completed))} \\\\")
    lines.append(f"Max steps (single case) & {format_number(d['_max_steps'])} \\\\")
    lines.append(r"\end{tabular}")
    lines.append(r"\medskip")
    lines.append("")

    # Algorithm summary
    summary = MACHINE_SUMMARIES.get(d["student"])
    if summary:
        lines.append(summary)
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
    parts.append(methodology())
    parts.append(leaderboard(data_list, mapping))
    parts.append(comparative_analysis(data_list, mapping))

    # Machine detail pages
    parts.append(r"\section{Machine Details}")
    parts.append("")
    parts.append("Each machine is shown with space-time diagrams on small inputs. "
                 "Columns represent tape positions, rows represent time steps (top to bottom). "
                 "Black dots mark the head position.")
    parts.append("")

    # Sort by leaderboard rank: completed desc, then avg completed steps asc
    def detail_sort_key(d):
        avg = d["_completed_steps"] / d["_completed"] if d["_completed"] > 0 else float("inf")
        return (-d["_completed"], avg)

    for d in sorted(data_list, key=detail_sort_key):
        parts.append(machine_detail_page(d, mapping, data_list))

    parts.append(r"\end{document}")
    return "\n".join(parts)


def load_csv_results(csv_paths):
    """Load and combine competition data from one or more CSV files.

    Each CSV has: student,states,transitions,passed,failed,total_steps,max_steps,
                  completed,timed_out,completed_steps
    Multiple CSVs are combined per student (fields summed across CSVs,
    max_steps takes the max, states/transitions from first occurrence).
    """
    combined = {}
    for path in csv_paths:
        with open(path) as f:
            reader = csv.DictReader(f)
            for row in reader:
                name = row["student"]
                if name not in combined:
                    combined[name] = {
                        "student": name,
                        "states": int(row["states"]),
                        "transitions": int(row["transitions"]),
                        "_passed": 0,
                        "_failed": 0,
                        "_total_steps": 0,
                        "_max_steps": 0,
                        "_total_cases": 0,
                        "_completed": 0,
                        "_timed_out": 0,
                        "_completed_steps": 0,
                    }
                combined[name]["_passed"] += int(row["passed"])
                combined[name]["_failed"] += int(row["failed"])
                combined[name]["_total_steps"] += int(row["total_steps"])
                combined[name]["_max_steps"] = max(
                    combined[name]["_max_steps"], int(row["max_steps"]))
                combined[name]["_total_cases"] += (
                    int(row["passed"]) + int(row["failed"]))
                combined[name]["_completed"] += int(row.get("completed", row["passed"]))
                combined[name]["_timed_out"] += int(row.get("timed_out", row["failed"]))
                combined[name]["_completed_steps"] += int(
                    row.get("completed_steps", row["total_steps"]))
    return list(combined.values())


def load_trace_data(trace_dir):
    """Load trace JSON files from a directory. Returns {student_name: trace_dict}."""
    traces = {}
    if not os.path.isdir(trace_dir):
        return traces
    for path in sorted(glob.glob(os.path.join(trace_dir, "*.json"))):
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            with open(path) as f:
                traces[name] = json.load(f)
        except (json.JSONDecodeError, OSError) as e:
            print(f"  WARNING: Could not load {path}: {e}", file=sys.stderr)
    return traces


def find_latest_results_dir():
    """Find the most recent results/<timestamp>/ directory."""
    results_root = os.path.join(ROOT, "results")
    if not os.path.isdir(results_root):
        return None
    # Look for timestamped directories (YYYY-MM-DD_HHMMSS)
    dirs = sorted(
        [d for d in os.listdir(results_root)
         if os.path.isdir(os.path.join(results_root, d)) and len(d) >= 10],
        reverse=True,
    )
    if dirs:
        return os.path.join(results_root, dirs[0])
    return None


def main():
    parser = argparse.ArgumentParser(description="Generate HW3A competition report from saved results")
    parser.add_argument("--output-dir",
                        help="Output directory for LaTeX files (default: most recent results/<timestamp>/)")
    parser.add_argument("--results-csv", action="append", default=[],
                        help="CSV file(s) with competition results (can be repeated)")
    args = parser.parse_args()

    # Auto-discover if no CSVs specified
    if not args.results_csv:
        latest = find_latest_results_dir()
        if not latest:
            print("Error: No results directories found in results/", file=sys.stderr)
            print("Run run_benchmarks.py first, or specify --results-csv explicitly.", file=sys.stderr)
            sys.exit(1)
        print(f"Using most recent results: {latest}")
        args.results_csv = sorted(glob.glob(os.path.join(latest, "*.csv")))
        if not args.results_csv:
            print(f"Error: No CSV files found in {latest}", file=sys.stderr)
            sys.exit(1)
        if not args.output_dir:
            args.output_dir = latest

    if not args.output_dir:
        args.output_dir = os.path.join(ROOT, "reports")

    # Load competition data from CSVs
    print("Loading competition data from CSV:")
    for path in args.results_csv:
        print(f"  {path}")
    data_list = load_csv_results(args.results_csv)
    print(f"  {len(data_list)} students loaded\n")

    if not data_list:
        print("Error: No data in CSV files", file=sys.stderr)
        sys.exit(1)

    # Normalize _total_cases: all students face the same tests, so use the max
    # (handles machines that crashed/timed out before producing a summary)
    max_cases = max(d["_total_cases"] for d in data_list)
    for d in data_list:
        if d["_total_cases"] == 0:
            d["_total_cases"] = max_cases
            d["_failed"] = max_cases
            d["_timed_out"] = max_cases

    # Load trace data from <output-dir>/traces/*.json
    trace_dir = os.path.join(args.output_dir, "traces")
    trace_data = load_trace_data(trace_dir)
    if trace_data:
        print(f"Loaded traces for {len(trace_data)} students from {trace_dir}\n")
        for d in data_list:
            name = d["student"]
            if name in trace_data:
                d["traces"] = trace_data[name].get("traces", [])
                d["results"] = trace_data[name].get("results", [])
    else:
        print(f"No trace data found in {trace_dir} (run run_benchmarks.py to generate)\n")

    # Create mapping
    mapping = create_mapping(data_list)

    # Generate LaTeX
    print("Generating LaTeX...")
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

    # Compile PDF
    print("\nCompiling PDF...")
    result = subprocess.run(
        ["pdflatex", "-interaction=nonstopmode", "hw3a_report.tex"],
        cwd=args.output_dir, capture_output=True, text=True,
    )
    if result.returncode == 0:
        # Second pass for references
        subprocess.run(
            ["pdflatex", "-interaction=nonstopmode", "hw3a_report.tex"],
            cwd=args.output_dir, capture_output=True, text=True,
        )
        pdf_path = os.path.join(args.output_dir, "hw3a_report.pdf")
        print(f"Wrote {pdf_path}")
    else:
        print("WARNING: pdflatex failed", file=sys.stderr)
        log_file = os.path.join(args.output_dir, "hw3a_report.log")
        if os.path.exists(log_file):
            with open(log_file) as f:
                lines = f.readlines()
                for line in lines[-20:]:
                    print(f"  {line.rstrip()}", file=sys.stderr)


if __name__ == "__main__":
    main()
