#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++){
	//128 interrupt can be called in user mode
	if(i == 128){
	  	SETGATE(idt[i], 1, SEG_KCODE<<3, vectors[i], DPL_USER);
   	}
	else SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  }
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  
  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  //interrupt 128
  if(tf->trapno == 128){
  	cprintf("user interrupt 128 called!\n");
	exit();
  }

  switch(tf->trapno){

  //timer interrupt	  
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  // timer interrupt -> yield(), take cpu away preemptively
#ifdef DEFAULT
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

#elif FCFS_SCHED
  //200 ticks and still running,then kill
  if(myproc() && myproc()->state == RUNNING && 
     (ticks - myproc()->srtime) >= 200){
  	cprintf("pid=%d process killed,by FCFS policy\n", myproc()->pid);
	kill(myproc()->pid);
  }

#elif MULTILEVEL_SCHED
  //if myproc()'s level = 0 : RR, default time interrupt
  if(myproc() && myproc()->state == RUNNING &&
	 myproc()->level==0  && tf->trapno == T_IRQ0+IRQ_TIMER)
	yield();
  
  //if myproc()'s level = 1 : FCFS, 200ticks kill
  if(myproc() && myproc()->state == RUNNING &&
	 myproc()->level==1  && 
	 (ticks - myproc()->srtime) >= 200){
	//cprintf("pid=%d killed due to FCFS policy\n",myproc()->pid);	
	kill(myproc()->pid);
  }

#elif MLFQ_SCHED
  //every 200 ticks perform priority boosting
  if(ticks % 200 == 0)
	  priority_boosting();

  if(myproc() && myproc()->state == RUNNING)
	  myproc()->timeq--;

  //if process running in L0 (level == 0)
  //if it is monopolizing (ismono==1), then it should not yield()
  //and it's time quantum all consumed (timeq)
  //then go down to L1
  if(myproc() && myproc()->state == RUNNING &&
     myproc()->level==0 &&
	 myproc()->ismono==0 &&
	 myproc()->timeq <= 0){
  	myproc()->level = 1;
	myproc()->timeq = 8;
	yield();
  }
  
  //if process running in L1 (level == 1)
  //and not monopolizing (ismono == 0), same as L0
  //and running time is over time quantum
  //then priority - 1, over 0
  if(myproc() && myproc()->state == RUNNING &&
	 myproc()->level==1 &&
	 myproc()->ismono==0 &&
	 myproc()->timeq <= 0){
	  if(myproc()->priority > 0)
		  myproc()->priority--;
	  yield();
  }
#endif

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
