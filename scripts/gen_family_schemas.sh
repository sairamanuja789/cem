#!/usr/bin/env bash
# ADR-015 rule 3: regenerates schemas/products/<product>/v1/families/<family>.schema.json from each
# family's field catalogue. Run inside the dev container after building the gcc-debug preset.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool="${root}/build/${CEMKIT_PRESET:-gcc-debug}/kernel/testing/cemkit_gen_family_schemas"
[ -x "$tool" ] || { echo "build the ${CEMKIT_PRESET:-gcc-debug} preset first ($tool)" >&2; exit 2; }
"$tool" "$root" --write
