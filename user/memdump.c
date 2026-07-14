#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char* fmt, char* data);

int main(int argc, char* argv[]) {
  if (argc == 1) {
    printf("Example 1:\n");
    int a[2] = {61810, 2025};
    memdump("ii", (char*)a);

    printf("Example 2:\n");
    memdump("S", "a string");

    printf("Example 3:\n");
    char* s = "another";
    memdump("s", (char*)&s);

    struct sss {
      char* ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;

    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");

    printf("Example 4:\n");
    memdump("pihcS", (char*)&example);

    printf("Example 5:\n");
    memdump("sccccc", (char*)&example);
  } else if (argc == 2) {
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while (n < sizeof(data)) {
      int nn = read(0, data + n, sizeof(data) - n);
      if (nn <= 0) break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void memdump(char* fmt, char* data) {
  // Your code here.
  for (; *(fmt) != '\0'; ++fmt) {
    switch (*fmt) {
      case 'S': {
        // 完整输出剩余的字符
        printf("%s\n", data);
        return;
      }
      case 's': {
        // 接下来八个字节是一个指针，指针指向的才是要输出的字符串
        char* ptr_str = *(char**)data;  // 对指向char* 的指针解引用，好绕...
        printf("%s\n", ptr_str);
        data += 8;
        break;
      }
      case 'c': {
        // 输出一个字节的字符
        printf("%c\n", *data);
        ++data;
        break;
      }
      case 'h': {
        // 输出长度为2字节的short型十进制整数,原本应当是%hd的，xv6识别有问题
        printf("%d\n", *(short*)data);
        data += 2;
        break;
      }
      case 'p': {
        // 输出长度为8字节的longlong型十六进制整数
        printf("%llx\n", *(long long*)data);
        data += 8;
        break;
      }
      case 'i': {
        // 输出长度为4字节的int型十进制整数
        printf("%d\n", *(int*)data);
        data += 4;
        break;
      }

      default:
        break;
    }
  }
}

// ./grade-lab-util memdump
