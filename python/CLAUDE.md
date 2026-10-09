# Python rules (orchestration layer)

- Python 3.13 via uv. Type hints everywhere; `mypy --strict` and `ruff` must pass.
- No engineering physics in Python except `cemkit/reference/` (independent reference implementations
  used only by tests). Production physics lives in the C++ kernel.
- Call the kernel only through `cemkit._kernel` batch APIs; never loop over candidates in Python
  calling the kernel one at a time (PERF-002).
- All database access goes through `cemkit/store/`. Schema changes only via migrations (STORE-004).
- External processes (solvers, meshers) only through the orchestration runner, which applies memory,
  core and time limits (SIM-002, RES-001). Never call `subprocess` elsewhere.
- Records are immutable: frozen dataclasses or frozen pydantic v2 models.
- Logging: structured JSON lines via `cemkit/logging.py`; include peak RAM, CPU and wall time for jobs.
- Tests: pytest + Hypothesis. Mark requirement coverage with `@pytest.mark.req("ID")`.
- Secrets only from environment variables (SEC-001). Never log them.
