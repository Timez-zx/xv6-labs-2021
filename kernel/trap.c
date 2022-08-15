#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

extern int cow_count[];


void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;
  char* mem;
  pte_t *pte;
  uint64 P_addr;
  uint64 pa;
  uint flags;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else if(r_scause() == 15){
    P_addr = r_stval();
    if(P_addr >= MAXVA){
      p->killed = 1;
    }
    else{
      pte = walk(p->pagetable, PGROUNDDOWN(P_addr), 0);
      pa = PTE2PA(*pte);
      int index = (PHYSTOP - pa)/4096 -1;
      if(cow_count[index] == 1){
        *pte = *pte | PTE_W;
      }
      else {
        if((mem = kalloc()) == 0){
          p->killed = 1;
        }
        else{
          *pte = *pte | PTE_W;
          if(cow_count[index] == 1){
            kfree((void*) mem);
          }
          else{
            cow_count[index]--;
            flags = PTE_FLAGS(*pte);
            memmove(mem, (char*)pa, PGSIZE);
            *pte = PA2PTE((uint64) mem) | flags;
          }
        }
//基本思路是对的，首先，在进行页面PTE更换的时候，首先应该unmap函数操作，再map以更新，我一开始采用直接对*PTE赋值，这样不能代替map和unmap的一些异常处理的操作
//之后，由于我在kfree中，对于释放页面进行了改写，有的情况不释放减少数值，有的时候释放页面，在unmap的时候，就调用kfree了，因此，不应该在这里重复减少数值。

//由于程序是多进程的，因此逻辑会受到并发的影响，举例子，在kalloc之前 ， cow_count[index]为2，但是在kalloc的过程中，有可能共享pa的另一个进程结束，会被kfree释放
//,cow_count[index]=1,如果此时我们不用uvmunmap，而是直接更换PTE，将cow_count[index]-1=0，此时pa的cow_count[index]为1，说明原页面只有一个进程享用pa，但是此时已经将pa复制给mem，
//mem和pa只需要一个页面就可以供给一个进程， 因此uvmnmap再次检查，如果有必要将pa释放掉，如果不释放，则会造成pa占用, uvmnmap多了一个将cow_count[index]为1的页面，
//由于该页面也即将被更换，将pa重新加入freelist中，这样pa被释放，如果不加入freelist，即使pa对应的PTE=0，内存没释放。。。。。。

//uvmunmap使用的思路见copyout, 此时是直接根据情况释放页面，尽可能不释放pa，但是没有锁，如果例子足够刁钻，可能也会有并发问题。

//还有一种思路，就是在整个过程中，上一把锁，直到完成page fault处理，再释放锁，主要是针对cow_count作为全局变量会被改变的情况，相比第二种情况，这种最为安全，第一种
//也比较安全，但是复制次数更多
      }
      
    }

    
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

