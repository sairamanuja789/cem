// MAINT-001 negative test: a deliberate heap-buffer overflow. Under the clang-asan preset the
// sanitizer must abort with "heap-buffer-overflow"; the ctest entry passes only when that message
// appears.
#include <cstddef>

int main() {
  constexpr std::size_t k_size = 4;
  int* data = new int[k_size];
  volatile std::size_t index = k_size;  // volatile: keep the optimizer from removing the access
  data[index] = 1;
  const int result = data[0];
  delete[] data;
  return result;
}
