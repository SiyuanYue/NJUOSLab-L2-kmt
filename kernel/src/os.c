#include <limits.h>
#include <devices.h>
#include <os.h>
#include <test.h>
typedef struct handlers_seq
{  //每个事件处理的结构体
	handler_t handler;
	int event;
	int seq;
} handlers_seq;
#define handler_size 20
handlers_seq handlers_sorted_by_seq[handler_size];//事件处理表
int handlers_cnt = 0;//事件处理表当前有效个数
extern spinlock_t tr;
void init_handlers_sorted_by_seq()
{//初始化不同事件处理列表
	handlers_cnt=0;
	for (size_t i = 0; i < handler_size; i++)
	{
		handlers_sorted_by_seq[i].handler=NULL;
		handlers_sorted_by_seq[i].seq=INT_MAX;
		handlers_sorted_by_seq[i].event=EVENT_ERROR;
	}
}
bool sane_context(Context* next)
{
	return false;
}
void sort_handlers()
{ // bubble_sort 事件处理表
	int i, j;
	for (i = 0; i < handlers_cnt; i++)
	{
		for (j = i + 1; j < handlers_cnt; j++)
		{
			if (handlers_sorted_by_seq[i].seq > handlers_sorted_by_seq[j].seq)
			{
				handlers_seq temp = handlers_sorted_by_seq[i];
				handlers_sorted_by_seq[i] = handlers_sorted_by_seq[j];
				handlers_sorted_by_seq[j] = temp;
			}
		}
	}
}
static void tty_reader(void *arg) {
  device_t *tty = dev->lookup(arg);
  char cmd[128], resp[128], ps[16];
  snprintf(ps, 16, "(%s) $ ", arg);
  while (1) {
    tty->ops->write(tty, 0, ps, strlen(ps));
    int nread = tty->ops->read(tty, 0, cmd, sizeof(cmd) - 1);
    cmd[nread] = '\0';
    sprintf(resp, "tty reader task: got %d character(s).\n", strlen(cmd));
    tty->ops->write(tty, 0, resp, strlen(resp));
  }
}
static inline task_t *task_alloc() {
  return pmm->alloc(sizeof(task_t));
}
static void os_init()
{
	pmm->init();
	init_handlers_sorted_by_seq();
	printf("kmt_init:\n");
	kmt->init();
	for (size_t i = 0; i < cpu_count(); i++)
	{
		char name[20];
		sprintf(name,"idle %d",i);
		task_t *t=task_alloc();
		kmt->create(t,name,NULL,(void *)i);
		currents[i]=t;
		t->status=RUNNING;
	}
	_current->status=RUNNING;

	//test01(); //一个一直打印字符的两个线程调度测试

	//test03(); //信号量PV测试

	printf("dev_init:\n");
	dev->init(); //dev模块测试
	kmt->create(task_alloc(), "tty_reader", tty_reader, "tty1");
  	kmt->create(task_alloc(), "tty_reader", tty_reader, "tty2");

}
Context *os_trap(Event ev, Context *ctx)
{
	kmt->spin_lock(&tr);//会关闭中断，即中断不允许嵌套
	Context *next = NULL;
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
	panic_on(!next,"returning NULL context");
	panic_on(sane_context(next), "returning to invalid context");
	kmt->spin_unlock(&tr);
	return next;
}
void os_irq(int seq, int event, handler_t handler)
{ // 注册handler
	handlers_sorted_by_seq[handlers_cnt].handler = handler;
	handlers_sorted_by_seq[handlers_cnt].event = event;
	handlers_sorted_by_seq[handlers_cnt].seq = seq;
	handlers_cnt++;
	panic_on(!(handlers_cnt > 0 && handlers_cnt <= handler_size), "irq wrong");
	sort_handlers();
}
static void os_run()
{
	printf("OS RUN \n");
	iset(true);// 要打开，不然之后的线程无法切换
	while (true);
}
MODULE_DEF(os) = {
	.init = os_init,
	.run = os_run,
	.trap = os_trap,
	.on_irq = os_irq,
};
