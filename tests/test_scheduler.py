#!/usr/bin/env python3
"""
test_scheduler.py
─────────────────
Automation harness for the Mini Process Scheduler Simulator.

What it does
============
1. Runs the C scheduler binary with a set of test scenarios.
2. Parses the structured CSV log it produces.
3. Validates that:
   - Every state transition follows the defined FSM.
   - Every process eventually reaches TERMINATED.
   - No invalid transitions appear in the log.
4. Prints a colour-coded pass/fail report to stdout and writes
   a summary to logs/test_report.txt.

Usage
=====
    python3 tests/test_scheduler.py

The script must be run from the project root (where ./scheduler lives).
"""

import subprocess
import csv
import sys
import os
import time
from dataclasses import dataclass, field
from typing import List, Dict, Tuple

# ── Paths ────────────────────────────────────────────────────────────────────
BINARY   = "./scheduler"
LOG_DIR  = "logs"
REPORT   = os.path.join(LOG_DIR, "test_report.txt")

# ── FSM: valid transitions (same table as the C code) ────────────────────────
VALID_TRANSITIONS = {
    "NEW":        {"READY"},
    "READY":      {"RUNNING"},
    "RUNNING":    {"READY", "WAITING", "TERMINATED"},
    "WAITING":    {"READY"},
    "TERMINATED": set(),
}

# ── ANSI colours ─────────────────────────────────────────────────────────────
GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"
RESET  = "\033[0m"
BOLD   = "\033[1m"

def ok(msg):   return f"{GREEN}  ✓  {RESET}{msg}"
def fail(msg): return f"{RED}  ✗  {RESET}{msg}"
def warn(msg): return f"{YELLOW}  !  {RESET}{msg}"

# ── Data structures ──────────────────────────────────────────────────────────
@dataclass
class TransitionRecord:
    timestamp: str
    pid: str
    name: str
    old_state: str
    new_state: str
    priority: str
    remaining_ms: str

@dataclass
class TestResult:
    name: str
    passed: bool
    details: List[str] = field(default_factory=list)

# ── Log parser ────────────────────────────────────────────────────────────────
def parse_log(log_path: str) -> List[TransitionRecord]:
    """Return only state-transition rows (skip EVENT rows)."""
    records = []
    with open(log_path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["pid"] == "–" or row["old_state"] == "EVENT":
                continue
            records.append(TransitionRecord(
                timestamp    = row["timestamp"],
                pid          = row["pid"],
                name         = row["name"],
                old_state    = row["old_state"].strip(),
                new_state    = row["new_state"].strip(),
                priority     = row["priority"],
                remaining_ms = row["remaining_ms"],
            ))
    return records

# ── Validators ────────────────────────────────────────────────────────────────
def validate_fsm(records: List[TransitionRecord]) -> TestResult:
    """Every transition must be in VALID_TRANSITIONS."""
    result = TestResult(name="FSM Validity", passed=True)
    for r in records:
        allowed = VALID_TRANSITIONS.get(r.old_state, set())
        if r.new_state not in allowed:
            result.passed = False
            result.details.append(
                f"PID {r.pid} ({r.name}): illegal {r.old_state} → {r.new_state} at {r.timestamp}"
            )
    if result.passed:
        result.details.append(f"All {len(records)} transitions are valid.")
    return result

def validate_all_terminated(records: List[TransitionRecord],
                             expected_pids: List[str]) -> TestResult:
    """Every submitted process must reach TERMINATED."""
    result = TestResult(name="All Processes Terminate", passed=True)
    terminated = {r.pid for r in records if r.new_state == "TERMINATED"}
    for pid in expected_pids:
        if pid not in terminated:
            result.passed = False
            result.details.append(f"PID {pid} never reached TERMINATED")
    if result.passed:
        result.details.append(f"All {len(expected_pids)} processes terminated.")
    return result

def validate_no_new_after_start(records: List[TransitionRecord]) -> TestResult:
    """No process should transition TO NEW (it starts in NEW, never returns)."""
    result = TestResult(name="No Return to NEW", passed=True)
    for r in records:
        if r.new_state == "NEW":
            result.passed = False
            result.details.append(
                f"PID {r.pid} ({r.name}): transitioned back to NEW at {r.timestamp}"
            )
    if result.passed:
        result.details.append("No process returned to NEW state.")
    return result

def validate_sequence(records: List[TransitionRecord]) -> TestResult:
    """
    For each PID, reconstruct the state sequence and verify it forms
    a valid FSM path from NEW → ... → TERMINATED.
    """
    result = TestResult(name="Per-Process State Sequence", passed=True)
    # Group by PID
    by_pid: Dict[str, List[TransitionRecord]] = {}
    for r in records:
        by_pid.setdefault(r.pid, []).append(r)

    for pid, recs in by_pid.items():
        name = recs[0].name
        # Reconstruct state path
        states = [recs[0].old_state] + [r.new_state for r in recs]
        # Check first state is NEW
        if states[0] != "NEW":
            result.passed = False
            result.details.append(
                f"PID {pid} ({name}): first state is {states[0]}, expected NEW"
            )
        # Check last state is TERMINATED
        if states[-1] != "TERMINATED":
            result.passed = False
            result.details.append(
                f"PID {pid} ({name}): last state is {states[-1]}, expected TERMINATED"
            )
        # Check each step
        for i in range(len(states) - 1):
            src, dst = states[i], states[i+1]
            if dst not in VALID_TRANSITIONS.get(src, set()):
                result.passed = False
                result.details.append(
                    f"PID {pid} ({name}): invalid step {src} → {dst}"
                )
        if result.passed:
            result.details.append(
                f"PID {pid} ({name}): {' → '.join(states)}"
            )

    return result

def compute_stats(records: List[TransitionRecord],
                  log_path: str) -> Dict:
    """Parse EVENT lines for turnaround and wait times."""
    stats = {"turnaround": {}, "wait": {}}
    with open(log_path) as f:
        for line in f:
            if "[TERM]" in line:
                # e.g. "[TERM] PID 2 name=TaskB turnaround=350ms wait=50ms"
                parts = line.split()
                pid = name = ta = wt = None
                for p in parts:
                    if p.startswith("PID"):
                        pass
                    elif p.isdigit():
                        pid = p
                    elif p.startswith("name="):
                        name = p.split("=")[1]
                    elif p.startswith("turnaround="):
                        ta = p.split("=")[1]
                    elif p.startswith("wait="):
                        wt = p.split("=")[1]
                if pid and name:
                    stats["turnaround"][name] = ta or "?"
                    stats["wait"][name]        = wt or "?"
    return stats

# ── Test scenarios ────────────────────────────────────────────────────────────
SCENARIOS = [
    {
        "name":     "Round Robin — Mixed Priorities",
        "policy":   "rr",
        "log":      "logs/test_rr.log",
        "tasks":    ["TaskA:5:200", "TaskB:8:150", "TaskC:3:300", "TaskD:7:100"],
    },
    {
        "name":     "Priority — Highest First",
        "policy":   "prio",
        "log":      "logs/test_prio.log",
        "tasks":    ["LowPri:2:100", "MidPri:5:150", "HighPri:9:80"],
    },
    {
        "name":     "Single Process",
        "policy":   "rr",
        "log":      "logs/test_single.log",
        "tasks":    ["Solo:5:75"],
    },
    {
        "name":     "Many Short Tasks",
        "policy":   "prio",
        "log":      "logs/test_many.log",
        "tasks":    [f"T{i}:{i}:50" for i in range(1, 7)],
    },
]

# ── Runner ────────────────────────────────────────────────────────────────────
def run_scenario(scenario: dict) -> Tuple[bool, List[TestResult], Dict]:
    log_path = scenario["log"]
    cmd = [BINARY, scenario["policy"], log_path] + scenario["tasks"]

    # Run C binary
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    elapsed = time.time() - t0

    if proc.returncode != 0:
        return False, [TestResult(
            name="Binary Execution",
            passed=False,
            details=[f"Exit code {proc.returncode}", proc.stderr]
        )], {}

    # Parse
    records = parse_log(log_path)
    expected_pids = [str(i+1) for i in range(len(scenario["tasks"]))]

    results = [
        validate_fsm(records),
        validate_all_terminated(records, expected_pids),
        validate_no_new_after_start(records),
        validate_sequence(records),
    ]
    results[0].details.insert(0, f"Binary ran in {elapsed:.2f}s, {len(records)} transitions logged.")

    stats = compute_stats(records, log_path)
    all_passed = all(r.passed for r in results)
    return all_passed, results, stats

# ── Report ────────────────────────────────────────────────────────────────────
def print_report(all_scenarios, report_lines):
    print()
    print(f"{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  Mini Process Scheduler — Automated Test Report{RESET}")
    print(f"{BOLD}{'='*60}{RESET}")

    total_pass = total_fail = 0

    for (scenario, passed, results, stats) in all_scenarios:
        icon  = f"{GREEN}PASS{RESET}" if passed else f"{RED}FAIL{RESET}"
        print(f"\n{BOLD}Scenario: {scenario['name']}  [{icon}]{RESET}")
        print(f"  Policy: {scenario['policy'].upper()}   Tasks: {', '.join(scenario['tasks'])}")

        for res in results:
            status = ok(res.name) if res.passed else fail(res.name)
            print(f"  {status}")
            for d in res.details:
                indent = "      "
                print(f"{indent}{d}")

        if stats.get("turnaround"):
            print(f"  {'─'*40}")
            print(f"  {'Task':<12} {'Turnaround':>14} {'Wait':>10}")
            print(f"  {'─'*40}")
            for name in stats["turnaround"]:
                print(f"  {name:<12} {stats['turnaround'][name]:>14} {stats['wait'].get(name,'?'):>10}")

        if passed:
            total_pass += 1
        else:
            total_fail += 1

    print(f"\n{BOLD}{'='*60}{RESET}")
    colour = GREEN if total_fail == 0 else RED
    print(f"{colour}{BOLD}  Result: {total_pass} passed, {total_fail} failed{RESET}")
    print(f"{BOLD}{'='*60}{RESET}\n")

    # Write plain-text report
    with open(REPORT, "w") as f:
        f.write("\n".join(report_lines))
    print(f"  Report saved to {REPORT}\n")

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    os.makedirs(LOG_DIR, exist_ok=True)

    if not os.path.isfile(BINARY):
        print(f"{RED}Error: binary '{BINARY}' not found. Run 'make' first.{RESET}")
        sys.exit(1)

    collected = []
    report_lines = ["Mini Process Scheduler — Test Report", "=" * 50]

    for scenario in SCENARIOS:
        print(f"  Running: {scenario['name']} ...", end=" ", flush=True)
        try:
            passed, results, stats = run_scenario(scenario)
        except subprocess.TimeoutExpired:
            passed, results, stats = False, [
                TestResult("Timeout", False, ["Binary timed out after 30s"])
            ], {}
        except Exception as e:
            passed, results, stats = False, [
                TestResult("Exception", False, [str(e)])
            ], {}

        print("OK" if passed else "FAILED")
        collected.append((scenario, passed, results, stats))

        # Plain text for file
        report_lines.append(f"\n[{'PASS' if passed else 'FAIL'}] {scenario['name']}")
        for res in results:
            report_lines.append(f"  {'OK' if res.passed else 'FAIL'}: {res.name}")
            for d in res.details:
                report_lines.append(f"      {d}")

    print_report(collected, report_lines)

    # Exit with non-zero if any test failed
    if any(not p for (_, p, _, _) in collected):
        sys.exit(1)

if __name__ == "__main__":
    main()
