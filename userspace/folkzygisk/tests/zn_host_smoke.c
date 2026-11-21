#include <stdio.h>

#include "zn_api.h"

int main(void) {
  zn_load_modules();
  printf("smoke-done\n");
  return 0;
}
