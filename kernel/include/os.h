#include <klib.h>
#include <klib-macros.h>
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
  int index;//该task在数组alltasks中的下标
  enum tr_status status;//该task的运行状态
  char name[20];
  Context context;//中断不允许嵌套
  uint8_t stack[STACK_SIZE];//内核栈
}task_t;

task_t *alltasks[MAX_TASKS];
size_t task_cnt; //最大index
task_t *currents[MAX_CPU];//每个CPU的current线程
#define _current currents[cpu_current()]//当前cpu的current线程
