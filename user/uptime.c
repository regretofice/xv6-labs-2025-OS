#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc, char** argv) {
  if (argc != 1) {
    printf("Usage: uptime\n");
    exit(1);
  }
  printf("%d\n", uptime());
  exit(0);
}