#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
// 改编自ls.c
char* fmtname(char* path) {
  char* p;
  // 找到 / 后的完整文件名
  for (p = path + strlen(path); p >= path && *p != '/'; p--);
  p++;
  return p;
}
void call_exec(char* path, int exec_argc, char** exec_argv) {
  int pid = fork(), status;

  // 检查参数数量超过上限的问题
  if (exec_argc + 2 > MAXARG) {
    printf("find: too many arguments for exec\n");
    return;
  }

  // 子进程要调用exec
  if (pid == 0) {
    int argc;  // 需要将传入的参数与当前文件路径结合起来，构成新的argc,argv
    char* argv[MAXARG];
    for (int i = 0; i < exec_argc; ++i) argv[i] = exec_argv[i];
    argv[exec_argc] = path;
    argc = exec_argc + 1;
    argv[argc] = 0;  // 追加结束符
    exec(argv[0], argv);
    // 理论上只有报错了才会执行后面两步，否则exec会直接跳过这两步
    printf("find: exec %s error,path:%s\n", argv[0], path);
    exit(1);
  } else if (pid > 0) {
    wait(&status);  // 父进程需要等待子进程执行完毕
  } else {
    printf("find: fork error,path:%s\n", path);
  }
}
void find(char* path, char* file, int exec_argc, char** exec_argv) {
  int fd;
  struct stat st;  // inode的部分内容“视图”
  struct dirent
      de; /*一个目录文件，其内容就是由无数个连续的struct dirent组成的大数组*/
  char buf[512], *p;
  // 检验能否打开文件
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  // 查询对应的“视图”
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  switch (st.type) {
    case T_FILE: {
      // 如果当前文件名字与要找的文件名相等
      if (strcmp(fmtname(path), file) == 0) {
        if (exec_argc == 0) {  // 不需要执行额外行动，只需要输出文件地址
          printf("%s\n", path);
        } else {  // 有额外参数，要调用call_exec
          call_exec(path, exec_argc, exec_argv);
        }
      }
      break;
    }
    case T_DIR: {
      // 拼起来的路径太长了，无法处理
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        break;
      }

      strcpy(buf, path);
      p = buf + strlen(buf);
      *p++ = '/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)  // 这个目录槽位是空的（对应文件已被删除）
          continue;
        if (strcmp(de.name, ".") == 0 ||
            strcmp(de.name, "..") == 0)  // 两个特殊的文件夹名字就不考虑
          continue;

        // 拼接得到当前的路径
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        // 不需要检验子文件是否能够打开，递归进去会检验一次
        find(buf, file, exec_argc, exec_argv);
      }
      break;
    }
    default:
      break;
  }
  close(fd);
}
int main(int argc, char** argv) {
  if (argc < 3) {
    printf("Usage: find <path> <file> [-exec <cmd>...]\n");
    exit(1);
  }
  int exec_argc = 0;
  char** exec_argv = 0;
  if (argc > 3) {
    if (strcmp(argv[3], "-exec") != 0) {  // 判断第三个参数的合法性
      printf("find: syntax error, expected -exec\n");
    }
    exec_argc = argc - 4;  // 计算exec需要的参数的数量与完整参数列表
    exec_argv = &argv[4];
  }
  find(argv[1], argv[2], exec_argc, exec_argv);
  exit(0);
}
// ./grade-lab-util find