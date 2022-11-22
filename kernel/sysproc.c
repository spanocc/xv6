#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{

  

  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);

  backstrace();

  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
 
uint64 sys_sigalarm(void) {       //printf("sysalarm!\n");
  int tricks;
  uint64 p;
  if(argint(0, &tricks) < 0)
    return -1;
  if(argaddr(1, &p) < 0)
    return -1;
  myproc()->tricks = tricks; 
  myproc()->handler = (void (*)())(p);   // printf("trick: %d\n  handler:%p\n",tricks, p);
  return 0;
}

// 系统调用返回后会恢复trapframe ，不用手动调用usertrapret
// 从内核态到用户态会调用usertrapret
uint64 sys_sigreturn(void) {  // printf("sysreturn\n");
  struct proc* p = myproc();

  p->trapframe->epc = p->as.pc;

  p->trapframe->ra = p->as.ra;
  p->trapframe->sp = p->as.sp;
  p->trapframe->gp = p->as.gp;
  p->trapframe->tp = p->as.tp;
  p->trapframe->t0 = p->as.t0;
  p->trapframe->t1 = p->as.t1;
  p->trapframe->t2 = p->as.t2;
  p->trapframe->s0 = p->as.s0;
  p->trapframe->s1 = p->as.s1;
  p->trapframe->a0 = p->as.a0;
  p->trapframe->a1 = p->as.a1;
  p->trapframe->a2 = p->as.a2;
  p->trapframe->a3 = p->as.a3;
  p->trapframe->a4 = p->as.a4;
  p->trapframe->a5 = p->as.a5;
  p->trapframe->a6 = p->as.a6;
  p->trapframe->a7 = p->as.a7;
  p->trapframe->s2 = p->as.s2;
  p->trapframe->s3 = p->as.s3;
  p->trapframe->s4 = p->as.s4;
  p->trapframe->s5 = p->as.s5;
  p->trapframe->s6 = p->as.s6;
  p->trapframe->s7 = p->as.s7;
  p->trapframe->s8 = p->as.s8;
  p->trapframe->s9 = p->as.s9;
  p->trapframe->s10 = p->as.s10;
  p->trapframe->s11 = p->as.s11;
  p->trapframe->t3 = p->as.t3;
  p->trapframe->t4 = p->as.t4;
  p->trapframe->t5 = p->as.t5;
  p->trapframe->t6 = p->as.t6;

  p->as.handler_now = 0;

 // usertrapret();

  return 0;
}
