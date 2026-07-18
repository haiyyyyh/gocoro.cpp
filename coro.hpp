#include <ucontext.h>
#include <setjmp.h>
#include <cassert>


namespace coro {

namespace {
    struct coro_store{
        char* stack;
        jmp_buf jmpbuf;
        coro_store* next;
        bool first_time;
    };
}

class coroutine_runtime {
private:
    ucontext_t caller_context;
    coro_store root_caller;
    coro_store* coro_queue;
    int coro_count;

    // 唯一的意义是让语义清晰一些
    #define active_co coro_queue->next

public:
    // 初始化时第一个元素是根调用者(Go函数)
    coroutine_runtime()
        : root_caller({
            .stack {},
            .jmpbuf {},
            .next = &root_caller,
            .first_time = false
        }), coro_queue(&root_caller), coro_count(0) {}

    coroutine_runtime(const coroutine_runtime&) = delete;
    void operator=(const coroutine_runtime&) = delete;

    void Go(void (*func)(), size_t stack_size=8192) {
        coro_store* new_coro_p = new coro_store{
            .stack = new char[stack_size],
            .jmpbuf {},
            .next = active_co,
            .first_time = true
        };
        active_co = new_coro_p; // 新协程上下文加入协程队列
        coro_count++;
        // ucontext时间和空间开销特别大,此临时上下文仅用于启动协程作为跳板
        ucontext_t boot_coro;
        getcontext(&boot_coro); // 初始化上下文
        boot_coro.uc_stack.ss_sp = new_coro_p->stack; // 设置栈指针
        boot_coro.uc_stack.ss_size = stack_size;      // 设置栈大小
        boot_coro.uc_link = &caller_context;          // 结束后返回caller上下文
        makecontext(&boot_coro, func, 0);             // 设置上下文入口
        // 设置调用者的跳转点,后续跳转跳回到这里
        int ret = setjmp(active_co->next->jmpbuf);
        if (ret==0) { // 首次setjmp设置跳转点会返回0,之后返回非0
            if (active_co->next==&root_caller)
                swapcontext(&caller_context, &boot_coro);
            else setcontext(&boot_coro);
        } else if (ret==1) { // 从某个协程跳回caller
            return; // 通过go函数的return返回调用者继续执行原来代码流
        }
        // 协程结束,销毁(此时协程队列指针指向当前活动协程的前一个协程, 即 now->die->next)
        // 从队列中移除协程                                         ^
        coro_store* temp = active_co->next;
        delete[] active_co->stack;
        delete active_co;
        active_co = temp;
        coro_count--;
        // 当最后一个协程结束时coro_queue->next会指向自己,初始空链表状态
        // 不再有协程,直接返回
        if (coro_queue->next == coro_queue) {
            return;
        }
        // 一个协程结束返回后,CPU派发给其他协程,执行一次Switch调度切走
        longjmp(active_co->jmpbuf, 1);
    }

    void Yield() {
        // 检查是否对非协程执行流使用了Yield,特征是当前协程的操作指针指向root_caller
        assert(active_co != &root_caller && "gocoro warning: Use runtime.Yield() outside of coroutine");
        int ret = setjmp(active_co->jmpbuf); // 保存当前协程上下文
        if (ret==0) {
            coro_queue = coro_queue->next; // 按顺序切下一个协程
            longjmp(active_co->jmpbuf, 1); // 此时活动协程已被切换
        }
    }

    void Switch() {
        // 检查是否对非协程执行流使用了Switch,特征是当前协程的操作指针指向root_caller
        assert(active_co != &root_caller && "gocoro warning: Use runtime.Switch() outside of coroutine");
        int ret = setjmp(active_co->jmpbuf); // 保存当前协程上下文
        if (ret==0) {
            coro_queue = coro_queue->next; // 轮换到下一个协程
            // 如果"切到根了"不能回去走执行正常执行流,跳过它,只在协程间轮换是Switch的特点
            if (active_co == &root_caller) {
                coro_queue = coro_queue->next;
            }
            longjmp(active_co->jmpbuf, 1); // 此时活动协程已被切换
        }
    }

    int queue_length() {
        return this->coro_count;
    }

    // 处理协程队列内还有挂起的协程,但根调用者返回,调度器(协程资源的所有者)生命周期结束的情况
    /* ~coroutine_runtime() {
        // 活动协程为根调用者(主执行流),意味着队列里有没执行完返回的挂起协程,但根调用者退出了
        if (coro_queue->next == &root_caller) {
            coro_queue = coro_queue->next; // move to root_caller
        }
        // 强行释放这些未结束的睡眠协程
        while (coro_queue->next != coro_queue) { // next指向自己,队列已空
            coro_store* temp = coro_queue->next->next;
            delete coro_queue->next;
            coro_queue->next = temp;
        }
    } */
    ~coroutine_runtime() {
        // 强行释放这些睡眠协程会跳过协程栈上RAII对象的析构导致泄漏,这是致命问题,应该直接炸
        assert(coro_queue->next == coro_queue && "gocoro warning: There is some suspending coroutine in queue when caller exit");
    }

    #undef active_co

};


}