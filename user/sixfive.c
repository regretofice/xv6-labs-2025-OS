#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void sixfive(int fd) {
#define ST_separator 'a'
#define ST_number 'b'
#define ST_non_numeric 'c'
  char* separators = "-\r\t\n./,";  // 定义分隔符数组
  char c;
  long long cur_val = 0;
  char state = ST_non_numeric;

  while (read(fd, &c, 1) == 1) {
    if (strchr(separators, c) != 0) {
      if (state == ST_number && (cur_val % 5 == 0 || cur_val % 6 == 0)) {
        printf("%lld\n", cur_val);
      }
      state = ST_separator;
      cur_val = 0;
    } else if ('0' <= c && c <= '9') {
      state = ST_number;
      cur_val = cur_val * 10 + c - '0';
    } else {
      state = ST_non_numeric;
      cur_val = 0;
    }
  }

  // 有可能末尾没有分隔符
  if (state == ST_number) {
    if (cur_val % 5 == 0 || cur_val % 6 == 0) {
      printf("%lld\n", cur_val);
    }
  }
#undef ST_separator
#undef ST_number
#undef ST_non_numeric
}
int main(int argc, char** argv) {
  int i, fd;
  if (argc < 2) {  // 处理参数问题
    printf("Usage: sixfive <file>...");
    exit(1);
  }
  for (i = 1; i < argc; ++i) {
    fd = open(argv[i], O_RDONLY);
    if (fd < 0) {  // 有可能文件打开失败
      printf("can not open file: %s", argv[i]);
      exit(1);
    }
    sixfive(fd);
    close(fd);
  }
  exit(0);
}
//    ./grade-lab-util sixfive