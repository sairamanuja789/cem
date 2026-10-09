"""SEC-001 gate test: the secret scanner must detect a secret and stay quiet on clean files.

The fake secret is assembled at runtime from fragments and written only to a temporary directory,
so no secret-like string is ever committed to the repository.
"""

import json
import subprocess
from pathlib import Path

import pytest


def _scan(directory: Path, report: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "gitleaks",
            "dir",
            str(directory),
            "--no-banner",
            "--redact",
            "--report-format",
            "json",
            "--report-path",
            str(report),
        ],
        capture_output=True,
        text=True,
        check=False,
    )


@pytest.mark.req("SEC-001")
def test_gitleaks_detects_a_fake_secret(tmp_path: Path) -> None:
    # join() at runtime: constant folding would put the assembled string into the compiled .pyc
    fake_key = "".join(["AKIA", "Q3XZ7MVLP2", "NBHKD5"])
    scanned = tmp_path / "scanned"
    scanned.mkdir()
    (scanned / "settings.py").write_text(f'aws_access_key_id = "{fake_key}"\n')
    report = tmp_path / "report.json"
    result = _scan(scanned, report)
    assert result.returncode == 1, result.stdout + result.stderr
    findings = json.loads(report.read_text())
    assert [finding["RuleID"] for finding in findings] == ["aws-access-token"]


@pytest.mark.req("SEC-001")
def test_gitleaks_is_quiet_on_a_clean_file(tmp_path: Path) -> None:
    scanned = tmp_path / "scanned"
    scanned.mkdir()
    (scanned / "clean.py").write_text("answer = 42\n")
    result = _scan(scanned, tmp_path / "report.json")
    assert result.returncode == 0, result.stdout + result.stderr
