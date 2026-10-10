// Test-data tool for the PHY-005 cross-check (ctest fans_l0_crosscheck, scripts/crosscheck_l0.py):
// reads a cemkit_l0_batch request ({"cases": [...]}) on stdin, evaluates it through the C ABI in
// one call and writes the response on stdout. Exit status 1 if the call does not return CEMKIT_OK.
#include <iostream>
#include <string>

#include "cemkit/capi/cemkit.h"

int main() {
  std::string request;
  for (std::string line; std::getline(std::cin, line);) {
    request += line;
    request += '\n';
  }
  char* response = nullptr;
  const cemkit_status status = cemkit_l0_batch(request.c_str(), &response);
  if (response != nullptr) {
    (status == CEMKIT_OK ? std::cout : std::cerr) << response << '\n';
  }
  cemkit_free(response);
  return status == CEMKIT_OK ? 0 : 1;
}
