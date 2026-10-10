/* cemkit C ABI (T11; MAINT-005, PERF-002). Stable boundary between the C++ kernel and every
 * caller (the Python binding cemkit._kernel first). Rules (kernel/CLAUDE.md, ADR-013):
 * - plain C: no C++ types, no exceptions cross this boundary;
 * - JSON (UTF-8) in and out; numbers in coherent SI units; the formats are in docs/capi.md;
 * - every call returns a cemkit_status; on CEMKIT_OK and CEMKIT_FAILED *out_json is a string the
 *   caller must release with cemkit_free (CEMKIT_FAILED: {"error": {...}} with the kernel's
 *   classified error); on CEMKIT_INVALID_ARGUMENT or CEMKIT_INTERNAL_ERROR *out_json may be NULL;
 * - the ABI is semantically versioned (CEMKIT_ABI_VERSION_*): a breaking change bumps MAJOR;
 * - every function is re-entrant and keeps no state between calls (thread-safe). */
#ifndef CEMKIT_CAPI_CEMKIT_H
#define CEMKIT_CAPI_CEMKIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CEMKIT_ABI_VERSION_MAJOR 0
#define CEMKIT_ABI_VERSION_MINOR 1
#define CEMKIT_ABI_VERSION_PATCH 0

typedef enum cemkit_status {
  CEMKIT_OK = 0,               /* *out_json holds the result */
  CEMKIT_FAILED = 1,           /* *out_json holds {"error": {...}}: a classified kernel error */
  CEMKIT_INVALID_ARGUMENT = 2, /* a NULL pointer argument; *out_json is untouched or NULL */
  CEMKIT_INTERNAL_ERROR = 3    /* a defect or out of memory; *out_json is an error or NULL */
} cemkit_status;

/* The ABI version this library implements (compare with the CEMKIT_ABI_VERSION_* the caller was
 * compiled against: same MAJOR, library MINOR >= caller MINOR). Any pointer may be NULL. */
void cemkit_abi_version(uint32_t* major, uint32_t* minor, uint32_t* patch);

/* The kernel version, "MAJOR.MINOR.PATCH". Static storage: do not free. */
const char* cemkit_kernel_version(void);

/* Compiles a spec (SPEC-001..011). Request: {"spec": <spec document>, "autonomous_mode": bool}. */
cemkit_status cemkit_spec_compile(const char* request_json, char** out_json);

/* Runs the L0 feasibility gate (SEL-004) for one duty. */
cemkit_status cemkit_feasibility(const char* request_json, char** out_json);

/* Evaluates a batch of L0 cases in one call (PERF-002): {"cases": [...]} -> {"results": [...]},
 * one result per case, in order. A case that fails carries its error; the batch still succeeds. */
cemkit_status cemkit_l0_batch(const char* request_json, char** out_json);

/* Builds the geometry test solid (GEO-001, GEO-002) and reports validity, topology, mass
 * properties and the SHA-256 and size of the requested exports (GEO-004). Writes no files. */
cemkit_status cemkit_geometry_smoke(const char* request_json, char** out_json);

/* Releases a string returned by this library. NULL is ignored. */
void cemkit_free(char* json);

#ifdef __cplusplus
}
#endif

#endif /* CEMKIT_CAPI_CEMKIT_H */
