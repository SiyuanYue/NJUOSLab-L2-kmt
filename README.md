
#  实验指导书: http://jyywiki.cn/OS/2022/labs/L2
# 实验目的
 >	在线程执行时中断到来，操作系统代码开始执行并保存处理器运行的寄存器现场；在中断返回时，可以选择任何一个进程/线程已经保存的寄存器现场恢复，从而实现上下文切换。在这个实验中，我们扩展课堂上的示例代码，实现多处理器操作系统内核中的内核线程 API (就像 pthreads 库，或是课堂上展示的 `thread.h`)。在完成这个实验后，你就得到了一个真正可以并行运行多任务的嵌入式操作系统。

L2 的代码位于 ` ./kenel/` 文件夹下。
**如果要启动多个处理器，可以为 `make run` 传递 `smp` 环境变量，例如 `smp=2` 代表启动 2 个处理器；`smp=4` 代表启动 4 个处理器。**
# 缺陷列表 ：
1. 奇怪的问题（BUG）：通过 `make run` 开启多核启动，这样会可以让多个线程真正*同时*并行，运行在不同的 CPU（核） 上，但在当 `smp ` 很大时（>4）会出现问题，会跑一段时间 crash 掉。不知道是因为程序有 BUG，还是虚拟机原因还是 qemu 之类的别的原因导致的。测试了基本的创建两个线程打印字符，没有使用信号量依然会出现该情况，可见并不是因为 dev 模块使用的信号量代码出现 BUG 导致，具体原因没有找到。
2. 睡眠信号量：会触发 assertion，所以注释掉了没有使用，先使用*未优化过的* 信号量代码，去进行之后的实验，不会出现该状况。

# 文件组织结构 ：
```text
. 
└── kernel/
	├── framework/ 
	├── src/ 
		└── kmt.c (L2)
		└── os.c  (L2)
		└── pmm.c (L1 kmalloc/kfree)
		└── test.c  (L2)  
	├── include/     
	├── Makefile         
└── abstract-machine/
```
L2-KMT (内核线程管理) 代码主要位于 `./kernel/` 下的 `os.c` 和 `kmt.c` 。
# 实验整体思路 ：
 1. 首先仔细阅读详尽的实验指导书: [L2: 内核线程管理 (kmt) (jyywiki.cn)](http://jyywiki.cn/OS/2022/labs/L2)，阅读并调试 [thread-os](http://jyywiki.cn/pages/OS/2022/demos/thread-os.c) 的代码。理解中断和上下文切换的机制。

2. 理解代码模块：
```C
typedef Context *(*handler_t)(Event, Context *);
MODULE(os) {
  void (*init)();
  void (*run)();
  Context *(*trap)(Event ev, Context *context);
  void (*on_irq)(int seq, int event, handler_t handler);
};

MODULE(pmm) {
  void  (*init)();
  void *(*alloc)(size_t size);
  void  (*free)(void *ptr);
};

typedef struct task task_t;
typedef struct spinlock spinlock_t;
typedef struct semaphore sem_t;
MODULE(kmt) {
  void (*init)();
  int  (*create)(task_t *task, const char *name, void (*entry)(void *arg), void *arg);
  void (*teardown)(task_t *task);
  void (*spin_init)(spinlock_t *lk, const char *name);
  void (*spin_lock)(spinlock_t *lk);
  void (*spin_unlock)(spinlock_t *lk);
  void (*sem_init)(sem_t *sem, const char *name, int value);
  void (*sem_wait)(sem_t *sem);
  void (*sem_signal)(sem_t *sem);
};
```
pmm 模块是 L0 的内容，实际要实现的就是 `MODULE(OS)` -os. c 和 `MODULE(kmt)` - kmt. c 
kmt 模块下有 kmt 的初始化函数，线程创建，回收，锁和信号量的实现函数。os 模块下有中断/异常处理程序的唯一入口 `os->trap` 和中断注册函数 `os->irq`，通过后者注册不同中断事件的处理函数，前者根据当前中断事件类型调用后者注册的处理函数。
其余见指导书。
3. os->irq 和 os->trap实现：

用数组的方式存储多有注册的中断处理程序
```C
typedef struct handlers_seq
{  //每个事件处理的结构体
    handler_t handler;
    int event;
    int seq;
} handlers_seq;
handlers_seq handlers_sorted_by_seq[handler_size];//事件处理表
int handlers_cnt = 0;//事件处理表当前有效个数
```
`void init_handlers_sorted_by_seq()` 函数在 `os->init()` 中调用，初始化事件处理表。
每次调用 os->irq 时, 将要注册的中断事件处理函数，事件名和 `seq` 作为一个表项加入整表，然后调用 `sort_handlers();` 对整表按 seq 由小到大冒泡排序。
然后再 os->trap 中按照指导书范例对事件表每个事件检查是否调用
```C
for (int i = 0; i < handler_size; i++)
    {
        handlers_seq h = handlers_sorted_by_seq[i];
        if (h.event == EVENT_NULL || h.event == ev.event)
        {
            Context *r = h.handler(ev, ctx);
            panic_on(r && next, "returning multiple contexts");
            if (r)
                next = r;
        }
    }
```
同样在 `kmt->init` 中
`os->on_irq(INT_MIN, EVENT_NULL, kmt_context_save); // 总是最先调用`
`os->on_irq (INT_MAX, EVENT_NULL, kmt_schedule);`
每次中断遍历事件表必定会调用这两个函数，并最先调用上下文保存函数，最后调用 `schedule`
4.  spinlock                            

锁的实现借鉴了 xv6 。
但将 push_off () 位置移在了原子交换指令之后，我觉得这样更合理
```C
while (atomic_xchg(&lk->lock, 1) != 0)
    {
        if(ienabled())
            yield();
    }
for(volatile int i=0;i<10000;++i);
push_off(); // disable interrupts to avoid deadlock.
```
5.  kmt_context_save 和 kmt_schedule
```C
typedef struct task
{
  int index;//该task在数组alltasks中的下标
  enum tr_status status;//该task的运行状态
  char name[20];
  Context context;//中断不允许嵌套
  uint8_t stack[STACK_SIZE];//内核栈
}task_t;
```
为了方便线程结构体中直接定义了 ` Context context;`，而非是保留一个指针，堆区分配一块内存（原本是这样，结果出了 BUG）
  `_current->context=*context;` 即可保存从 trap 入口陷入中断的当前线程的上下文，保存到该线程的内核栈中。
  接下里是调度，这里要注意一个问题，主要是 smp>1, 多处理器会触发的。
  调度到的线程不仅仅是不在 block 状态的线程，**而且也不能是别的 CPU 正在运行的线程**
  因此采取线程在调用 `context_save` 时将当前线程状态改为 `RUNNABLE`, 在调度函数中不能选取 BLOCKED 和 RUNNING 状态（他们正在别的 CPU 上跑！）的线程调度。  
6. yield () 

要相当小心的使用 yield (), 注意你使用 yield () 前，中断是否打开。否则单处理器模式还好，多处理器模式会出问题。
```C
 if(ienabled())
	yield();
```

  