#ifndef MY_LIST_H_
#define MY_LIST_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>          // std::launder
#include <type_traits>
#include <utility>

/// @brief 固定容量、线程安全的 FIFO 对象池（双向索引链表实现）
///
/// 【状态模型】
///   每个槽位有三种状态：
///     free     在 free_ 链上，未构造
///     reserved 被摘出，正在锁外构造 / 消费 / 析构，不在任何链上
///     used     在 used_ 链上，已构造
///   不变量：
///     free_.size + used_.size + reserved_ == Capacity
///     size() = used_.size + reserved_
///     full() = (free_.size == 0)
///
/// 【并发契约】
///   - 所有公共方法线程安全，内部由一把 std::mutex 保护
///   - for_each 的回调在锁内执行，必须 noexcept、快速、不重入
///   - consume / erase_if / clear 的析构在锁外执行，允许慢
///   - consume 的回调在锁外执行，允许慢、阻塞、IO、再次访问本容器
///     （但不能递归调用 consume）
///   - consume_front 是逐节点消费，只有“正在处理的那一个”对
///     clear / erase_if / for_each 不可见
///   - clear / consume / consume_front / erase_if / for_each 只处理
///     used_ 节点，不碰 reserved_ 节点
///   - 析构 ~MyList() 的前置条件：reserved_ == 0
///
/// 【API 分层】
///   状态查询      size / used_size / reserved_size / empty / full /
///                 free_size / capacity
///   生产          emplace_back / construct
///   短消费        pop_front
///   长耗时消费    consume / consume_front
///   锁内遍历      for_each
///   锁内销毁      erase_if / clear
///
/// 【异常安全】
///   - 除 emplace_back / construct 外，其余方法均 noexcept
///   - emplace_back 要求 T 从 Args 构造 noexcept
///   - construct    要求 fn 是 noexcept，且 fn 内部保证构造成功
///   - pop_front    要求 T 移动赋值 noexcept
///   - for_each     要求回调 noexcept
///   - consume / consume_front 要求回调与析构 noexcept
///   - erase_if     要求谓词与析构 noexcept
template<typename T, std::int16_t Capacity>
class MyList {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
    static_assert(Capacity <= 32767, "Capacity must fit in Index/Size");

public:
    using Index = std::int16_t;
    using Size  = std::int16_t;

private:
    struct Node {
        Index prev = -1;
        Index next = -1;
        alignas(T) std::byte storage[sizeof(T)];
    };

    /// @brief 双向索引链表头
    struct List {
        Index head = -1;
        Index tail = -1;
        Size  size = 0;
    };

    std::array<Node, Capacity> nodes_;
    List used_;
    List free_;
    Size reserved_ = 0;      // 被摘出、尚未挂回任何链的节点数
    mutable std::mutex mtx_;

public:
    // ============================================================
    // 构造 / 析构
    // ============================================================

    MyList() noexcept {
        // 初始化空闲链：0 -> 1 -> ... -> Capacity-1
        for (Size i = 0; i < Capacity; ++i) {
            nodes_[i].prev = (i > 0) ? (i - 1) : -1;
            nodes_[i].next = (i + 1 < Capacity) ? (i + 1) : -1;
        }
        free_.head = 0;
        free_.tail = Capacity - 1;
        free_.size = Capacity;

        // 已用链初始为空
        used_.head = -1;
        used_.tail = -1;
        used_.size = 0;
    }

    /// @note 前置条件：reserved_ == 0。
    ///       若仍有线程在锁外构造 / 消费，容器的生命周期不应结束。
    ~MyList() { clear(); }

    MyList(const MyList&)            = delete;
    MyList& operator=(const MyList&) = delete;
    MyList(MyList&&)                 = delete;
    MyList& operator=(MyList&&)      = delete;

    // ============================================================
    // 状态查询
    // ============================================================

    /// @brief 已分配槽位数，等于 used_.size + reserved_
    [[nodiscard]] Size size() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return static_cast<Size>(used_.size + reserved_);
    }

    /// @brief 已构造完成、可被 consume / for_each / erase_if 处理的元素数
    [[nodiscard]] Size used_size() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return used_.size;
    }

    /// @brief 正在锁外构造 / 消费 / 析构的 in-flight 元素数
    [[nodiscard]] Size reserved_size() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return reserved_;
    }

    /// @brief 容器中既没有已构造元素（不含 in-flight ）时为 true
    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return used_.size == 0;
    }

    /// @brief 没有空槽可分配时为 true
    [[nodiscard]] bool full() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return free_.size == 0;
    }

    /// @brief 空闲槽数
    [[nodiscard]] Size free_size() const noexcept {
        std::lock_guard<std::mutex> lock(mtx_);
        return free_.size;
    }

    [[nodiscard]] static constexpr Size capacity() noexcept { return Capacity; }

    // ============================================================
    // 生产
    // ============================================================

    /// @brief 尾部原地构造
    ///        锁内摘槽并计入 reserved_，锁外构造，锁内挂入 used_
    /// @return 池已满返回 false
    /// @note 构造在锁外执行，允许慢。要求 T 从 Args 构造 noexcept。
    template<typename... Args>
    bool emplace_back(Args&&... args) {
        static_assert(std::is_nothrow_constructible_v<T, Args...>,
                      "T must be nothrow constructible from Args");

        Index idx;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            idx = pop_free_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        // 锁外构造
        new (object_ptr(idx)) T(std::forward<Args>(args)...);

        {
            std::lock_guard<std::mutex> lock(mtx_);
            --reserved_;
            push_back_unlocked(used_, idx);
        }
        return true;
    }

    /// @brief 在尾部空槽上执行调用方提供的构造逻辑
    ///        锁内摘槽并计入 reserved_，锁外执行 fn，锁内挂入 used_
    /// @param fn 接受一个 T* 槽位指针，应在其中构造对象；必须 noexcept
    /// @return 池已满返回 false
    /// @note 用于构造派生类型（T 的派生类），emplace_back 无法覆盖此场景。
    ///       fn 在锁外执行，允许慢。fn 必须保证在其上成功构造 T，
    ///       否则槽位会永久悬浮（reserved_ 无法回滚）。
    template<typename Construct>
    bool construct(Construct fn) {
        static_assert(std::is_nothrow_invocable_v<Construct, T*>,
                      "Construct must be nothrow invocable with T*");

        Index idx;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            idx = pop_free_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        // 锁外构造
        fn(object_ptr(idx));

        {
            std::lock_guard<std::mutex> lock(mtx_);
            --reserved_;
            push_back_unlocked(used_, idx);
        }
        return true;
    }

    // ============================================================
    // 短消费
    // ============================================================

    /// @brief 头部弹出：移动赋值到 out，析构元素，归还槽位
    /// @return 队列为空返回 false
    /// @note 整体在锁内完成，移动赋值与析构必须 noexcept 且快。
    ///       若需要长耗时处理，请用 consume / consume_front。
    [[nodiscard]] bool pop_front(T& out) {
        static_assert(std::is_nothrow_move_assignable_v<T>,
                      "T must be nothrow move assignable");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");
        std::lock_guard<std::mutex> lock(mtx_);
        Index idx = pop_used_unlocked();
        if (idx == -1) return false;
        out = std::move(*object_ptr(idx));
        object_ptr(idx)->~T();
        push_free_unlocked(idx);
        return true;
    }

    // ============================================================
    // 锁内遍历
    // ============================================================

    /// @brief 按 FIFO 顺序遍历 used_ 中的所有元素，对每个元素调用 fn
    /// @note 回调在锁内执行，必须 noexcept、快速、不重入本容器。
    template<typename F>
    void for_each(F&& fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<F&, T&>,
                      "F must be nothrow invocable with T&");
        std::lock_guard<std::mutex> lock(mtx_);
        for (Index i = used_.head; i != -1; i = nodes_[i].next) {
            fn(*object_ptr(i));
        }
    }

    // ============================================================
    // 长耗时消费：批量
    // ============================================================

    /// @brief 消费调用时刻 used_ 中的所有元素
    ///        内部：锁内 O(1) 整体摘出 → 锁外逐个 fn + 析构 → 锁内 O(1) 归还
    /// @note 回调 fn 在锁外执行，允许慢、阻塞、IO、再次访问本容器
    ///       （但不能递归调用 consume、保存指针）。
    ///       只处理 used_ 快照，不处理 reserved_ 节点。
    ///       摘出期间对应节点计入 reserved_，所以 size() 保持稳定。
    template<typename Consumer>
    void consume(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<Consumer, T*>,
                      "Consumer must be nothrow invocable with T*");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List tmp;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tmp = used_;
            used_ = List{};
            reserved_ += tmp.size;
        }

        for (Index i = tmp.head; i != -1; ) {
            Index next = nodes_[i].next;
            fn(object_ptr(i));
            object_ptr(i)->~T();
            i = next;
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            reserved_ -= tmp.size;
            splice_unlocked(free_, tmp);
        }
    }

    // ============================================================
    // 长耗时消费：逐节点
    // ============================================================

    /// @brief 消费 used_ 头部一个节点：锁内摘出 → 锁外 fn + 析构 → 锁内归还
    /// @return used_ 为空返回 false
    /// @note 与 consume 的区别：
    ///       - consume        一次性摘出整批，消费期间 used_ 对
    ///                        clear / erase_if / for_each 不可见
    ///       - consume_front  逐节点，只有正在处理的那一个对
    ///                        clear / erase_if / for_each 不可见
    ///       适合“边消费边能被 Clear 看到剩余任务”的场景。
    ///       fn 与析构必须 noexcept。
    template<typename Consumer>
    bool consume_front(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<Consumer, T*>,
                      "Consumer must be nothrow invocable with T*");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        Index idx;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            idx = pop_used_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        fn(object_ptr(idx));
        object_ptr(idx)->~T();

        {
            std::lock_guard<std::mutex> lock(mtx_);
            --reserved_;
            push_free_unlocked(idx);
        }
        return true;
    }

    // ============================================================
    // 锁内销毁
    // ============================================================

    /// @brief 移除所有满足谓词的元素
    /// @return 被移除的元素数量
    /// @note 谓词在锁内执行，必须 noexcept 且快。
    ///       析构在锁外执行，允许慢。
    ///       只处理 used_，不处理 reserved_ 节点。
    template<typename Predicate>
    Size erase_if(Predicate pred) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Predicate&, T&>,
                      "Predicate must be nothrow invocable returning bool");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List removed;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            Index i = used_.head;
            while (i != -1) {
                Index next = nodes_[i].next;
                if (pred(*object_ptr(i))) {
                    unlink_unlocked(used_, i);
                    push_back_unlocked(removed, i);
                }
                i = next;
            }
            if (removed.size > 0) {
                reserved_ += removed.size;
            }
        }

        const Size count = removed.size;
        if (count == 0) return 0;

        for (Index i = removed.head; i != -1; i = nodes_[i].next) {
            object_ptr(i)->~T();
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            reserved_ -= removed.size;
            splice_unlocked(free_, removed);
        }
        return count;
    }

    /// @brief 清空 used_ 中的所有元素
    /// @note 析构在锁外执行，允许慢。
    ///       不处理 reserved_ 节点。清空后 size() 可能不为 0（等于 reserved_）。
    void clear() noexcept {
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List removed;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            removed = used_;
            used_ = List{};
            reserved_ += removed.size;
        }

        if (removed.size == 0) return;

        for (Index i = removed.head; i != -1; i = nodes_[i].next) {
            object_ptr(i)->~T();
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            reserved_ -= removed.size;
            splice_unlocked(free_, removed);
        }
    }

private:
    // ============================================================
    // 内部辅助（全部不加锁，调用方必须持锁）
    // ============================================================

    T* object_ptr(Index i) noexcept {
        return std::launder(reinterpret_cast<T*>(&nodes_[i].storage));
    }
    const T* object_ptr(Index i) const noexcept {
        return std::launder(reinterpret_cast<const T*>(&nodes_[i].storage));
    }

    // ---------- 空槽链 ----------

    Index pop_free_unlocked() noexcept {
        if (free_.head == -1) return -1;
        Index idx = free_.head;
        free_.head = nodes_[idx].next;
        if (free_.head == -1) {
            free_.tail = -1;
        } else {
            nodes_[free_.head].prev = -1;
        }
        --free_.size;
        return idx;
    }

    void push_free_unlocked(Index idx) noexcept {
        nodes_[idx].prev = -1;
        nodes_[idx].next = free_.head;
        if (free_.head == -1) {
            free_.tail = idx;
        } else {
            nodes_[free_.head].prev = idx;
        }
        free_.head = idx;
        ++free_.size;
    }

    // ---------- 已用链 ----------

    Index pop_used_unlocked() noexcept {
        if (used_.head == -1) return -1;
        Index idx = used_.head;
        used_.head = nodes_[idx].next;
        if (used_.head == -1) {
            used_.tail = -1;
        } else {
            nodes_[used_.head].prev = -1;
        }
        --used_.size;
        return idx;
    }

    // ---------- 通用链表操作 ----------

    void push_back_unlocked(List& list, Index idx) noexcept {
        nodes_[idx].prev = list.tail;
        nodes_[idx].next = -1;
        if (list.tail == -1) {
            list.head = idx;
        } else {
            nodes_[list.tail].next = idx;
        }
        list.tail = idx;
        ++list.size;
    }

    void unlink_unlocked(List& list, Index idx) noexcept {
        Index prev = nodes_[idx].prev;
        Index next = nodes_[idx].next;
        if (prev == -1) {
            list.head = next;
        } else {
            nodes_[prev].next = next;
        }
        if (next == -1) {
            list.tail = prev;
        } else {
            nodes_[next].prev = prev;
        }
        --list.size;
        nodes_[idx].prev = -1;
        nodes_[idx].next = -1;
    }

    void splice_unlocked(List& dst, List& src) noexcept {
        if (src.head == -1) return;
        if (dst.tail == -1) {
            dst.head = src.head;
            dst.tail = src.tail;
        } else {
            nodes_[dst.tail].next = src.head;
            nodes_[src.head].prev = dst.tail;
            dst.tail = src.tail;
        }
        dst.size = static_cast<Size>(dst.size + src.size);
        src = List{};
    }
};

#endif  // MY_LIST_H_