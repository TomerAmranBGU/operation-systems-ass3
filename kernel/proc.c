#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define next_local_idx(i) (i + 1) % MAX_PSYC_PAGES
#if SELECTION == LAPA
#define INIT_AGE_VALUE 0xFFFFFFFF
#elif SELECTION == NFUA
#define INIT_AGE_VALUE 0
#else
#define INIT_AGE_VALUE 0
#endif

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

int choose_some_page(struct proc *p)
{
  return 1;
}

int ones(uint number)
{
  int count_ones = 0;
  while (number)
  {
    count_ones += number & 1;
    number >>= 1;
  }
  return count_ones;
}

int nfu_algo(struct proc *p)
{
  int index_to_return = -1;
  int min_age = p->local_pages[0].age;
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (min_age >= p->local_pages[i].age)
    {
      min_age = p->local_pages[i].age;
      index_to_return = i;
    }
  }
  return index_to_return;
}

int lapa_algo(struct proc *p)
{
  int min_age = p->local_pages[0].age;
  int index_to_return = -1;
  int min_access = ones(p->local_pages[0].age);
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (min_access > ones(p->local_pages[i].age))
    {
      min_access = ones(p->local_pages[i].age);
      index_to_return = i;
      min_age = p->local_pages[i].age;
    }
    else if ((min_access == ones(p->local_pages[i].age)) && (min_age > p->local_pages[i].age))
    {
      min_access = ones(p->local_pages[i].age);
      index_to_return = i;
      min_age = p->local_pages[i].age;
    }
  }
  return index_to_return;
}

int scfifo_algo(struct proc *p)
{
  for (; p->scfifo_out_index < MAX_PSYC_PAGES; p->scfifo_out_index = next_local_idx(p->scfifo_out_index))
    {
        pte_t *pte = walk(p->pagetable, p->local_pages[p->scfifo_out_index].va, 0);
        if (*pte & PTE_A)
        {
            *pte &= ~PTE_A;
        }
        else
        {
            int index_to_return = p->scfifo_out_index;
            p->scfifo_out_index = next_local_idx(p->scfifo_out_index);
            return index_to_return;
        }
    }
    return -1;
}

int page_metadata_init(struct proc *p)
{
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    p->local_pages[i].va = 0;
    p->local_pages[i].state = PAGE_FREE;
    p->swap_pages[i].va = 0;
    p->swap_pages[i].state = PAGE_FREE;
  }
  return 0;
}

int page_to_swap(struct proc *p)
{
#if SELECTION == SCFIFO
  return scfifo_algo(p);
#endif
#if SELECTION == NFUA
  return nfua_algo(p);
#endif
#if SELECTION == LAPA
  return lapa_algo(p);
#endif
#if SELECTION == SOME
  return choose_some_page(p);
#endif
  return -1;
}

//free meta-data why do I need it?
int swapfile_init(struct proc *p)
{
  if (p->swapFile == 0 && createSwapFile(p) < 0)
  {
    printf("swapfile_init: creating swapfile failled\n");
    return -1;
  }
  // return 0;

  char *mem = kalloc(); //block of garbge
  char zero = 14;
  for (int i = 0; i < PGSIZE; i++)
  {
    memmove(&mem[i], &zero, 1);
  }
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (writeToSwapFile(p, (char *)mem, i * PGSIZE, PGSIZE) < 0)
    {
      return -1;
    }
  }
  kfree(mem);
  return 0;
}
// initialize the proc table at boot time.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  // ADDED
  // NEW PROCCESS
  if (np->pid > 2) // main and shell don't need paging mechanizem
  {
    if (swapfile_init(np) < 0)
    {
      printf("fork: failed swappage_init\n");
      freeproc(np);
      return -1;
    }
    if (page_metadata_init(np) < 0)
    {
      printf("fork: faild page_metadata_init\n");
      freeproc(np);
      return -1;
    }
  }
  //OLD PROCCESS
  if (p->pid > 2)
  {
    if (copy_swapfile(p, np) < 0)
    {
      printf("fork: copy_swapfile faild\n");
      return -1;
    }
    memmove(np->local_pages, p->local_pages, sizeof(p->local_pages));
    memmove(np->swap_pages, p->swap_pages, sizeof(p->swap_pages));
  }
  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}
//ADDED
int freemetadata(struct proc *p)
{
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    // p->local_pages[i].age = 0;
    p->local_pages[i].va = 0;
    p->local_pages[i].state = PAGE_FREE;
    p->swap_pages[i].va = 0;
    p->swap_pages[i].state = PAGE_FREE;
  }
  return 0;
}
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }
  //ADDED
  if (p->pid > 2)
  {
    if (removeSwapFile(p) < 0)
    {
      printf("exit: removeSwapFile faild\n");
    }
    p->swapFile = 0;
    freemetadata(p);
  }
  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); //DOC: wait-sleep
  }
}

void update_ages(struct proc *p)
{
  p->counter++;
  int index = 0;
  for (struct page_metadata *page_metadata = p->local_pages; page_metadata < &p->local_pages[MAX_PSYC_PAGES]; page_metadata++)
  {
    pte_t *pte = walk(p->pagetable, page_metadata->va, 0);
    page_metadata->age >>= 1; // shift right
    if (*pte & PTE_A)
    {
      page_metadata->age |= (1 << 31); // adding 1 to the MSB of age.
      // printf("ram page %d with age %p\n", index, rmpg->age);
      *pte &= ~PTE_A;
    }
    index++;
  }
}
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
#if SELECTION == NFUA || SELECTION == LAPA
        update_ages(p);
#endif

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}
// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

int copy_swapfile(struct proc *source, struct proc *target)
{
  if (source == 0 || target == 0)
  {
    return -1;
  }
  char *buf = (char *)kalloc();
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    int loc = i * PGSIZE;
    if (source->swap_pages[i].state == PAGE_FREE)
    { //optimization and safety
      continue;
    }
    if (readFromSwapFile(source, buf, loc, PGSIZE) < 0)
    {
      return -1;
    }
    if (writeToSwapFile(target, buf, loc, PGSIZE) < 0)
    {
      return -1;
    }
  }
  return 0;
}
//ADD
//returns an index of the page to swap
int i = 3;
int choose_page_to_swap(struct proc *p)
{
  //
  // for now it always return the first page
  i = (i + 2) % MAX_PSYC_PAGES;

  return 1;
}
void print_flags_of_all_the_pages_in_swap()
{
  return;
  struct proc *p = myproc();
  printf("swap\n");
  for (int i = 0; i < 16; i++)
  {
    struct page_metadata page = p->swap_pages[i];
    if (page.state == PAGE_USED)
    {
      pte_t *pte = walk(p->pagetable, page.va, 0);
      printf("i:%d, PTE_V:%d, PTE_PG: %d,va:%p, pte:%p\n", i, *pte & PTE_V, *pte & PTE_PG, page.va, *pte);
    }
  }
  printf("local\n");
  for (int i = 0; i < 16; i++)
  {
    struct page_metadata page = p->local_pages[i];
    if (page.state == PAGE_USED)
    {
      pte_t *pte = walk(p->pagetable, page.va, 0);
      printf("i:%d, PTE_V:%d, PTE_PG: %d,va:%p, pte:%p\n", i, *pte & PTE_V, *pte & PTE_PG, page.va, *pte);
    }
  }
}

//ADDED
int swapout(struct proc *p, int local_page_toswapout_index)
{
  struct page_metadata *local_page = &p->local_pages[local_page_toswapout_index];
  pte_t *pte = walk(p->pagetable, local_page->va, 0);
  uint64 pa = PTE2PA(*pte);
  if (!(*pte & PTE_V))
  {
    panic("tried to swapout unvalid page\n");
  }
  if (local_page->state == PAGE_FREE)
  {
    panic("tried to swap out free page\n");
  }
  //FIND PLACE IN SWAP FILE
  int free_swap_index = -1;
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (p->swap_pages[i].state == PAGE_FREE)
    {

      free_swap_index = i;
      break;
    }
  }
  if (free_swap_index == -1)
  {
    panic("no free place for page at swapfile;");
  }
  // copy out the page
  if (writeToSwapFile(p, (char *)pa, free_swap_index * PGSIZE, PGSIZE) < 0)
  {
    panic("writeToSwapFile faild at swapout()\n");
  };
  //update metadata of both
  struct page_metadata *swap_page = &p->swap_pages[free_swap_index];
  swap_page->state = PAGE_USED;
  swap_page->va = local_page->va;
  local_page->state = PAGE_FREE;
  local_page->va = 0;
  kfree((void *)pa);
  *pte |= PTE_PG; // flag page swaptout
  *pte &= ~PTE_V; //
  sfence_vma();   // refreshing the TLB
  print_flags_of_all_the_pages_in_swap();
  return 0;
}
//ADD
int register_new_page(struct proc *p, uint64 va)
{
  if (p->pid <= 2)
  {
    return 0;
  }
  struct page_metadata *new_page;
  int freeindex;
  if ((freeindex = find_local_slot(p)) < 0)
  {
    //swap file out
    int toswap_index;
    if ((toswap_index = page_to_swap(p)) < 0)
    {
      printf("choose_page_to_swap faild \n");
      return -1;
    }
    if (swapout(p, toswap_index) < 0)
    {
      printf("swapout faild\n");
      return -1;
    }
    freeindex = toswap_index;
  }
  new_page = &p->local_pages[freeindex];
  new_page->state = PAGE_USED;
  new_page->va = va;
  return 0;
}

//ADDED
//return index of free space in local_pages metatdata
int find_local_slot(struct proc *p)
{
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (p->local_pages[i].state == PAGE_FREE)
      return i;
  }
  return -1;
}
//ADDED
int pagefault(uint64 va)
{
  struct proc *p = myproc();
  if (p->pid <= 2)
  {
    panic("pagefault: procces with pid <=2 arrived\n");
  }
  pte_t *pte = walk(p->pagetable, va, 0);
  if (*pte & PTE_PG)
  {
    swapin(va);
  }
  else
  {
    printf("segmentation fault\n");
    p->killed = 1;
  }
  return 0;
}
//ADDED
//returns the index of the corespoding page_metadata at the swap_pages
int find_swappage_by_va(uint va)
{
  struct proc *p = myproc();
  //find matching page
  struct page_metadata *page;
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    page = &p->swap_pages[i];
    if (page->va == va)
    {
      return i;
    }
  }
  return -1;
}
//ADDED
int find_localpage_by_va(uint va)
{
  struct proc *p = myproc();
  //find matching page
  struct page_metadata *page;
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    page = &p->local_pages[i];
    if (page->va == va)
    {
      return i;
    }
  }
  return -1;
}
//ADDED
//there is no use case of swaping in for another procces
int swapin(uint64 va)
{
  struct proc *p = myproc();
  int page_index = find_swappage_by_va(va);
  // printf("page to swapin:%d\n",page_index);
  if (page_index < 0)
  {
    panic("swapin: can't locate swaped page at swap_pages\n");
  }
  // printf("page index in swapfile: %d\n", page_index);
  // take out the page form the swap
  char *buf = kalloc();
  if (readFromSwapFile(p, buf, page_index * PGSIZE, PGSIZE) < 0)
  {
    panic("swapin: can't readFromSwapFile");
  }
  // printf("first char of buf: %d\n",buf[0]);
  pte_t *pte = walk(p->pagetable, va, 0);
  *pte |= PTE_V;
  *pte &= ~PTE_PG;
  // mappages(p->pagetable, va, PGSIZE, (uint64)buf, PTE_FLAGS(*pte));
  *pte = PA2PTE(buf) | PTE_FLAGS(*pte) | PTE_V; // insert the new allocated pa to the pte in the correct part
  p->swap_pages[page_index].state = PAGE_FREE;
  p->swap_pages[page_index].va = 0;
  // printf("before registering new page");
  register_new_page(p, va);
  sfence_vma(); // refreshing the TLB
  return 0;
}
//ADDED
int remove_page(uint64 va)
{
  struct proc *p = myproc();
  if (p->pid <= 2)
    return 0;
  // printf("\n");
  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (p->local_pages[i].va == va && p->local_pages[i].state == PAGE_USED)
    {
      p->local_pages[i].va = 0;
      p->local_pages[i].state = PAGE_FREE;
      // p->local_pages[i].age = 0;
      return 0;
    }
  }

  for (int i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (p->swap_pages[i].va == va && p->swap_pages[i].state == PAGE_USED)
    {
      p->swap_pages[i].va = 0;
      p->swap_pages[i].state = PAGE_FREE;
      return 0;
    }
  }
  return -1;
}