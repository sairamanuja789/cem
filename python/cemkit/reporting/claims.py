"""Banned-claims check (REP-004; CLAUDE.md rule 9).

Every text cemkit outputs (CLI, reports) passes through check_claims before it is shown. A design or
result may not be called optimal, perfect, validated or certified unless the evidence the word
requires exists. The only evidence the system can hold today is an L3 validated result (a rig
measurement with uncertainty), so the single allowed use is the fidelity label "L3 validated" when
the caller states that such a result is being shown. Words are matched case-insensitively as whole
words, including in text quoted from a spec (a note saying "optimal" is refused, not reworded).
"""

from __future__ import annotations

import re

BANNED_WORDS = ("optimal", "perfect", "validated", "certified")
_WORD = re.compile(r"\b(" + "|".join(BANNED_WORDS) + r")\b", re.IGNORECASE)
_L3_LABEL = re.compile(r"\bL3 validated\b")


class BannedClaim(ValueError):
    """Output contained a banned claim; it was not shown."""

    def __init__(self, words: list[str]) -> None:
        super().__init__(f"output contains banned claims {words} (REP-004); nothing was printed")
        self.words = words


def banned_claims(text: str, *, l3_evidence: bool = False) -> list[str]:
    """The banned words in text, in order of appearance (lower-case)."""
    if l3_evidence:
        text = _L3_LABEL.sub("", text)
    return [match.group(1).lower() for match in _WORD.finditer(text)]


def check_claims(text: str, *, l3_evidence: bool = False) -> str:
    """Returns text unchanged, or raises BannedClaim."""
    words = banned_claims(text, l3_evidence=l3_evidence)
    if words:
        raise BannedClaim(words)
    return text
