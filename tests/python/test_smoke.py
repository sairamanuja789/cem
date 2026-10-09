import pytest

import cemkit


@pytest.mark.req("MAINT-002")
def test_package_imports_and_reports_a_version() -> None:
    assert cemkit.__version__ == "0.0.0"
