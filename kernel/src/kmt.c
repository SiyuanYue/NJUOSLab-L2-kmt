#include <os.h>
#include <limits.h>

static Context *kmt_context_save(Event, Context *);
static Context *kmt_schedule(Event, Context *);
spinlock_t kt;
spinlock_t tr;
extern task_t *alltasks[MAX_TASKS];
extern size_t task_cnt;
extern task_t *currents[MAX_CPU];//每个CPU的current线程
typedef struct CPU
{
    int intena;
    int noff;
}CPU;
CPU cpus[MAX_CPU];
void push_off()
{
    int i = ienabled();
    iset(false);
    int c = cpu_current();
    if (cpus[c].noff == 0)
        cpus[c].intena = i;
    cpus[c].noff++;
}
void pop_off()
{
    int c = cpu_current();
    assert(cpus[c].noff >= 1);
    assert(!cpus[c].intena);
    cpus[c].noff--;
    if (cpus[c].noff == 0 && cpus[c].intena == true)
    {
        iset(true);
    }
}
void spin_init(spinlock_t *lk, const char *name)
{
    lk->lock = 0;
    lk->cpu = -1;
    strcpy(lk->name, name);
}
void spin_lock(spinlock_t *lk)
{
    push_off();
    assert(!(lk->lock && lk->cpu == cpu_current()));
    while (atomic_xchg(&lk->lock, 1) != 0)
    {
        yield();
    }
    lk->cpu = cpu_current();
}
void spin_unlock(spinlock_t *lk)
{
    assert(lk->lock && lk->cpu == cpu_current());
    lk->cpu = -1;
    atomic_xchg(&lk->lock, 0);
    pop_off();
}

static Context *kmt_context_save(Event ev, Context *context)
{
    if (!_current)
    {
        task_t *th_main = (task_t *)pmm->alloc(sizeof(task_t *));
        if(!th_main) panic("NO memory");
        th_main->status = RUNNING;
        strcpy(th_main->name, "main");
        _current = th_main;
         //为all_tasks添加current节点
        size_t i = 0;
        for (; i < task_cnt; i++)
        {
            if(alltasks[i]==NULL)
                break;
        }
        alltasks[i]=_current;
        _current->index=i;
    }
    assert(_current->status==RUNNING);
    memcpy(_current->context,context,sizeof(Context));
    return NULL;
}
static Context *kmt_schedule(Event ev, Context *context)
{
    size_t i = _current->index+1;
    for (; i <MAX_TASKS; i++)
    {
        if(i==MAX_TASKS)
            i=0;
        if(alltasks[i]&&alltasks[i]->status!=BLOCKED)
            break;
    }
    _current=alltasks[i];
    _current->status=RUNNING;
    return _current->context;
}
static int kmt_create(task_t *task, const char *name, void (*entry)(void *arg), void *arg)
{
    kmt->spin_lock(&kt);
    task->status = RUNABLE;
    strcpy(task->name, name);
    Area stack = (Area){task->stack, task + 1};
    task->context = kcontext(stack, entry, arg);
    if (!_current)
    { //主线程第一次调用，没记录
        task_t *th_main = (task_t *)pmm->alloc(sizeof(task_t *));
        if(!th_main) return 1;
        th_main->status = RUNNING;
        strcpy(th_main->name, "main");
        _current = th_main;
        //为al_tasks添加current节点
        size_t i = 0;
        for (; i < task_cnt; i++)
        {
            if(alltasks[i]==NULL)
                break;
        }
        alltasks[i]=_current;
        _current->index=i;
    }
     //为al_tasks添加创立的task节点
    size_t i = 0;
    for (; i < task_cnt; i++)
    {
        if(alltasks[i]==NULL)
            break;
    }
    alltasks[i]=task;
    task->index=i;
    kmt->spin_unlock(&kt);
    return 0;
}
static void kmt_teardown(task_t *task)
{
    kmt->spin_lock(&kt);
    alltasks[task->index]=NULL;
    pmm->free(task);
    kmt->spin_unlock(&kt);
}
static void kmt_init()
{
    os->on_irq(INT_MIN, EVENT_NULL, kmt_context_save); // 总是最先调用
    os->on_irq(INT_MAX, EVENT_NULL, kmt_schedule);
    for (size_t i = 0; i < cpu_count(); i++)
    {
        currents[i]=NULL;
        cpus[i].noff=0;
        cpus[i].intena=0;
    }
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        alltasks[i]=NULL;
    }
    task_cnt=0;
    kmt->spin_init(&kt, "thread_lock_task");
    kmt->spin_init(&tr, "trap_lock");
}
void sem_init(sem_t *sem, const char *name, int value)
{
    kmt->spin_init(&sem->lock,sem->name);
    sem->count = value;
    sem->l=0;
    sem->r=0;
    strcpy(sem->name, name);
}
void wakeup(sem_t *sem)
{
    assert(sem->l-sem->r!=0);
    task_t *t=sem->pool[sem->l];
    sem->l++;
    if(sem->l==MAX_TASKS)
        sem->l=0;
    t->status=RUNABLE;
    kmt->spin_unlock(&sem->lock);
}
void sem_signal(sem_t *sem)
{
    kmt->spin_lock(&sem->lock);
    sem->count++;
    if(sem->count>=0)
    {
        wakeup(sem);
    }else
    {
        kmt->spin_unlock(&sem->lock);
    }

}
void block(sem_t *sem)
{//阻塞
    _current->status=BLOCKED;
    sem->pool[sem->r]=_current;
    sem->r++;
    if(sem->r==MAX_TASKS)
        sem->r=0;
    kmt->spin_unlock(&sem->lock);
    yield();
}
void sem_wait(sem_t *sem)
{//中断处理程序不可睡眠(sem_wait)，可以调用 sem_signal。
    kmt->spin_lock(&sem->lock);
    sem->count--;
    while(sem->count<0)
        block(sem);
    kmt->spin_unlock(&sem->lock);
}
MODULE_DEF(kmt) = {
    .init = kmt_init,
    .create = kmt_create,
    .teardown = kmt_teardown,
    .spin_init = spin_init,
    .spin_lock = spin_lock,
    .spin_unlock = spin_unlock,
    .sem_init = sem_init,
    .sem_signal = sem_signal,
    .sem_wait = sem_wait
    };
