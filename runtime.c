#include <stdio.h>

void __inst_log_pair(const char *user, const char *operand) {
  printf("%s <- %s\n", user, operand);
}

__attribute__((constructor)) static void __inst_init(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
}
