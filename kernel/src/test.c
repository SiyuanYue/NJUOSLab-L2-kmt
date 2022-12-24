#include <test.h>
#include <os.h>
#include <common.h>
void P(sem_t *sem);
void V(sem_t *sem);
void P(sem_t *sem)
{
  assert(sem);
  bool succ=false;
  while(!succ)
  {
    kmt->spin_lock(&(sem->lock));
    if(sem->count>0)
    {
      sem->count--;
      succ=true;
    }
    kmt->spin_unlock(&(sem->lock));
    if(!succ)
		{
        yield();printf("ok\n");
    }
  }
}
void V(sem_t *sem)
{
  assert(sem);
  kmt->spin_lock(&(sem->lock));
  sem->count++;
  //printf("%s : %d\n",sem->name,sem->count);
  kmt->spin_unlock(&(sem->lock));
}
void foo(void *s ){
	while(1)
		putch(*(const char *)s);
}
void test01()
{
	task_t *task1=(task_t*) pmm->alloc(sizeof(task_t));
	task_t *task2=(task_t*) pmm->alloc(sizeof(task_t));
	kmt->create(task1,"foo1",foo,(void *)"a");
	kmt->create(task2,"foo2",foo,(void *)"b");
}
sem_t empty1;
sem_t fill1;
void producer1(){while(1){P(&empty1);putch('('); V(&fill1);}}
void consumer1(){while(1){P(&fill1);putch(')');V(&empty1);}}
void test02()
{
	kmt->sem_init(&empty1,"empty1",1);
	kmt->sem_init(&fill1,"fill1",0);
	task_t* task1= (task_t*) pmm->alloc(sizeof(task_t));
	task_t* task2= (task_t*) pmm->alloc(sizeof(task_t));
	kmt->create(task1,"producer1",producer1,NULL);
	kmt->create(task2,"consumer1",consumer1,NULL);
}
