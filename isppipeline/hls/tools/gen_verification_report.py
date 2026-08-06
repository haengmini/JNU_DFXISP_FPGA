#!/usr/bin/env python3
"""Generate a compact local DFXISP HLS verification report.

Stdlib-only. Inspects Makefile state, validates the new ``tests/golden_vectors.csv``
(shared baseline core + mutually exclusive tone RM slot), runs the local C-sim
binary when it exists, and writes Markdown to ``reports/latest.md`` by default.

The golden CSV carries per-case metadata (mode, selected RM, output shape) plus
input RAW rows (kind=raw) and expected output rows (kind=rgb).
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

DFXISP_MODE_LOW_LIGHT = 1
DFXISP_RM_NORMAL_TONE = 0
DFXISP_RM_LOW_LIGHT_TONE = 1
RM_NAME = {0: "RM_NORMAL_TONE", 1: "RM_LOW_LIGHT_TONE"}

EXPECTED_HEADER = ["case", "in_w", "in_h", "mode", "threshold",
                   "out_w", "out_h", "sel_mode", "sel_rm", "kind", "idx", "val"]


@dataclass
class GoldenCase:
    name: str
    in_w: int
    in_h: int
    mode: int
    threshold: int
    out_w: int
    out_h: int
    sel_mode: int
    sel_rm: int
    raw_rows: int = 0
    rgb_rows: int = 0


def parse_makefile(path: Path) -> tuple[dict[str, str], list[str]]:
    variables: dict[str, str] = {}
    targets: list[str] = []
    assign_re = re.compile(r"^([A-Za-z0-9_]+)\s*(?::=|\?=|=)\s*(.*)$")
    target_re = re.compile(r"^([A-Za-z0-9_.-]+)\s*:")
    if not path.exists():
        return variables, targets
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or raw.startswith("\t"):
            continue
        m = assign_re.match(line)
        if m:
            variables[m.group(1)] = m.group(2).strip()
            continue
        m = target_re.match(line)
        if m:
            targets.append(m.group(1))
    return variables, targets


def expand_make_value(value: str, variables: dict[str, str]) -> str:
    pattern = re.compile(r"\$\(([^)]+)\)")
    result = value
    for _ in range(8):
        new = pattern.sub(lambda m: variables.get(m.group(1), m.group(0)), result)
        if new == result:
            break
        result = new
    return result


def analyze_golden(path: Path) -> tuple[str, list[str], list[GoldenCase], int]:
    notes: list[str] = []
    cases: dict[str, GoldenCase] = {}
    total_rows = 0
    if not path.exists():
        return "missing", [f"{path} not found"], [], 0
    try:
        with path.open(newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            if reader.fieldnames != EXPECTED_HEADER:
                notes.append(f"unexpected CSV header: {reader.fieldnames}")
            for row in reader:
                total_rows += 1
                name = row["case"]
                c = cases.get(name)
                if c is None:
                    c = GoldenCase(name, int(row["in_w"]), int(row["in_h"]), int(row["mode"]),
                                   int(row["threshold"]), int(row["out_w"]), int(row["out_h"]),
                                   int(row["sel_mode"]), int(row["sel_rm"]))
                    cases[name] = c
                kind = row["kind"]
                if kind == "raw":
                    c.raw_rows += 1
                    if not (0 <= int(row["val"]) <= 4095):
                        notes.append(f"{name}: RAW {row['val']} outside RAW12 range")
                else:
                    c.rgb_rows += 1
                    if not (0 <= int(row["val"], 16) <= 0xFFFFFF):
                        notes.append(f"{name}: RGB {row['val']} outside RGB888 range")
    except Exception as exc:
        return "fail", [f"failed to parse {path}: {exc}"], [], total_rows

    golden = list(cases.values())
    for c in golden:
        if c.raw_rows != c.in_w * c.in_h:
            notes.append(f"{c.name}: {c.raw_rows} raw rows, expected {c.in_w * c.in_h}")
        if c.rgb_rows != c.out_w * c.out_h:
            notes.append(f"{c.name}: {c.rgb_rows} rgb rows, expected {c.out_w * c.out_h}")
        # Policy A: low-light halves shape; normal preserves shape.
        if c.sel_rm == DFXISP_RM_LOW_LIGHT_TONE:
            if (c.out_w, c.out_h) != (max(1, c.in_w // 2), max(1, c.in_h // 2)):
                notes.append(f"{c.name}: low-light output shape {c.out_w}x{c.out_h} not H/2 x W/2")
        elif (c.out_w, c.out_h) != (c.in_w, c.in_h):
            notes.append(f"{c.name}: normal output shape {c.out_w}x{c.out_h} != in shape")
        # mutual exclusion: selected RM must agree with resolved mode.
        want = DFXISP_RM_LOW_LIGHT_TONE if c.sel_mode == DFXISP_MODE_LOW_LIGHT else DFXISP_RM_NORMAL_TONE
        if c.sel_rm != want:
            notes.append(f"{c.name}: selected RM {c.sel_rm} inconsistent with mode {c.sel_mode}")

    names = {c.name for c in golden}
    for required in ["bright", "dark", "recovery", "odd_dimension"]:
        if not any(required in n for n in names):
            notes.append(f"missing required '{required}' golden-vector coverage")
    if not any(c.sel_rm == DFXISP_RM_LOW_LIGHT_TONE for c in golden):
        notes.append("no low-light tone RM case present")
    if not any(c.sel_rm == DFXISP_RM_NORMAL_TONE for c in golden):
        notes.append("no normal tone RM case present")

    status = "pass" if total_rows > 0 and not notes else "fail"
    return status, notes, golden, total_rows


def run_csim(binary: Path) -> tuple[str, str, int | None]:
    if not binary.exists():
        return "missing", f"{binary} not found", None
    if not os.access(binary, os.X_OK):
        return "fail", f"{binary} is not executable", None
    proc = subprocess.run([str(binary)], cwd=binary.parent.parent, text=True,
                          capture_output=True, check=False)
    output = (proc.stdout + proc.stderr).strip()
    status = "pass" if proc.returncode == 0 else "fail"
    return status, output, proc.returncode


def gate(ok: bool) -> str:
    return "PASS" if ok else "FAIL"


def write_report(root: Path, out: Path) -> None:
    makefile = root / "Makefile"
    variables, targets = parse_makefile(makefile)
    golden_rel = expand_make_value(variables.get("GOLDEN_CSV", "tests/golden_vectors.csv"), variables)
    csim_rel = expand_make_value(variables.get("CSIM_BIN", "build/dfxisp_csim"), variables)
    golden_path = root / golden_rel
    csim_path = root / csim_rel

    golden_status, golden_notes, golden_cases, total_rows = analyze_golden(golden_path)
    csim_status, csim_output, csim_returncode = run_csim(csim_path)

    # Architecture gates (RESEARCH.md §12 Task 4) hold when the golden structure is
    # clean AND the bit-exact C-sim (which asserts mode/RM/shape metadata) passes.
    structural_ok = golden_status == "pass"
    csim_ok = csim_status == "pass"
    has_normal = any(c.sel_rm == DFXISP_RM_NORMAL_TONE for c in golden_cases)
    has_low = any(c.sel_rm == DFXISP_RM_LOW_LIGHT_TONE for c in golden_cases)
    low_shapes_ok = all(
        (c.out_w, c.out_h) == (max(1, c.in_w // 2), max(1, c.in_h // 2))
        for c in golden_cases if c.sel_rm == DFXISP_RM_LOW_LIGHT_TONE
    )

    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    rel_out = out.relative_to(root) if out.is_relative_to(root) else out
    lines: list[str] = [
        "# DFXISP HLS Verification Report",
        "",
        f"Generated: {generated}",
        f"Report: `{rel_out}`",
        "",
        "Architecture (ver1): shared baseline core (demosaic+BLC+WB+CCM, 12-bit, no "
        "gain/gamma) + mutually exclusive tone RM slot (RM_NORMAL_TONE = gain 1.25x + "
        "gamma2.0 / RM_LOW_LIGHT_TONE = 2x2 bin + gain 2.0x + gamma2.0). See `RESEARCH.md` / `SPEC.md`.",
        "",
        "## Status",
        "",
        "| Check | Status | Evidence |",
        "|---|---:|---|",
        f"| Golden vectors | {golden_status.upper()} | `{golden_rel}`; {total_rows} data rows; {len(golden_cases)} cases |",
        f"| C-sim | {csim_status.upper()} | `{csim_rel}`; return code {csim_returncode if csim_returncode is not None else 'n/a'} |",
        "",
        "## Architecture gates",
        "",
        "| Gate | Status |",
        "|---|---:|",
        f"| Shared baseline core (bit-exact) | {gate(structural_ok and csim_ok)} |",
        f"| RM_NORMAL_TONE present | {gate(has_normal and csim_ok)} |",
        f"| RM_LOW_LIGHT_TONE present | {gate(has_low and csim_ok)} |",
        f"| Mutually exclusive RM selection | {gate(structural_ok and csim_ok)} |",
        f"| No duplicate gain/gamma (tone RM only) | {gate(structural_ok and csim_ok)} |",
        f"| Output shape policy: LOW_LIGHT H/2 x W/2 (Policy A), NORMAL H x W | {gate(low_shapes_ok and structural_ok)} |",
        "",
        "## Makefile state",
        "",
        f"- `CXX`: `{variables.get('CXX', 'g++')}`",
        f"- `PYTHON`: `{variables.get('PYTHON', 'python3')}`",
        f"- `CSIM_BIN`: `{csim_rel}`",
        f"- `GOLDEN_CSV`: `{golden_rel}`",
        f"- Targets include: `{', '.join(targets)}`",
        "",
        "## Golden vector cases",
        "",
        "| Case | Mode | Sel mode | Selected RM | In | Out |",
        "|---|---:|---:|---|---:|---:|",
    ]
    if golden_cases:
        for c in golden_cases:
            lines.append(f"| {c.name} | {c.mode} | {c.sel_mode} | {RM_NAME.get(c.sel_rm, c.sel_rm)} "
                         f"| {c.in_w}x{c.in_h} | {c.out_w}x{c.out_h} |")
    else:
        lines.append("| n/a | n/a | n/a | n/a | n/a | n/a |")

    lines.extend(["", "## C-sim output", "", "```text", csim_output or "(no output)", "```"])

    if golden_notes:
        lines.extend(["", "## Notes", ""])
        lines.extend(f"- {note}" for note in golden_notes)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out} (golden={golden_status}, csim={csim_status})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1], type=Path,
                        help="HLS root directory")
    parser.add_argument("--out", default=None,
                        help="output Markdown path (default: <root>/reports/latest.md)")
    args = parser.parse_args()
    root = args.root.resolve()
    out = Path(args.out).resolve() if args.out else root / "reports" / "latest.md"
    write_report(root, out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
