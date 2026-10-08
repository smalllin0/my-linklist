#ifndef MY_LIST_H_
#define MY_LIST_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>


#include "../src/my_list_base.h"

// ============================================================
// 锁策略
// ============================================================

/// @brief 无锁策略：不做任何同步
/// 适用场景：
///   - 单线程访问
///   - 外部已有同步保证（如 IRQ 关中断、FreeRTOS 临界区）
///   - 明确不需要跨线程安全
/// @warning 使用此策略时，容器不再线程安全。
///          多线程访问会导致数据竞争和未定义行为。
struct NoLock {
    void lock() noexcept {}
    void unlock() noexcept {}
};

// ============================================================
// MyList模板
// ============================================================
/// @brief 固定容量、FIFO 对象池（双向索引链表实现）
/// 【线程安全】
///   由第三个模板参数 LockPolicy 决定：
///     - std::mutex（默认）：所有公共方法线程安全
///     - NoLock：无同步，单线程或外部同步下使用
///     - 自定义策略：需满足 BasicLockable（lock/unlock）
/// 【状态模型】
///   每个槽位有三种状态：
///     free     在 free_ 链上，未构造
///     reserved 被摘出，正在锁外构造 / 消费 / 析构，不在任何链上
///     used     在 used_ 链上，已构造
///   不变量：
///     free_.size + used_.size + reserved_ == Capacity
///     size() = used_.size + reserved_
///     full() = (free_.size == 0)
/// 【并发契约】（仅 LockPolicy 提供同步时有效）
///   - for_each / last 的回调在锁内执行，必须 noexcept、快速、不重入
///   - consume / consume_front / consume_to / erase_if / clear
///     的析构在锁外执行，允许慢
///   - consume / consume_front / consume_to 的回调在锁外执行，
///     允许慢、阻塞、IO、再次访问本容器
///   - consume_front / consume_to 是逐节点消费，只有"正在处理的
///     节点"对 clear / erase_if / for_each 不可见
///   - 所有销毁操作只处理 used_ 节点，不碰 reserved_ 节点
///   - 析构 ~MyList() 的前置条件：reserved_ == 0
/// 【API 分层】
///   状态查询      size / used_size / reserved_size / empty / full /
///                free_size / capacity
///   生产          emplace_back / construct
///   短消费        pop_front
///   长耗时消费    consume / consume_front / consume_to
///   锁内访问      for_each / last
///   锁内销毁      erase_if / clear
/// 【异常安全】
///   - 除 emplace_back / construct 外，其余方法均 noexcept
///   - emplace_back 要求 T 从 Args 构造 noexcept
///   - construct    要求 fn 是 noexcept，且 fn 内部保证构造成功
///   - pop_front    要求 T 移动赋值 noexcept
///   - for_each / last 要求回调 noexcept
///   - consume / consume_front / consume_to 要求回调与析构 noexcept
///   - erase_if     要求谓词与析构 noexcept
template<typename T, std::int16_t Capacity, typename LockPolicy = std::mutex>
class MyList : private ListBase {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
    static_assert(Capacity <= 32767, "Capacity must fit in Index/Size");

public:
    using Index = std::int16_t;
    using Size  = std::int16_t;
    using Lock  = LockPolicy;

private:
    /// @brief 节点：NodeBase 作为第一个基类，保证偏移 0
    struct Node : NodeBase {
        alignas(T) std::byte storage[sizeof(T)];
    };

    using Guard = std::lock_guard<LockPolicy>;

    std::array<Node, Capacity> nodes_;
    List used_;
    List free_;
    Size reserved_ = 0;
    mutable LockPolicy mtx_;

    // ============================================================
    // 链表操作：转调非模板基类
    // ============================================================

    NodeView view() noexcept {
        return NodeView{ nodes_.data(), sizeof(Node) };
    }

    void init_free_list_unlocked() noexcept {
        ListBase::init_free_list(free_, view(), Capacity);
        used_.head = -1;
        used_.tail = -1;
        used_.size = 0;
    }

    Index pop_free_unlocked() noexcept {
        return ListBase::pop_free_unlocked(free_, view());
    }

    void push_free_unlocked(Index idx) noexcept {
        ListBase::push_free_unlocked(free_, view(), idx);
    }

    Index pop_used_unlocked() noexcept {
        return ListBase::pop_used_unlocked(used_, view());
    }

    void push_back_unlocked(List& list, Index idx) noexcept {
        ListBase::push_back_unlocked(list, view(), idx);
    }

    void unlink_unlocked(List& list, Index idx) noexcept {
        ListBase::unlink_unlocked(list, view(), idx);
    }

    void splice_unlocked(List& dst, List& src) noexcept {
        ListBase::splice_unlocked(dst, src, view());
    }

    // ============================================================
    // 对象访问（依赖 T）
    // ============================================================

    T* object_ptr(Index i) noexcept {
        return std::launder(reinterpret_cast<T*>(&nodes_[i].storage));
    }
    const T* object_ptr(Index i) const noexcept {
        return std::launder(reinterpret_cast<const T*>(&nodes_[i].storage));
    }

public:
    // ============================================================
    // 构造 / 析构
    // ============================================================

    MyList() noexcept {
        init_free_list_unlocked();
    }

    ~MyList() { clear(); }

    MyList(const MyList&)            = delete;
    MyList& operator=(const MyList&) = delete;
    MyList(MyList&&)                 = delete;
    MyList& operator=(MyList&&)      = delete;

    // ============================================================
    // 状态查询
    // ============================================================

    [[nodiscard]] Size size() const noexcept {
        Guard lock(mtx_);
        return static_cast<Size>(used_.size + reserved_);
    }

    [[nodiscard]] Size used_size() const noexcept {
        Guard lock(mtx_);
        return used_.size;
    }

    [[nodiscard]] Size reserved_size() const noexcept {
        Guard lock(mtx_);
        return reserved_;
    }

    [[nodiscard]] bool empty() const noexcept {
        Guard lock(mtx_);
        return used_.size == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        Guard lock(mtx_);
        return free_.size == 0;
    }

    [[nodiscard]] Size free_size() const noexcept {
        Guard lock(mtx_);
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
            Guard lock(mtx_);
            idx = pop_free_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        new (object_ptr(idx)) T(std::forward<Args>(args)...);

        {
            Guard lock(mtx_);
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
            Guard lock(mtx_);
            idx = pop_free_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        fn(object_ptr(idx));

        {
            Guard lock(mtx_);
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
        Guard lock(mtx_);
        Index idx = pop_used_unlocked();
        if (idx == -1) return false;
        out = std::move(*object_ptr(idx));
        object_ptr(idx)->~T();
        push_free_unlocked(idx);
        return true;
    }

    // ============================================================
    // 锁内访问
    // ============================================================

    /// @brief 按 FIFO 顺序遍历 used_ 中的所有元素，对每个元素调用 fn
    /// @return 若 fn 返回 true 则提前终止遍历
    /// @note 回调在锁内执行，必须 noexcept、快速、不重入本容器。
    template<typename F>
    void for_each(F&& fn) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, F&, T&>,
                      "F must be nothrow invocable, returning bool");
        Guard lock(mtx_);
        for (Index i = used_.head; i != -1; i = nodes_[i].next) {
            if (fn(*object_ptr(i))) return;
        }
    }

    /// @brief 锁内只读访问 used_ 尾部节点
    /// @return 队列为空返回 false；否则调用 fn(*tail) 并返回 true
    /// @note 回调在锁内执行，必须 noexcept、快速、不重入本容器。
    template<typename F>
    [[nodiscard]] bool last(F&& fn) const noexcept {
        static_assert(std::is_nothrow_invocable_r_v<void, F&, const T&>,
                      "F must be nothrow invocable with const T&");
        Guard lock(mtx_);
        if (used_.tail == -1) return false;
        fn(*object_ptr(used_.tail));
        return true;
    }

    // ============================================================
    // 长耗时消费：批量
    // ============================================================

    /// @brief 消费调用时刻 used_ 中的所有元素
    ///        内部：锁内 O(1) 整体摘出 → 锁外逐个 fn + 析构 → 锁内 O(1) 归还
    /// @note 回调 fn 在锁外执行，允许慢、阻塞、IO、再次访问本容器
    ///       （但不能递归调用 consume）。
    template<typename Consumer>
    void consume(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<Consumer, T*>,
                      "Consumer must be nothrow invocable with T*");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List tmp;
        {
            Guard lock(mtx_);
            detach_all_locked(tmp);
        }

        destroy_list_unlocked(tmp, fn);

        {
            Guard lock(mtx_);
            reclaim_locked(tmp);
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
    ///       适合"边消费边能被 Clear 看到剩余任务"的场景。
    ///       fn 与析构必须 noexcept。
    template<typename Consumer>
    bool consume_front(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<Consumer, T*>,
                      "Consumer must be nothrow invocable with T*");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        Index idx;
        {
            Guard lock(mtx_);
            idx = pop_used_unlocked();
            if (idx == -1) return false;
            ++reserved_;
        }

        fn(object_ptr(idx));        
        object_ptr(idx)->~T();      

        {
            Guard lock(mtx_);
            --reserved_;
            push_free_unlocked(idx);
        }
        return true;

    }

    /// @brief 从 used_ 头部开始消费，直到 fn 返回 true（该节点保留）或队列为空
    /// @return 消费掉的节点数
    /// @note 回调在锁内执行，必须 noexcept、快速、不重入本容器。
    ///       返回 true 表示"保留该节点并停止消费"。
    template<typename Consumer>
    Size consume_to(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Consumer, T*>,
                      "Consumer must be nothrow invocable with T*, returning bool");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List consume_list;
        {
            Guard lock(mtx_);
            while (used_.head != -1) {
                auto i = used_.head;
                if (fn(object_ptr(i))) break;

                auto idx = pop_used_unlocked();
                push_back_unlocked(consume_list, idx);
            }
            if (consume_list.size == 0) return 0;
            reserved_ += consume_list.size;
        }

        for (auto i = consume_list.head; i != -1; i = nodes_[i].next) {
            object_ptr(i)->~T();
        }

        {
            Guard lock(mtx_);
            reserved_ -= consume_list.size;
            splice_unlocked(free_, consume_list);
        }
        return consume_list.size;
    }

    // ============================================================
    // 锁内销毁
    // ============================================================

    /// @brief 移除所有满足谓词的元素
    /// @return 被移除的元素数量
    /// @note 谓词在锁内执行，必须 noexcept 且快。
    ///       析构在锁外执行，允许慢。
    template<typename Predicate>
    Size erase_if(Predicate pred) noexcept {
        static_assert(std::is_nothrow_invocable_r_v<bool, Predicate&, T&>,
                      "Predicate must be nothrow invocable returning bool");

        return erase_impl([&](List& dst) noexcept {
            return detach_if_locked(dst, pred);
        });
    }

    /// @brief 清空 used_ 中的所有元素
    /// @note 析构在锁外执行，允许慢。
    ///       不处理 reserved_ 节点。清空后 size() 可能不为 0。
    void clear() noexcept {
        erase_impl([this](List& dst) noexcept {
            return detach_all_locked(dst);
        });
    }

private:
    // ============================================================
    // 内部辅助：擦除骨架
    // ============================================================

    template<typename Detach>
    Size erase_impl(Detach detach) noexcept {
        static_assert(std::is_nothrow_destructible_v<T>,
                      "T must be nothrow destructible");

        List removed;
        {
            Guard lock(mtx_);
            if (detach(removed) == 0) return 0;
        }

        destroy_list_unlocked(removed, [](T*) noexcept {});

        {
            Guard lock(mtx_);
            reclaim_locked(removed);
        }
        return removed.size;
    }

    Size detach_all_locked(List& dst) noexcept {
        dst = used_;
        used_ = List{};
        reserved_ += dst.size;
        return dst.size;
    }

    template<typename Predicate>
    Size detach_if_locked(List& dst, Predicate pred) noexcept {
        Size count = 0;
        Index i = used_.head;
        while (i != -1) {
            Index next = nodes_[i].next;
            if (pred(*object_ptr(i))) {
                unlink_unlocked(used_, i);
                push_back_unlocked(dst, i);
                ++count;
            }
            i = next;
        }
        if (count > 0) reserved_ += count;
        return count;
    }

    template<typename F>
    void destroy_list_unlocked(List& list, F&& pre_destroy) noexcept {
        for (Index i = list.head; i != -1; i = nodes_[i].next) {
            pre_destroy(object_ptr(i));
            object_ptr(i)->~T();
        }
    }

    void reclaim_locked(List& lst) noexcept {
        reserved_ -= lst.size;
        splice_unlocked(free_, lst);
    }
};

// ============================================================
// 便捷别名
// ============================================================

/// @brief 线程安全版本（内部互斥锁）
template<typename T, std::int16_t Capacity>
using ThreadSafeList = MyList<T, Capacity, std::mutex>;

/// @brief 无锁版本（调用方保证无并发）
template<typename T, std::int16_t Capacity>
using UnsafeList = MyList<T, Capacity, NoLock>;

#endif  // MY_LIST_H_