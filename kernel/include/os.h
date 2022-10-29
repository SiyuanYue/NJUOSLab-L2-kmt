#include <common.h>
#define STACK_SIZE 8192
#define MAX_CPU 16
#define MAX_TASKS 64
enum tr_status
{
	RUNABLE=1,
  BLOCKED,
  RUNNING,
};
typedef struct task
{
  int index;
  enum tr_status status;
  char *name;
  Context *context;
  uint8_t stack[STACK_SIZE];//内核栈
}task_t;
task_t *alltasks[MAX_TASKS];
size_t task_cnt;
task_t *currents[MAX_CPU];//每个CPU的current线程
#define _current currents[cpu_current()]//当前cpu的current线程
struct spinlock
{
  int lock;
  int cpu;
  char *name;
};

struct semaphore
{
    int count;
    char *name;
    spinlock_t lock;
    task_t* pool[MAX_TASKS];
    int l;
    int r;
};
