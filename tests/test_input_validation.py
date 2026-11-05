#!/usr/bin/env python3
"""Exercise input validation through the real standard and LM executables.

All fixtures derive from the committed synthetic small example. No private data
or external programs are needed. The suite never builds or edits the engine.

    python3 tests/test_input_validation.py --binary build/findhap

An optional --record writes the compact, machine-readable test outcome. Runtime
logs stay in temporary directories and are included only when a check fails.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Callable


@dataclass(frozen=True)
class InvalidCase:
    name: str
    change: Callable[[bytes], bytes]
    filename: str = "genotypes.txt"
    line: int = 1
    column: int | None = None
    always_fatal: bool = True


def identity(path: Path) -> dict:
    with path.open("rb") as stream:
        return {"bytes": path.stat().st_size,
                "sha256": hashlib.file_digest(stream, "sha256").hexdigest()}


def change_record(data: bytes, transform: Callable[[bytes], bytes], index=0) -> bytes:
    rows = data.splitlines(keepends=True)
    raw = rows[index]
    ending = b"\r\n" if raw.endswith(b"\r\n") else b"\n" if raw.endswith(b"\n") else b""
    content = raw[:-len(ending)] if ending else raw
    rows[index] = transform(content) + ending
    return b"".join(rows)


def field(data: bytes, start: int, value: bytes) -> bytes:
    assert len(value) == 10
    return change_record(data, lambda row: row[:start] + value + row[start + 10:])


def payload(data: bytes, offset: int, value: bytes, index=0) -> bytes:
    assert len(value) == 1
    return change_record(data, lambda row: row[:31 + offset] + value + row[32 + offset:], index)


def map_field(data: bytes, column: int, value: bytes) -> bytes:
    def transform(row):
        fields = row.split()
        fields[column] = value
        return b" ".join(fields)
    return change_record(data, transform, 1)


def location_present(message: str, filename: str, line: int, column: int | None) -> bool:
    """Permit either file:line:column or explicit line/column diagnostics."""
    for diagnostic in message.splitlines():
        if filename not in diagnostic:
            continue
        escaped = re.escape(filename)
        numeric = re.search(escaped + rf":{line}:(\d+)(?:\D|$)", diagnostic)
        if numeric and (column is None or int(numeric[1]) == column):
            return True
        if re.search(rf"\bline\s*[=:]?\s*{line}\b", diagnostic, re.I):
            col = re.search(r"\b(?:column|byte)\s*[=:]?\s*(\d+)\b", diagnostic, re.I)
            if col and (column is None or int(col[1]) == column):
                return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    example = Path(__file__).resolve().parents[1] / "examples" / "small"
    expected = json.loads((example / "expected.json").read_text())
    original = (example / "genotypes.txt").read_bytes()
    row_count = len(original.splitlines())
    assert int(original[20:30]) == 5000
    for filename, reference in expected["inputs"].items():
        assert identity(example / filename) == reference, f"Synthetic fixture changed: {filename}"

    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("OMP_", "GOMP_", "KMP_", "FINDHAP_", "TURBOHAP_"))}
    environment.update(OMP_NUM_THREADS="1", OMP_DYNAMIC="FALSE", LC_ALL="C")
    records = []

    def record(name, mode, category, **details):
        records.append(dict(case=name, mode=mode, category=category, passed=True, **details))
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps({"passed": None, "status": "in_progress",
                                              "checks": records}, indent=2) + "\n")
        print(f"PASS {category}: {name} ({mode})", flush=True)

    with tempfile.TemporaryDirectory(prefix="turbohap-input-validation-") as directory:
        base = Path(directory)
        counter = 0

        def setup(data=None, changed_file="genotypes.txt", sentinel=False):
            nonlocal counter
            counter += 1
            root = base / str(counter)
            root.mkdir()
            for filename in expected["inputs"]:
                shutil.copy2(example / filename, root / filename)
            if data is not None:
                (root / changed_file).write_bytes(data)
            if sentinel:
                # hap.Xseg is an output and is a prior only when hap.listpriors
                # is supplied. Neither prior library is installed in this case.
                for filename in expected["outputs"]:
                    (root / filename).write_bytes(b"PRESERVE EXISTING " + filename.encode() + b"\n")
            return root

        def invoke(root, mode, extra=()):
            command = [str(binary), "1", *(["--low-memory"] if mode == "lm" else []), *extra]
            result = subprocess.run(command, cwd=root, env=environment, capture_output=True,
                                    text=True, errors="replace", timeout=60)
            return result

        def inputs(root):
            return {name: identity(root / name) for name in expected["inputs"]}

        def outputs(root):
            return {name: identity(root / name) for name in expected["outputs"] if (root / name).exists()}

        def check_success(result, case):
            assert result.returncode == 0, (case, result.returncode, result.stdout[-3000:], result.stderr)
            assert "Program findhap completed normally" in result.stdout, (case, result.stdout[-3000:])

        valid = {
            "canonical-lf": original,
            "crlf": original.replace(b"\n", b"\r\n"),
            "no-final-newline": original.rstrip(b"\n"),
            "trailing-spaces": b"".join(row + b"   \n" for row in original.splitlines()),
            "partial-codes-3-and-4": payload(payload(original, 0, b"3"), 1, b"4"),
        }
        for name, data in valid.items():
            standard = None
            for mode in ("standard", "lm"):
                root = setup(data)
                before = inputs(root)
                result = invoke(root, mode)
                check_success(result, name)
                after = outputs(root)
                assert set(after) == set(expected["outputs"]), (name, mode, after)
                assert inputs(root) == before, (name, "input modification")
                if name != "partial-codes-3-and-4":
                    assert after == expected["outputs"], (name, mode, "legacy output mismatch")
                if mode == "standard":
                    standard = after
                else:
                    assert after == standard, (name, "standard/LM output mismatch")
                record(name, mode, "valid", raw_output_files=len(after))

        cases = [
            InvalidCase("extra-separator-space", lambda data: change_record(data, lambda row: row[:31] + b" " + row[31:]), always_fatal=False),
            InvalidCase("leading-payload-blank", lambda data: payload(data, 0, b" "), column=32, always_fatal=False),
            InvalidCase("internal-payload-blank", lambda data: payload(data, 40, b" "), column=72, always_fatal=False),
            InvalidCase("trailing-extra-digit", lambda data: change_record(data, lambda row: row + b"0"), column=5032, always_fatal=False),
            InvalidCase("truncated-payload", lambda data: change_record(data, lambda row: row[:-1])),
            InvalidCase("missing-separator", lambda data: change_record(data, lambda row: row[:30] + row[31:])),
            InvalidCase("short-header", lambda data: change_record(data, lambda row: row[:25])),
            InvalidCase("invalid-genotype-code-6", lambda data: payload(data, 0, b"6"), column=32),
            InvalidCase("invalid-genotype-letter", lambda data: payload(data, 0, b"X"), column=32),
            InvalidCase("payload-tab", lambda data: payload(data, 0, b"\t"), column=32),
            InvalidCase("payload-nul", lambda data: payload(data, 0, b"\0"), column=32),
            InvalidCase("header-letters", lambda data: field(data, 0, b"     1X001")),
            InvalidCase("header-overflow", lambda data: field(data, 0, b"9999999999")),
            InvalidCase("negative-animal-id", lambda data: field(data, 0, b"        -1")),
            InvalidCase("zero-animal-id", lambda data: field(data, 0, b"         0")),
            InvalidCase("blank-animal-id", lambda data: field(data, 0, b"          ")),
            InvalidCase("unknown-chip", lambda data: field(data, 10, b"         3")),
            InvalidCase("negative-chip", lambda data: field(data, 10, b"        -1")),
            InvalidCase("wrong-locus-count", lambda data: field(data, 20, b"      4999")),
            InvalidCase("negative-locus-count", lambda data: field(data, 20, b"        -1")),
            InvalidCase("locus-count-overflow", lambda data: field(data, 20, b"9999999999")),
            InvalidCase("late-invalid-record", lambda data: payload(data, 0, b"6", -1), line=row_count, column=32),
            InvalidCase("map-truncated-record", lambda data: change_record(data, lambda row: b" ".join(row.split()[:-1]), 1), filename="chromosome.data", line=2),
            InvalidCase("map-invalid-number", lambda data: map_field(data, 1, b"X"), filename="chromosome.data", line=2),
            InvalidCase("map-wrong-chip-columns", lambda data: map_field(data, 5, b"3"), filename="chromosome.data", line=2),
            InvalidCase("map-noncontiguous-chip-ordinal", lambda data: map_field(data, 6, b"2"), filename="chromosome.data", line=2),
        ]
        for case in cases:
            data = case.change((example / case.filename).read_bytes())
            policies = ("strict", "warn") if case.always_fatal else ("strict",)
            for policy in policies:
                for mode in ("standard", "lm"):
                    # Strict's default is tested here; one explicit strict case
                    # below checks parsing independently of the default.
                    extra = () if policy == "strict" else ("--input-validation=warn",)
                    for sentinel in (False, True):
                        root = setup(data, case.filename, sentinel)
                        before_inputs, before_outputs = inputs(root), outputs(root)
                        result = invoke(root, mode, extra)
                        diagnostic = result.stderr
                        assert result.returncode != 0, (case.name, mode, policy, "unexpected success")
                        assert "Program findhap completed normally" not in result.stdout
                        assert location_present(diagnostic, case.filename, case.line, case.column), (
                            case.name, mode, policy, "missing file/line/column", diagnostic)
                        assert inputs(root) == before_inputs, (case.name, "input modification")
                        assert outputs(root) == before_outputs, (case.name, "legacy output created/modified")
                        record(case.name, mode, "rejected", policy=policy,
                               existing_outputs_preserved=sentinel, exit_status=result.returncode)

        # A canonical encoding of the exact legacy-decoded values is an
        # independent differential oracle: warn must not silently realign data.
        warning_variants = {
            "extra-separator-space": (
                change_record(original, lambda row: row[:31] + b" " + row[31:]),
                change_record(original, lambda row: row[:31] + b"0" + row[31:-1])),
            "leading-payload-blank": (payload(original, 0, b" "), payload(original, 0, b"0")),
            "internal-payload-blank": (payload(original, 40, b" "), payload(original, 40, b"0")),
            "trailing-extra-digit": (change_record(original, lambda row: row + b"0"), original),
            "bounded-many-warnings": (
                b"".join(row[:31] + b" " + row[32:] + b"\n" for row in original.splitlines()),
                b"".join(row[:31] + b"0" + row[32:] + b"\n" for row in original.splitlines())),
        }
        for name, (malformed, legacy_values) in warning_variants.items():
            for mode in ("standard", "lm"):
                reference = setup(legacy_values)
                control = invoke(reference, mode, ("--input-validation=strict",))
                check_success(control, name + " canonical legacy-value oracle")
                root = setup(malformed)
                before = inputs(root)
                result = invoke(root, mode, ("--input-validation=warn",))
                check_success(result, name)
                assert "warn" in result.stderr.lower(), (name, "warning absent", result.stderr)
                assert "genotypes.txt" in result.stderr, (name, result.stderr)
                assert outputs(root) == outputs(reference), (name, mode, "legacy interpretation changed")
                assert inputs(root) == before, (name, "input modified")
                if name == "bounded-many-warnings":
                    assert str(row_count) in result.stderr, (name, "aggregate affected count absent", result.stderr)
                    assert len(result.stderr.splitlines()) < 40, (name, "unbounded diagnostics", result.stderr)
                record(name, mode, "warn-retains-legacy", raw_output_files=len(outputs(root)))

        for extra in (("--input-validation=warn", "1"), ("1", "--input-validation=strict")):
            root = setup()
            result = subprocess.run([str(binary), *extra], cwd=root, env=environment,
                                    capture_output=True, text=True, timeout=60)
            check_success(result, "CLI option position")
            assert outputs(root) == expected["outputs"]
            record(" ".join(extra), "standard", "cli-position")

        for flags in (("--input-validation",), ("--input-validation=",),
                      ("--input-validation=off",), ("--input-validation=none",),
                      ("--input-validation=WARN",), ("--input-validationX=warn",),
                      ("--input-validation=strict", "--input-validation=warn")):
            root = setup(sentinel=True)
            before = outputs(root)
            result = invoke(root, "standard", flags)
            assert result.returncode != 0, (flags, "malformed option accepted")
            assert "input-validation" in result.stderr, (flags, result.stderr)
            assert outputs(root) == before, (flags, "outputs changed")
            record(" ".join(flags), "standard", "cli-rejected")

        root = base / "help-without-inputs"
        root.mkdir()
        result = subprocess.run([str(binary), "--help"], cwd=root, env=environment,
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0
        for term in ("input-validation", "strict", "warn"):
            assert term in result.stdout, ("help missing", term)
        assert not list(root.iterdir()), "Help created files"
        record("help-documents-policy", "standard", "help")

    if args.record:
        args.record.write_text(json.dumps({"passed": True, "status": "complete",
                                          "checks": records}, indent=2) + "\n")
    print(f"PASS: {len(records)} input-validation checks; standard and LM, raw output parity, "
          "bounded legacy warnings and preservation of existing outputs")


if __name__ == "__main__":
    main()
