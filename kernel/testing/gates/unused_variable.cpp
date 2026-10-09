// MAINT-001 negative test: this file must NOT compile under the project warning flags
// (-Wall -Wextra -Werror). It proves the warnings-as-errors gate bites. Built only through the
// EXCLUDE_FROM_ALL target neg_unused_variable, whose failure is the expected result.
int main() {
  int never_used = 0;
  return 0;
}
