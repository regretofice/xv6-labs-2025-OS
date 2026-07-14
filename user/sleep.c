#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
// 头文件顺序不能错
int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: sleep <time>\n");
    exit(1);
  }
  pause(atoi(argv[1]));
  exit(0);
}