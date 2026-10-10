/* MAINT-005: cemkit.h is plain C. Built as C11 with warnings as errors, linked against the kernel
 * and run as a ctest: it checks the ABI version and makes one call through the C interface. */
#include <stdio.h>
#include <string.h>

#include "cemkit/capi/cemkit.h"

int main(void) {
  uint32_t major = 99;
  uint32_t minor = 99;
  uint32_t patch = 99;
  char* out = NULL;
  cemkit_status status;
  int ok;

  cemkit_abi_version(&major, &minor, &patch);
  if (major != CEMKIT_ABI_VERSION_MAJOR || minor != CEMKIT_ABI_VERSION_MINOR ||
      patch != CEMKIT_ABI_VERSION_PATCH) {
    fprintf(stderr, "ABI version mismatch\n");
    return 1;
  }
  status = cemkit_l0_batch(
      "{\"cases\": [{\"id\": \"c\", \"function\": \"tip_speed\", "
      "\"args\": {\"omega\": 200.0, \"d_tip\": 0.1}}]}",
      &out);
  ok = status == CEMKIT_OK && out != NULL && strstr(out, "\"value\":10.0") != NULL;
  if (!ok) {
    fprintf(stderr, "unexpected response: %s\n", out != NULL ? out : "(null)");
  }
  cemkit_free(out);
  return ok ? 0 : 1;
}
