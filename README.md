# MyList - 线程安全静态链表容器

适用于 **ESP32 / -fno-exceptions** 环境的固定容量、双向链表，基于静态数组实现，无动态内存碎片。

## Todo
1. 提取公共代码，减少模板膨胀
2. 完善锁机制，尝试支持多线程

## 特性

- ✅ **静态内存分配** – 编译期固定容量，全程无动态内存分配（除 `pop_front` 返回 `unique_ptr` 的重载外）
- ✅ **高性能无堆分配接口** – 提供 `pop_front(T& out)`，直接将元素移至外部，零堆开销
- ✅ **对象池管理** – 内置空闲链表，对象复用高效
- ✅ **线程安全** – 所有公开接口均使用互斥锁保护，但迭代器不持有锁（快照语义）
- ✅ **原地构造 & 消费** – 支持 `construct` 和 `consume_front` / `consume`，避免不必要的移动或拷贝
- ✅ **无异常要求** – 设计用于 `-fno-exceptions` 环境；任何抛出异常的行为将导致 `std::terminate`

## 元素类型要求

1. **必须支持**
   - 移动构造函数（用于 `push_back` 和 `pop_front`）
   - 析构函数（生命周期管理）

2. **强烈推荐**
   - 所有操作标记为 `noexcept`（与 `-fno-exceptions` 环境一致）
   - 移动赋值运算符、默认构造函数等可根据需要提供

3. **严禁**
   - 构造函数、析构函数、消费回调等 **不得抛出异常**（违反即 `std::terminate`）

## API 使用指南

### 1. 添加元素

```cpp
// 移动构造（要求 T 从 U 可 nothrow 构造）
list.push_back(std::move(your_object));

// 原地构造（无移动开销）
list.construct([](T* ptr) {
    new (ptr) T(arg1, arg2);
});