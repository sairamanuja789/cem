import pytest

import fancem


@pytest.mark.req("MAINT-002")
def test_package_imports_and_reports_a_version() -> None:
    assert fancem.__version__ == "0.0.0"
