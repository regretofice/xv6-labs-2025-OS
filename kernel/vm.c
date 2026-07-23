#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) { kernel_pagetable = kvmmake(); }

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t* walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t* pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
#ifdef LAB_PGTBL
      // 这个判断叶子使得walk函数支持超级页的查询，但必须是已经存在的
      if (PTE_LEAF(*pte)) {
        return pte;
      }
#endif
    } else {
      if (!alloc || (pagetable = (pde_t*)kalloc()) == 0) return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}
// 在walk基础上新增一条信息，允许得知当前返回的PTE属于的层次为LEVEL
pte_t* superwalk(pagetable_t pagetable, uint64 va, int alloc, int* LEVEL) {
  if (va >= MAXVA) panic("superwalk");

  for (int level = 2; level > *LEVEL; level--) {
    pte_t* pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
      // 这个判断叶子使得walk函数支持超级页的查询，但必须是已经存在的
      // 通过LEVEL指针传递出当前的这个pte属于哪个层次
      if (PTE_LEAF(*pte)) {
        *LEVEL = level;
        return pte;
      }
    } else {
      if (!alloc || (pagetable = (pde_t*)kalloc()) == 0) return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(*LEVEL, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t* pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0) return 0;
  if ((*pte & PTE_V) == 0) return 0;
  if ((*pte & PTE_U) == 0) return 0;
  pa = PTE2PA(*pte);
  return pa;
}

#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)

void vmprint_base(pagetable_t pagetable, int level, uint64 base_va) {
  for (int i = 0; i < 512; ++i) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0) continue;  // 跳过不使用的节点

    // 获取虚拟地址与物理地址
    uint64 va = base_va | ((uint64)i << (12 + 9 * level));
    uint64 pa = PTE2PA(pte);
    for (int j = 3; j > level; --j) printf(" ..");
    printf("%p: pte %p pa %p\n", (void*)va, (void*)pte, (void*)pa);
    if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // 非叶节点继续递归
      vmprint_base((pagetable_t)pa, level - 1, va);
    }
    // 叶节点不需要执行更多操作了
  }
}

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  vmprint_base(pagetable, 2, 0);
};
#endif

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0) panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  uint64 a, pgsize;
  int LEVEL;
  pte_t* pte;

  // 只要求按 4KB 对齐(允许超级页在内部决定步长)
  if ((va % PGSIZE) != 0) panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0) panic("mappages: size not aligned");

  if (size == 0) panic("mappages: size");

  for (a = va; a < va + size; a += pgsize) {
    // 默认小页
    pgsize = PGSIZE;

    // 判断超级页的条件(此时 a < va+size，所以一定不会越界)
    if ((a % SUPERPGSIZE) == 0 &&
        (a + SUPERPGSIZE <= va + size) &&  // 剩余空间足够 2MB
        (perm & PTE_U)) {                  // 仅限用户映射
      pgsize = SUPERPGSIZE;
    }

    // 根据 pgsize 选择 walk 或 superwalk
    if (pgsize == PGSIZE)
      pte = walk(pagetable, a, 1);
    else {
      LEVEL = 1;
      pte = superwalk(pagetable, a, 1, &LEVEL);
    }
    if (pte == 0) return -1;
    if (*pte & PTE_V) panic("mappages: remap");

    *pte = PA2PTE(pa) | perm | PTE_V;
    pa += pgsize;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
// 辅助函数:将2MB超级页拆分为512个4KB小页，并替换原L1 PTE为指向新L0页表
static void split_superpage(pagetable_t pagetable, uint64 super_va,
                            pte_t super_pte) {
  uint64 pa = PTE2PA(super_pte);
  int perm = PTE_FLAGS(super_pte);

  // 分配一个新的 L0 页表页(4KB)
  pagetable_t l0 = (pagetable_t)kalloc();
  if (!l0) panic("split_superpage: kalloc");
  memset(l0, 0, PGSIZE);

  // 为 512 个 4KB 子页分配物理页，复制数据，填入 L0 页表
  for (int i = 0; i < 512; i++) {
    char* mem = kalloc();
    if (!mem) panic("split_superpage: no memory");
    memmove(mem, (char*)(pa + i * PGSIZE), PGSIZE);
    l0[i] = PA2PTE((uint64)mem) | perm | PTE_V;
  }

  // 找到指向该超级页的 L1 PTE，改为指向新 L0 页表(变成目录项)
  pte_t* l1_pte = walk(pagetable, super_va, 0);
  if (!l1_pte || !(*l1_pte & PTE_V) || PTE2PA(*l1_pte) != pa)
    panic("split_superpage: inconsistent");
  *l1_pte = PA2PTE((uint64)l0) | PTE_V;  // 现在 L1 变成内部节点，R/W/X 均为 0

  // 释放原来的 2MB 超级页物理内存
  ksuperfree((void*)pa);
}

// 解除映射并可选释放物理内存
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a, end = va + npages * PGSIZE;
  if ((va % PGSIZE) != 0) panic("uvmunmap: not aligned");

  for (a = va; a < end;) {
    int LEVEL = 0;
    pte_t* pte = superwalk(pagetable, a, 0, &LEVEL);
    if (!pte || !(*pte & PTE_V)) {
      a += PGSIZE;  // 跳过无效或未映射的地址
      continue;
    }
    if (PTE_FLAGS(*pte) == PTE_V) panic("uvmunmap: not a leaf");

    uint64 pa = PTE2PA(*pte);
    uint64 step = PGSIZE;  // 默认步长 4KB

    // 如果是超级页(LEVEL == 1)
    if (LEVEL == 1) {
      // 检查是否完全覆盖这个 2MB 大页
      if ((a & (SUPERPGSIZE - 1)) == 0 && (a + SUPERPGSIZE <= end)) {
        // 完全释放，直接处理
        step = SUPERPGSIZE;
        if (do_free) ksuperfree((void*)pa);
        *pte = 0;
        a += step;
        continue;
      } else {
        // 部分释放:拆分超级页为小页，然后继续(不前进 a)
        split_superpage(pagetable, SUPERPGROUNDDOWN(a), *pte);
        continue;
      }
    }

    // 普通 4KB 小页
    if (do_free) kfree((void*)pa);
    *pte = 0;
    a += PGSIZE;
  }
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  char* mem;
  uint64 a;
  int sz;

  if (newsz < oldsz) return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += sz) {
    sz = PGSIZE;
    // 在循环内部判断是否可以使用超级页,末尾21位都为0才行
    if (a + SUPERPGSIZE <= newsz && a % SUPERPGSIZE == 0) {
      sz = SUPERPGSIZE;
      mem = ksuperalloc();
    } else {
      mem = kalloc();
    }
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, sz);
    if (mappages(pagetable, a, sz, (uint64)mem, PTE_R | PTE_U | xperm) != 0) {
      // 如果分配的是超级页大小内存则释放超级页内存
      if (sz == SUPERPGSIZE) {
        ksuperfree(mem);
      } else {
        kfree(mem);
      }
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }

  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0) uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t* pte;
  uint64 pa, i;
  uint flags;
  char* mem;
  int pgsize;  // 当前循环的步长(4KB 或 2MB)
  int LEVEL = -1;

  // i的递增完全交给 for 循环的迭代器 i += pgsize
  for (i = 0; i < sz; i += pgsize) {
    // 默认按 4KB 处理，并且当前的父进程的页表有可能是普通页
    pgsize = PGSIZE;
    LEVEL = -1;

    // 需要得知父进程的页表级别
    if ((pte = superwalk(old, i, 0, &LEVEL)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0) panic("uvmcopy: page not present");

    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    // 判断父进程是否为超级页
    if (LEVEL == 1) {
      // 尝试给子进程分配超级页
      if ((mem = ksuperalloc()) != 0) {
        pgsize = SUPERPGSIZE;  // 步长设为 2MB
        memmove(mem, (char*)pa, pgsize);
        if (mappages(new, i, pgsize, (uint64)mem, flags) != 0) {
          ksuperfree(mem);
          goto err;
        }
        continue;  // 复制完成:i += pgsize(此时 pgsize==2MB)
      }
      // 如果 ksuperalloc() 返回 0，说明超级页池耗尽，不panic，而是降级为小页。
      // 注意:这里不修改 pgsize，它仍为 PGSIZE(默认值)
    }

    // --- 降级路径(小页)---
    // 无论原始 PTE 是大页还是小页，只要走到这里，都只复制 4KB
    if ((mem = kalloc()) == 0) goto err;

    // 根据父进程的页表类型进行差异化复制
    if (LEVEL == 1) {
      uint64 offset = i % SUPERPGSIZE;  // i 是虚拟地址，超级页起始 2MB 对齐
      memmove(mem, (char*)(pa + offset), PGSIZE);
    } else {
      // 普通小页
      memmove(mem, (char*)pa, PGSIZE);
    }
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
    // 循环末尾，for 迭代器执行 i += pgsize(此时 pgsize 仍为
    // PGSIZE)，正确前进 4KB
  }

  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t* pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char* src, uint64 len) {
  uint64 n, va0, pa0;
  pte_t* pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA) return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if ((pte = walk(pagetable, va0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }

    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0) return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void*)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char* dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void*)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char* dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max) n = max;

    char* p = (char*)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
  uint64 mem;
  struct proc* p = myproc();

  if (va >= p->sz) return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64)kalloc();
  if (mem == 0) return 0;
  memset((void*)mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void*)mem);
    return 0;
  }
  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va) {
  pte_t* pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V) {
    return 1;
  }
  return 0;
}

#ifdef LAB_PGTBL
pte_t* pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0);
}
#endif
