#include <os.h>
#include <common.h>
#include <limits.h>
#include <test.h>
static Context *kmt_context_save(Event, Context *);
static Context *kmt_schedule(Event, Context *);
spinlock_t kt;
spinlock_t tr;
extern task_t *alltasks[MAX_TASKS];
extern size_t task_cnt;
extern task_t *currents[MAX_CPU]; //每个CPU的current线程
typedef struct CPU
{
    int intena; //中断信息
    int noff;   //递归深度
} CPU;
CPU cpus[MAX_CPU];
static void push_off()
{
    int i = ienabled();
    iset(false);
    int c = cpu_current();
    if (cpus[c].noff == 0)
        cpus[c].intena = i;
    cpus[c].noff++;
}
static void pop_off()
{
    int c = cpu_current();
    assert(cpus[c].noff >= 1);
    cpus[c].noff--;
    if (cpus[c].noff == 0 && cpus[c].intena == true)
    {
        iset(true);
    }
}
static void spin_init(spinlock_t *lk, const char *name)
{
    lk->lock = 0;
    lk->cpu = -1;
    strcpy(lk->name, name);
}
static void spin_lock(spinlock_t *lk)
{
    while (atomic_xchg(&lk->lock, 1) != 0)
    {
        if(ienabled())
            yield();
    }
    for(volatile int i=0;i<10000;++i);
    push_off(); // disable interrupts to avoid deadlock.
    #ifdef delock
    //printf("thread %s : %s , cpu's intena:%d  \n",_current->name ,lk->name,cpus[cpu_current()].intena);
    //printf("thread %s : %s \n",_current->name ,lk->name);
    #endif
    lk->cpu = cpu_current();
}
static void spin_unlock(spinlock_t *lk)
{
    assert(lk->cpu == cpu_current());
    atomic_xchg(&lk->lock, 0);
    lk->cpu=-1;
    #ifdef delock
    //printf("%s  unlock\n", lk->name);
    #endif
    pop_off();
}

static Context *kmt_context_save(Event ev, Context *context)
{
    _current->context=*context;
    _current->status=RUNABLE;
    return NULL;
}
static Context *kmt_schedule(Event ev, Context *context)
{
    size_t i;
    kmt->spin_lock(&kt);
    if(task_cnt==_current->index)
        i=0;
    else
        i=_current->index + 1;
    int num=0;
    for (; i <= task_cnt; i++)
    {
        ++num;
        if (alltasks[i] && alltasks[i]->status != BLOCKED &&alltasks[i]->status!=RUNNING)
            break;
        if (i == task_cnt)
        {
            i = 0;
        }
        assert(num<100);
    }
    kmt->spin_unlock(&kt);
    #ifdef DEBUG
    printf("\nCPUID:%d , from [%s] schedule to [%s]\n",cpu_current(),_current->name,alltasks[i]->name);
    #endif
    _current = alltasks[i];
    _current->status = RUNNING;
    return &(_current->context);
}
static int kmt_create(task_t *task, const char *name, void (*entry)(void *arg), void *arg)
{
    assert(task);
    task->status = RUNABLE;
    //printf("creat-thread\n");
    kmt->spin_lock(&kt);
    strcpy(task->name, name);
    Area stack = (Area){task->stack, task + 1};
    task->context =*kcontext(stack, entry, arg);
    size_t i = 0;
    for (; i < MAX_TASKS; i++) //原本task_cnt忘记更新，且task_cnt一直为0啊！！！  task_cnt-->MAX_TASKS
    {
        if (alltasks[i] == NULL)
        {
            break;
        }
    }
    //printf("task->index: %d\n", i);
    alltasks[i] = task;//加入线程池中
    task->index = i;
    if (i >task_cnt)
        task_cnt = i;
    kmt->spin_unlock(&kt);
    return 0;
}
static void kmt_teardown(task_t *task)
{
    kmt->spin_lock(&kt);
    alltasks[task->index] = NULL;
    if (task_cnt == task->index)
        task_cnt--;
    pmm->free(task);
    kmt->spin_unlock(&kt);
}
static void kmt_init()
{
    os->on_irq(INT_MIN, EVENT_NULL, kmt_context_save); // 总是最先调用
    os->on_irq(INT_MAX, EVENT_NULL, kmt_schedule);
    for (size_t i = 0; i < 16; i++)
    {
        currents[i] = NULL;
        cpus[i].noff = 0;
        cpus[i].intena = 0;
    }
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        alltasks[i] = NULL;
    }
    task_cnt = 0;
    kmt->spin_init(&kt, "thread_lock_task");
    kmt->spin_init(&tr, "trap_lock");
}
static void sem_init(sem_t *sem, const char *name, int value)
{
    kmt->spin_init(&(sem->lock),name);
    sem->count = value;
    sem->l = 0;
    sem->r = 0;
    strcpy(sem->name, name);
}

//信号量 sem ：

//这里的睡眠信号量part总归是存在 BUG TODO
// static void wakeup(sem_t *sem)
// {
//     if(sem->l-sem->r!=0)//如果有等待队列有线程
//     {
//         task_t *t = sem->pool[sem->l];
//         t->status = RUNABLE;
//         sem->l=(sem->l+1)%MAX_TASKS;
//         //printf("signal %d ,l:%d ,r:%d \n",t->index,sem->l,sem->r);
//     }
// }
// static void sem_signal(sem_t *sem)
// {
//     kmt->spin_lock(&sem->lock);
//     sem->count++;
//     wakeup(sem);
//     kmt->spin_unlock(&sem->lock);
// }
// static void block(sem_t *sem)
// { //阻塞
//     kmt->spin_lock(&sem->lock);
//     sem->pool[sem->r] = _current;
//     _current->status = BLOCKED;
//     sem->r=(sem->r+1)%MAX_TASKS;
//     //printf("blocked %d ,sem->name:%s ,l:%d ,r:%d \n",_current->index,sem->name,sem->l,sem->r);
//     kmt->spin_unlock(&sem->lock);
// }
// static void sem_wait(sem_t *sem)
// {//中断处理程序不可睡眠(sem_wait)，可以调用 sem_signal。
//     assert(sem);
//     bool succ=false;
//     while(!succ)
//     {
//         kmt->spin_lock(&(sem->lock));
//         if(sem->count>0)
//         {
//             sem->count--;
//             succ=true;
//         }
//         kmt->spin_unlock(&(sem->lock));
//         if(!succ)
// 		{
//             block(sem);
//             if(ienabled())
//                 yield();
//         }
//     }
// }

//无睡眠的基础信号量
void sem_wait_base(sem_t *sem)
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
            if(ienabled())
                yield();
        }
  }
}
void sem_signal_base(sem_t *sem)
{
    kmt->spin_lock(&(sem->lock));
    sem->count++;
    kmt->spin_unlock(&(sem->lock));
}
MODULE_DEF(kmt) = {
    .init = kmt_init,
    .create = kmt_create,
    .teardown = kmt_teardown,
    .spin_init = spin_init,
    .spin_lock = spin_lock,
    .spin_unlock = spin_unlock,
    .sem_init = sem_init,
    .sem_signal = sem_signal_base,
    .sem_wait = sem_wait_base
    };
