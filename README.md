# 神奇的C++有栈协程

## 基础使用方式

```cpp
#include <print>

#include "coro.hpp"

coro::coroutine_runtime co;

void func1() {
    for (int i=0; i<10; ++i){
        std::print("hello from func1")
        co.Yield();
    }
}

void func2() {
    for (int i=0; i<10; ++i){
        std::print("hello from func2")
        co.Yield();
    }
}

void func3() {
    co.Go(func2); // 嵌套协程
    for (int i=0; i<10; ++i){
        std::print("hello from func3")
        co.Yield();
    }
}

void wait() {
    while(co.queue_length()!=1) {
        co.Switch();
    }
}

int main() {
    co.Go(func1);
    co.Go(func3);
    co.Go(wait); // 等待协程执行完毕
    return 0;
}
```

### 不支持在任何协程外使用Yield()或Switch(),如果你想从主执行流切到那些协程去执行,可以这样做

```cpp
int main() {
    co.Go(func1);
    co.Go(func3);
    co.Go([]() {
        co.Switch();
    });
    return 0;
}
```

### Go函数还有第二个参数,可以指定协程启动时分配的栈空间大小,默认8192

```cpp
int main() {
    co.Go(func1, 65536);
    return 0;
}
```

## 性能

```cpp
void func1() {
    for (int i=0; i<1e7; ++i){
        co.Yield();
    }
}

void func2() {
    for (int i=0; i<1e7; ++i){
        co.Yield();
    }
}

void wait() {
    while(co.queue_length()>1) {
        co.Switch();
    }
}

int main() {
    co.Go(func1);
    co.Go(func2);
    co.Go(wait); // 等待协程执行完毕
    return 0;
}
```

> 耗时在500ms左右,且受优化等级影响不大

### 另外,你还可以有多个coroutine_runtime调度器实例,每个实例管理一组协程,一个线程里不止可以有一个调度器 一组协程,但永远不要让多个线程共用一个调度器实例!