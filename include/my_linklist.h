#ifndef MY_LINKLIST_
#define MY_LINKLIST_

#include <array>
#include <mutex>
#include <atomic>
#include <memory>
#include <type_traits>
#include <new>          // std::launder

using Index = int16_t;
using SizeType = int16_t;

template<typename T, SizeType Capacity>
class MyList {
    static_assert(Capacity > 0, "Capacity must be greater than 0");

private:
    struct Node {
        Index   prev_index = -1;
        Index   next_index = -1;
        alignas(T) std::byte storage[sizeof(T)];
    };

    std::array<Node, Capacity>  nodes_;

    struct Link {
        Node*       nodes{nullptr};
        Index       head{-1};       // 头部
        Index       tail{-1};       // 尾部
        std::atomic<SizeType> size{0};        // 大小
        mutable std::mutex  mutex;  // 互斥锁

        explicit Link(Node* n = nullptr)
            : nodes(n) {}


        Link(const Link&) = delete;
        Link& operator=(const Link&) = delete;

        // 移动语义：只搬运 head/tail/size，nodes 保持不变（指向同一数组）。
        // 不加锁；调用方应在已持有的临界区内调用。
        Link(Link&& other) noexcept
            : nodes(other.nodes)
            , head(other.head)
            , tail(other.tail)
            , size(other.size.load())
        {
            other.head = -1;
            other.tail = -1;
            other.size = 0;
        }

        Link& operator=(Link&& other) noexcept {
            if (this == &other) return *this;
            nodes = other.nodes;
            head  = other.head;
            tail  = other.tail;
            size  = other.size.load();
            other.head = -1;
            other.tail = -1;
            other.size = 0;
            return *this;
        }

        // 添加到头部
        void AddNodeToHead(Index index) {
            if (index == -1) return;
            std::lock_guard<std::mutex> lock(mutex);
            nodes[index].prev_index = -1;
            nodes[index].next_index = head;
            if (head == -1) {
                tail = index;
            } else {
                nodes[head].prev_index = index;
            }
            head = index;
            size++;
        }

        void AddNodeToTailWithoutLock(Index index) {
            if (index == -1) return;
            nodes[index].prev_index = tail;
            nodes[index].next_index = -1;
            if (tail == -1) {
                head = index;
            } else {
                nodes[tail].next_index = index;
            }
            tail = index;
            size++;
        }

        // 添加到尾部
        void AddNodeToTail(Index index) {
            std::lock_guard<std::mutex> lock(mutex);
            AddNodeToTailWithoutLock(index);
        }

        // 弹出头部(不消费内容)
        Index PopNodeFromHead() {
            std::lock_guard<std::mutex> lock(mutex);
            if (head == -1) return -1;
            auto node = head;
            head = nodes[node].next_index;
            if (head == -1) {
                tail = -1;
            } else {
                nodes[head].prev_index = -1;
            }
            size--;
            return node;
        }

        // 移出指定节点
        void RemoveNodeWithoutLock(Index index) {
            if (index == -1) return;
            auto prev = nodes[index].prev_index;
            auto next = nodes[index].next_index;

            if (prev == -1) {
                head = next;
            } else {
                nodes[prev].next_index = next;
            }

            if (next == -1) {
                tail = prev;
            } else {
                nodes[next].prev_index = prev;
            }
            size--;
        }

        // 拼接一个链表(被拼接对象归零)
        void ConcatWithoutLock(Link& other) {
            if (this == &other || other.head == -1) return;
            if (head == -1) {
                head = other.head;
                tail = other.tail;
            } else {
                nodes[tail].next_index = other.head;
                nodes[other.head].prev_index = tail;
                tail = other.tail;
            }
            size += other.size.load();
            other.head = -1;
            other.tail = -1;
            other.size = 0;
        }


        SizeType Size() const {
            return size.load();
        }
    };

    Link used_link_;
    Link free_link_;

public:
    // 非const迭代器
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator() : owner_(nullptr), current_index_(-1) {}
        Iterator(MyList* owner, Index current_index)
            : owner_(owner), current_index_(current_index)
        {}
        reference operator*() const { return *(owner_->eptr(current_index_)); }
        pointer operator->() const { return owner_->eptr(current_index_); }

        Iterator& operator++() {
            if (current_index_ != -1) {
                current_index_ = owner_->nodes_[current_index_].next_index;
            }
            return *this;
        }
        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        bool operator==(const Iterator& other) const {
            return owner_ == other.owner_ && current_index_ == other.current_index_;
        }
        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }
    private:
        MyList*         owner_;
        Index           current_index_;
    };

    // const迭代器
    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator() : owner_(nullptr), current_index_(-1) {}
        ConstIterator(const MyList* owner, Index current_index)
            : owner_(owner), current_index_(current_index)
        {}
        reference operator*() const { return *owner_->eptr(current_index_); }
        pointer operator->() const { return owner_->eptr(current_index_); }

        ConstIterator& operator++() {
            if (current_index_ != -1) {
                current_index_ = owner_->nodes_[current_index_].next_index;
            }
            return *this;
        }
        ConstIterator operator++(int) {
            ConstIterator tmp = *this;
            ++(*this);
            return tmp;
        }
        bool operator==(const ConstIterator& other) const {
            return owner_ == other.owner_ && current_index_ == other.current_index_;
        }
        bool operator!=(const ConstIterator& other) const {
            return !(*this == other);
        }
    private:
        const MyList*   owner_;
        Index           current_index_;

        friend class MyList;
    };

    MyList()
        : used_link_(nodes_.data())
        , free_link_(nodes_.data())
    {
        // 初始化空闲链表
        for (SizeType i = 0; i < Capacity; i++) {
            nodes_[i].next_index = (i + 1 < Capacity) ? (i + 1) : -1;
        }
        free_link_.head = 0;
        free_link_.tail = Capacity - 1;
        free_link_.size = Capacity;

        // 初始化已用链表
        used_link_.head = -1;
        used_link_.tail = -1;
        used_link_.size = 0;
    }
    ~MyList() { clear(); }

    MyList(const MyList&) = delete;
    MyList& operator=(const MyList&) = delete;
    MyList(MyList&&) = delete;
    MyList& operator=(MyList&&) = delete;

    [[nodiscard]] SizeType size() const {
        return used_link_.Size();
    }
    [[nodiscard]] bool empty() const {
        return used_link_.Size() == 0;
    }
    [[nodiscard]] bool full() const {
        return used_link_.Size() == Capacity;
    }
    static constexpr SizeType capacity() noexcept { return Capacity; }

    // 非const版本的begin/end
    Iterator begin() {
        std::lock_guard<std::mutex> lock(used_link_.mutex);
        return Iterator(this, used_link_.head);
    }
    Iterator end() {
        return Iterator(this, -1);
    }

    // const版本的begin/end
    ConstIterator begin() const {
        std::lock_guard<std::mutex> lock(used_link_.mutex);
        return ConstIterator(this, used_link_.head);
    }
    ConstIterator end() const {
        return ConstIterator(this, -1);
    }

    // 明确标记的const版本
    ConstIterator cbegin() const {
        std::lock_guard<std::mutex> lock(used_link_.mutex);
        return ConstIterator(this, used_link_.head);
    }
    ConstIterator cend() const {
        return ConstIterator(this, -1);
    }

    /// @brief 将元素添加到链表尾部
    /// @note T 必须能 noexcept 地从 U 构造，否则编译失败
    template<typename U>
    bool push_back(U&& data) {
        static_assert(std::is_nothrow_constructible<T, U>::value,
                      "T must be nothrow constructible from U");

        auto index = free_link_.PopNodeFromHead();
        if (index == -1) return false;
        new (eptr(index)) T(std::forward<U>(data));
        used_link_.AddNodeToTail(index);
        return true;
    }

    /// @brief 直接在容器上进行构造，添加到链表尾部
    /// @tparam Construct 用于构造对象的函数，接受一个 T* 地址并构造对象
    /// @param fn 应该在该地址上构造对象；不允许抛出异常（-fno-exceptions 下抛异常会 terminate）
    /// @note 若 fn 之后构造失败，节点会丢失（无法恢复）；请保证 fn 不会失败
    template<typename Construct>
    bool construct(Construct fn) {
        auto index = free_link_.PopNodeFromHead();
        if (index == -1) return false;
        fn(eptr(index));
        used_link_.AddNodeToTail(index);
        return true;
    }

    /// @brief 从链表头部弹出元素
    /// @note 使用此接口需要 T 具备 noexcept 移动构造；否则一旦失败节点会丢失
    [[nodiscard]] std::unique_ptr<T> pop_front() {
        static_assert(std::is_nothrow_move_constructible<T>::value,
                      "T类型移动构造函数必须为 noexcept");
        auto index = used_link_.PopNodeFromHead();
        if (index == -1) return nullptr;

        auto result = std::make_unique<T>(std::move(*eptr(index)));
        eptr(index)->~T();
        free_link_.AddNodeToHead(index);
        return result;
    }

    /// @brief 弹出头部元素（移动到外部对象，无动态内存分配）
    /// @note 使用此接口需要 T 具备 noexcept 移动赋值；否则一旦失败节点会丢失
    [[nodiscard]] bool pop_front(T& out) {
        static_assert(std::is_nothrow_move_assignable<T>::value,
                      "T must be nothrow move assignable");
        auto node = used_link_.PopNodeFromHead();
        if (node == -1) return false;

        out = std::move(*eptr(node));
        eptr(node)->~T();
        free_link_.AddNodeToHead(node);
        return true;
    }

    /// @brief 消费头部元素（处理并销毁）
    /// @tparam Consumer 消费对象的函数
    /// @param fn 接受一个 T* 参数，处理对象后该对象会被立即销毁
    /// @note 消费函数不应抛出异常，否则在 -fno-exceptions 下会 terminate
    template<typename Consumer>
    void consume_front(Consumer fn) {
        auto node = used_link_.PopNodeFromHead();
        if (node == -1) return;
        fn(eptr(node));
        eptr(node)->~T();
        free_link_.AddNodeToHead(node);
    }

    /// @brief 链式消费节点（减少消费时锁的占用）
    /// @note 多任务调用此接口会导致队列清空不彻底（!!!消费时所有节点会移到到临时链上，clear()接口不可见）
    /// @tparam Consumer 消费对象的函数，处理对象后该对象的析构函数会被调用
    /// @note 消费函数不应抛出异常，否则在 -fno-exceptions 下会 terminate
    template<typename Consumer>
    void consume(Consumer fn) noexcept {
        static_assert(std::is_nothrow_invocable_v<Consumer, T*>,
        "Consumer 必须能对 T* 进行 noexcept 调用，否则一旦抛出，"
        "临时链里未消费的节点会丢失。");

        Link tmp(nodes_.data());
        {
            std::lock_guard<std::mutex> lock(used_link_.mutex);
            tmp = std::move(used_link_);
        }

        // 临时链，无需加锁
        auto node = tmp.head;
        while (node != -1) {
            auto next = nodes_[node].next_index;
            fn(eptr(node));
            eptr(node)->~T();
            node = next;
        }

        {
            std::lock_guard<std::mutex> lock(free_link_.mutex);
            free_link_.ConcatWithoutLock(tmp);
        }
    }

    template<typename Predicate>
    [[nodiscard]] SizeType remove_if(Predicate pred) {
        static_assert(
            std::is_nothrow_invocable_r_v<bool, Predicate, T&>,
            "Predicate 必须能对 T& 进行 noexcept 调用，返回 bool"
        );
        Link tmp(nodes_.data());
        {
            std::lock_guard<std::mutex> lock(used_link_.mutex);
            auto current = used_link_.head;
            while(current != -1) {
                auto next = nodes_[current].next_index;
                if (pred(*eptr(current))) {
                    used_link_.RemoveNodeWithoutLock(current);
                    tmp.AddNodeToTailWithoutLock(current);
                }
                current = next;
            }
        }
        auto count = tmp.size.load();
        if (count == 0) return 0;

        // 析构临时链
        auto node = tmp.head;
        while(node != -1) {
            auto next = nodes_[node].next_index;
            eptr(node)->~T();
            node = next;
        }

        // 将临时链节点放回 free 链
        {
            std::lock_guard<std::mutex> lock(free_link_.mutex);
            free_link_.ConcatWithoutLock(tmp);
        }

        return count;
    }

    /// @brief 清空整个链表（自动析构）
    void clear() {
        Link tmp(nodes_.data());
        {
            std::lock_guard<std::mutex> lock(used_link_.mutex);
            tmp = std::move(used_link_);
        }

        auto current = tmp.head;
        while (current != -1) {
            auto next = nodes_[current].next_index;
            eptr(current)->~T();
            current = next;
        }

        {
            std::lock_guard<std::mutex> lock(free_link_.mutex);
            free_link_.ConcatWithoutLock(tmp);
        }
    }

private:
    T* eptr(Index index) {
        return std::launder(reinterpret_cast<T*>(&nodes_[index].storage));
    }

    const T* eptr(Index index) const {
        return std::launder(reinterpret_cast<const T*>(&nodes_[index].storage));
    }
};

#endif