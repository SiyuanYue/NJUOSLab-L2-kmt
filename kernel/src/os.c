#include <common.h>
#include <limits.h>
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
		handlers_sorted_by_seq[i].event=EVENT_NULL;
	}
}
bool sane_context(Context* next)
{
	return true;
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

static void os_init()
{
	pmm->init();
	init_handlers_sorted_by_seq();
	// dev->init();
	kmt->init();
}
Context *os_trap(Event ev, Context *ctx)
{
	kmt->spin_lock(&tr);

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
	panic_on(!next, "returning NULL context");
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
	panic_on(handlers_cnt > 0 && handlers_cnt <= handler_size, "irq wrong");
	sort_handlers();
}
static void os_run()
{
	iset(true);
	for (const char *s = "Hello World from CPU #*\n"; *s; s++)
	{
		putch(*s == '*' ? '0' + cpu_current() : *s);
	}
	while (true)
		;
}
MODULE_DEF(os) = {
	.init = os_init,
	.run = os_run,
	.trap = os_trap,
	.on_irq = os_irq,
};
