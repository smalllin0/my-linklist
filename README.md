 # MyList

 固定容量、三态、锁策略可配的 **FIFO 对象池** —— 以"索引双向链表 +
 非模板基类"实现，专为嵌入式 / FreeRTOS 场景设计。

---

 ## 1. 简介

 `MyList<T, Capacity, LockPolicy>` 是一个**编译期固定容量**的先进先出
 对象池。与 `std::deque` 等动态容器不同，它在构造时一次性分配 `Capacity`
 个节点，运行期**不 malloc / free**，适合：

   - 消息队列、日志缓冲、事件分发等"生产者-消费者"场景；
   - 中断上下文 / ISR 中**不阻塞**地投递；
   - 内存受限、要求 **O(1) 且无动态分配** 的嵌入式环境。

 核心亮点：

   - **索引双向链表**：节点用 `int16_t` 索引而非指针，节点数组可整体
     移动、复制、序列化；
   - **三态模型**：`free / reserved / used`，允许**构造、消费、析构在
     锁外执行**；
   - **非模板基类 `ListBase`**：所有与 `T` 无关的链表操作集中在 `.cc`
     中，**全局只编译一次**，多个 `MyList<T, N>` 实例共享同一份代码，
     显著减少代码膨胀；
   - **锁策略可插拔**：默认 `std::mutex`，可换成 `NoLock` 或任意满足
     `BasicLockable` 的自定义策略；
   - **异常安全约定**：除 `emplace_back` / `construct` 外所有公共方法
     `noexcept`。

---

 ## 2. 特性一览

 | 维度             | 支持情况                                       |
 | ---------------- | ---------------------------------------------- |
 | 容量             | 编译期固定 `[1, 32767]`                        |
 | 动态分配         | ❌ 无（节点数组内嵌）                          |
 | 并发安全         | ✅ 由 `LockPolicy` 决定                        |
 | 构造函数抛异常   | ✅ 允许（锁外构造）                            |
 | 析构函数抛异常   | ❌ 要求 `noexcept`                             |
 | 元素类型         | 任意可析构的 `T`（含派生类，见 `construct`）   |
 | 遍历顺序         | FIFO（按 `used_` 链顺序）                      |
 | 迭代器           | 不暴露，用回调                                 |
 | 每节点开销       | `sizeof(T) + 2 * sizeof(int16_t)`，另加对齐    |

---

 ## 3. 目录结构

 ```
 my_list/
 ├── src/
 │   ├── my_list_base.h     # 非模板基类 ListBase（内部实现，不对外）
 │   └── my_list_base.cc    # 链表操作实现，全局只编译一次
 └── include/
     └── my_linklist.h      # 公开 API：MyList<T, Capacity, LockPolicy>
 ```

 **依赖**：C++17 及以上（`std::byte`、`std::launder`）。

---

 ## 4. 快速开始

 ```cpp
 #include "my_linklist.h"

 struct Msg { int id; float value; };

 // 默认线程安全：容量 16，内部 std::mutex
 ThreadSafeList<Msg, 16> q;

 // 生产
 q.emplace_back(1, 3.14f);
 q.emplace_back(2, 2.71f);

 // 短消费（整体锁内完成，要求 nothrow move assignable）
 Msg out;
 if (q.pop_front(out)) {
     // ...
 }

 // 长耗时消费（回调在锁外执行）
 q.consume([](Msg& m) noexcept {
     // 慢操作、IO、甚至再次 emplace_back 本容器都可以
     process(m);
 });

 // 状态
 q.size();       // 含 reserved
 q.used_size();  // 仅 used
 q.empty();
 q.full();
 ```

---

 ## 5. 状态模型

 每个槽位在任意时刻处于三种状态之一：

 ```
                    pop_free (锁内摘出)              new T (锁外构造)
    ┌─────────┐  ──────────────────►  ┌────────────┐  ─────────────────►  ┌─────────┐
    │  free   │                       │  reserved  │                      │  used   │
    │(在 free_│                       │(不在任何链) │                      │(在 used_│
    │  链上)  │  ◄──────────────────  │            │  ◄─────────────────  │  链上)  │
    └─────────┘   reclaim (归还)      └────────────┘   push_back (锁内)    └────────┘
 ```

 **不变量**（任何时刻都成立）：

 ```
 free_.size + used_.size + reserved_ == Capacity
 size()        == used_.size + reserved_
 empty()       == (used_.size == 0)
 full()        == (free_.size == 0)
 ```

 **为什么需要 `reserved` 状态？**

 构造 `T`、消费 `T`、析构 `T` 都可能在锁外执行（用户回调可能很慢）。
 这段时间槽位既不属于 `free`，也不属于 `used`，必须单独计数，否则：

   - `size()` 会低估"占用"（还没挂进 `used_`）；
   - 另一个线程的 `emplace_back` 会看到 "free 非空" 却找不到真正的槽位。

 `reserved` 是"在飞行中的槽位"计数，保证所有状态总和恒等于 `Capacity`。

---

 ## 6. API 参考

 ### 6.1 构造 / 析构

 ```cpp
 MyList() noexcept;
 ~MyList();   // 调用 clear()，要求 reserved_ == 0

 MyList(const MyList&)            = delete;
 MyList& operator=(const MyList&) = delete;
 MyList(MyList&&)                 = delete;   // 节点数组含 std::mutex，不可移动
 MyList& operator=(MyList&&)      = delete;
 ```

 ### 6.2 状态查询（均在锁内，O(1)）

 ```cpp
 Size size()          const noexcept;   // used + reserved
 Size used_size()     const noexcept;
 Size reserved_size() const noexcept;
 bool empty()         const noexcept;   // used 为空
 bool full()          const noexcept;   // free 为空
 Size free_size()     const noexcept;
 static constexpr Size capacity() noexcept;
 ```

 ### 6.3 生产

 ```cpp
 // 锁内摘槽 → 锁外构造 → 锁内挂入 used_
 template <typename... Args>
 bool emplace_back(Args&&... args);

 // 锁内摘槽 → 锁外执行 fn(T*) → 锁内挂入 used_
 template <typename Construct>
 bool construct(Construct fn);
 ```

 - `emplace_back`：要求 `T` 从 `Args...` **`nothrow` 可构造**；满返回
   `false`。
 - `construct`：`fn` 接收一个 **未构造的 `T*` 槽位**，应在其上
   `placement new` 构造。用于构造派生类型，`emplace_back` 无法覆盖此
   场景。**`fn` 必须保证成功构造**，否则槽位永久悬浮。

 ### 6.4 短消费

 ```cpp
 [[nodiscard]] bool pop_front(T& out);
 ```

 - 整体在锁内完成：`move` 赋值 → 析构 → 归还槽位。
 - 要求 `T` **`nothrow` 可移动赋值且 `nothrow` 可析构**。
 - 若消费逻辑耗时较长，请改用 `consume` / `consume_front`。

 ### 6.5 长耗时消费

 ```cpp
 // 批量：一次性摘出整个 used_，锁外消费，锁内归还
 template <typename Consumer>
 void consume(Consumer fn) noexcept;

 // 逐节点：每次只摘出一个，锁外消费，锁内归还
 template <typename Consumer>
 bool consume_front(Consumer fn) noexcept;

 // 锁内条件消费：fn 返回 true 表示"保留该节点并停止"
 template <typename Consumer>
 Size consume_to(Consumer fn) noexcept;
 ```

 - `consume`：内部 `O(1)` 整体摘出，消费期间 `used_` 对
   `clear / erase_if / for_each` **完全不可见**。
 - `consume_front`：逐节点，只有"正在处理的那一个"对
   `clear / erase_if / for_each` 不可见。适合"边消费边允许 Clear 看到
   剩余任务"的场景。
 - `consume_to`：回调在**锁内**执行，`fn` 返回 `true` 表示"保留该节点
   并停止消费"，返回 `false` 表示"消费该节点并继续"。要求 `fn`
   `noexcept`、快速、不重入。
 - 返回 `Size` 表示消费掉的节点数。

 ### 6.6 锁内访问

 ```cpp
 template <typename F>
 void for_each(F&& fn) noexcept;            // 按 FIFO 遍历 used_

 template <typename F>
 [[nodiscard]] bool last(F&& fn) noexcept;  // 访问 used_ 尾部
 ```

 - 回调在**锁内**执行，必须 `noexcept`、快速、**不重入本容器**。
 - `for_each` 的 `fn` 返回 `true` 则提前终止。
 - `last` 返回 `false` 表示队列为空。

 ### 6.7 锁内销毁

 ```cpp
 template <typename Predicate>
 Size erase_if(Predicate pred) noexcept;

 void clear() noexcept;
 ```

 - `erase_if`：谓词在**锁内**执行，接收 `const T&`；**析构在锁外**执行，
   允许慢。返回移除数量。
 - `clear`：清空 `used_` 中的全部元素。不处理 `reserved_` 节点，
   清空后 `size()` 可能不为 0。

---

 ## 7. 锁策略

 ### 7.1 `std::mutex`（默认，线程安全）

 ```cpp
 template<typename T, std::int16_t Capacity>
 using ThreadSafeList = MyList<T, Capacity, std::mutex>;
 ```

 所有公共方法自动加锁。回调契约见第 8 节。

 ### 7.2 `NoLock`（无同步）

 ```cpp
 template<typename T, std::int16_t Capacity>
 using UnsafeList = MyList<T, Capacity, NoLock>;
 ```

 适用场景：

   - 单线程访问；
   - 外部已有同步保证（如关中断、FreeRTOS 临界区）；
   - 明确不需要跨线程安全。

 **警告**：使用此策略时容器**不再线程安全**，多线程访问会导致数据竞争
 和未定义行为。

 ### 7.3 自定义策略

 只需满足 C++11 `BasicLockable`：提供 `lock()` 和 `unlock()`。
 典型实现：`FreeRTOS` 的 `portMUX_TYPE`、`std::shared_mutex`、
 `std::recursive_mutex` 等。

 ```cpp
 struct MyMutex {
     void lock() noexcept   { taskENTER_CRITICAL(&mux_); }
     void unlock() noexcept { taskEXIT_CRITICAL(&mux_);  }
     portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
 };
 MyList<Msg, 8, MyMutex> q;
 ```

---

 ## 8. 并发契约

 仅当 `LockPolicy` 提供同步时有效：

 | 方法类别                                     | 回调执行位置 | 允许耗时 | 允许重入本容器 |
 | -------------------------------------------- | ------------ | -------- | -------------- |
 | `emplace_back` / `construct`                 | 锁外         | ✅       | ✅             |
 | `consume` / `consume_front`                  | 锁外         | ✅       | ⚠️ 不可递归 consume |
 | `pop_front`                                  | 锁内         | ❌       | ❌             |
 | `consume_to` / `for_each` / `last` / `erase_if` | 锁内     | ❌       | ❌             |
 | `clear` / `~MyList`                          | 析构锁外     | ✅（析构）| ✅（析构）      |

 **额外约束**：

   - 所有销毁操作只处理 `used_` 节点，不碰 `reserved_` 节点；
   - `~MyList()` 的前置条件：`reserved_ == 0`（由用户保证，例如在
     析构前确保没有 `consume` 正在飞行）；
   - `erase_if` / `clear` 的析构在锁外执行，但**回调在锁内**；
   - `consume` / `consume_front` 在回调中再次 `emplace_back` 本容器是
     允许的（`reserved` 会正确计数），但**不可递归 `consume` 自身**，
     否则会因为 `used_` 被摘空而"偷走"正在消费的节点。

---

 ## 9. 异常安全约定

 本组件为嵌入式优化，**大多数公共方法为 `noexcept`**。具体：

 | 方法                          | 要求                                 |
 | ----------------------------- | ------------------------------------ |
 | `emplace_back`                | `T` 从 `Args...` `nothrow` 可构造    |
 | `construct`                   | `fn(T*)` `nothrow`，且必须成功构造   |
 | `pop_front`                   | `T` `nothrow` 移动赋值、`nothrow` 析构 |
 | `for_each` / `last`           | 回调 `noexcept`                      |
 | `consume` / `consume_front`   | 回调 `noexcept`，`T` `nothrow` 析构  |
 | `consume_to`                  | 回调 `noexcept`，`T` `nothrow` 析构  |
 | `erase_if`                    | 谓词 `noexcept`，`T` `nothrow` 析构  |
 | `clear` / `~MyList`           | `T` `nothrow` 析构                   |

 这些约束由 `static_assert` 在编译期强制检查。**违反它们会编译失败**。

 锁外构造（`emplace_back` / `construct`）是唯一允许用户代码"慢"的
 路径，也是唯一允许 `T` 构造函数抛出异常的位置——但因为是 `nothrow`，
 实际也不会抛。

---

 ## 10. 性能特征

 | 操作                          | 时间复杂度 | 加锁次数 | 备注                      |
 | ----------------------------- | ---------- | -------- | ------------------------- |
 | `emplace_back`                | O(1)       | 2        | 中间锁外构造              |
 | `construct`                   | O(1)       | 2        | 同上                      |
 | `pop_front`                   | O(1)       | 1        | 全程锁内                  |
 | `consume`                     | O(N)       | 2        | 整批摘出 + 归还           |
 | `consume_front`               | O(1)       | 2        | 每次一个节点              |
 | `consume_to`                  | O(K)       | 1        | K = 实际消费数            |
 | `for_each` / `last`           | O(N) / O(1)| 1        | 全程锁内                  |
 | `erase_if` / `clear`          | O(N)       | 2        | 中间锁外析构              |
 | `size` / `empty` / `full` 等  | O(1)       | 1        | 仅读计数                  |

 **锁内工作量最小化**是本组件最重要的设计目标之一。`consume` 系列
 保证"锁内只做 O(1) 摘链 / 挂链"，把 `T` 的构造、消费、析构全部推到
 锁外。

---

 ## 11. 使用建议

 ### 11.1 何时用 `consume`，何时用 `consume_front`？

 - 批量清空、批量处理：用 `consume`（效率最高，锁内 O(1)）；
 - 需要"边处理边响应 Clear 请求"：用 `consume_front`
   （剩余节点对 `clear` 可见）；
 - 需要"条件消费直到某节点"：用 `consume_to`
   （回调在锁内，必须快）。

 ### 11.2 避免在锁内回调中做重活

 `pop_front`、`for_each`、`last`、`consume_to`、`erase_if`、`clear`
 的回调 / 析构 / 谓词在锁内执行。若逻辑耗时长（IO、日志、再次访问
 其它受锁保护的容器），一律改用 `consume` / `consume_front`。

 ### 11.3 ISR 中投递

 ISR 中**只能**使用 `NoLock` 策略，或通过 `FreeRTOS` 提供的
 `portMUX_TYPE` 自定义锁策略（不要在 ISR 里用 `std::mutex`）。
 ISR 中禁止调用 `consume` / `consume_front` / `pop_front` 等会阻塞或
 可能触发调度的路径；推荐 ISR 只做 `emplace_back` / `construct`，
 消费放到任务上下文。

 ### 11.4 派生类型

 `emplace_back` 只能构造 `T` 本身。若需要向池中放 `T` 的派生类，
 用 `construct`：

 ```cpp
 struct Base { virtual ~Base() noexcept = default; virtual void run() = 0; };
 struct Impl : Base { void run() override { /* ... */ } };

 MyList<Base*, 8> pool;   // 或 MyList<std::unique_ptr<Base>, 8>
 pool.construct([](Base* slot) noexcept {
     // 注意：这样构造的不是派生对象本身，而是 Base 子对象...
 });
 ```

 实际上 `construct` 更常见的用法是 `T` 本身是联合体 / 变体 /
 需要特殊初始化逻辑的类型：

 ```cpp
 MyList<std::variant<A, B, C>, 8> pool;
 pool.construct([](auto* slot) noexcept {
     new (slot) std::variant<A, B, C>(std::in_place_type<B>, /* ... */);
 });
 ```

 ### 11.5 容量选择

 - `Capacity` 最大 `32767`（受 `int16_t` 索引限制）；
 - 每节点大小 `sizeof(Node) = max(sizeof(T), 4) + 对齐`，
   整体占用 `Capacity * sizeof(Node) + 少量元数据`；
 - 建议容量按**峰值并发** + **突发余量** 设定，避免为了"以防万一"
   浪费 RAM。

---

 ## 12. 已知限制与取舍

 | 限制                                    | 原因 / 取舍                                  |
 | --------------------------------------- | -------------------------------------------- |
 | 容量编译期固定                          | 无动态分配，无法运行期扩容                   |
 | 不支持拷贝 / 移动                       | 节点数组内嵌锁对象，移动语义会破坏同步       |
 | 不暴露迭代器                            | 回调式 API 避免"在锁外遍历被并发修改"        |
 | `~MyList` 要求 `reserved_ == 0`         | 悬浮槽位意味着有线程在飞行中操作，不确定     |
 | 回调大量 `noexcept` 约束                | 避免锁内抛异常 → 死锁 / 状态不一致           |
 | `pop_front` 要求 `T` `nothrow` 移动赋值 | 锁内移动赋值，不允许异常                     |

---

 ## 13. 与其他容器对比

 | 维度           | `MyList`           | `std::deque`     | `std::queue`     | FreeRTOS Queue |
 | -------------- | ------------------ | ---------------- | ---------------- | -------------- |
 | 动态分配       | ❌                 | ✅（分块）       | ✅（底层容器）   | ✅（创建时）   |
 | 容量上限       | 编译期固定         | 动态             | 动态             | 动态           |
 | 线程安全       | ✅（策略可配）     | ❌               | ❌               | ✅             |
 | 锁外构造       | ✅（`emplace_back` / `consume`） | ❌     | ❌               | ❌             |
 | ISR 安全       | ✅（`NoLock`）     | ❌               | ❌               | ✅（`_FromISR`）|
 | 代码膨胀       | 低（`ListBase`）   | 高（模板展开）   | 高               | 中             |
 | 回调式消费     | ✅                 | ❌               | ❌               | ❌             |

---

 ## 14. 常见问题

 ### Q1. 为什么 `size()` 包含 `reserved`，而 `used_size()` 不包含？

 `size()` 表达"占用的槽位数"——`reserved` 是"在飞行中"的槽位，物理上
 已被摘出 `free_`，**不能再被其它线程分配**，所以算占用。
 `used_size()` 表达"逻辑上可被消费的元素数"——`reserved` 中正在构造的
 还没挂进 `used_`，正在析构的已经摘出，都不算。

 ### Q2. `consume` 和 `consume_front` 的差别到底是什么？

 假设 `used_` 里有 10 个元素：

   - `consume`：**锁内**一次性把 10 个整体摘出，`used_` 变为空；
     此后 `clear` / `for_each` 看到的是空；锁外逐个处理。
   - `consume_front`：**锁内**只摘出 1 个，`used_` 剩 9 个；锁外处理
     这 1 个；循环 10 次。中途 `clear` 可以把剩下的 9 个清掉。

 ### Q3. 回调里能再次 `emplace_back` 本容器吗？

   - 在 `consume` / `consume_front` 的回调里：**能**（回调在锁外）。
   - 在 `pop_front` / `for_each` / `last` / `consume_to` / `erase_if`
     的回调里：**不能**（回调在锁内，重入会死锁）。

 ### Q4. 构造过程中抛异常怎么办？

 不会抛——`emplace_back` 要求 `T` 从 `Args...` **`nothrow` 可构造**。
 `construct` 要求 `fn` **`noexcept` 且保证成功**。若用户违反，
 行为未定义（槽位悬浮、`reserved_` 计数错乱）。

 ### Q5. 为什么移动构造 / 移动赋值被删除了？

 节点数组内嵌了 `LockPolicy` 实例（如 `std::mutex`）。移动整个容器
 需要移动或重建所有锁，这是不可移植且危险的（正在被其它线程持有的
 锁不能直接 memcpy）。需要搬运请用 `std::unique_ptr<MyList>` 或
 在更高层做转移。
