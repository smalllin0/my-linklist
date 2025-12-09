#ifndef MY_LINKLIST_
#define MY_LINKLIST_

#include <array>
#include <mutex>
#include <optional>
#include <memory>
#include <type_traits>

using INDEX = int16_t;
using SIZE = int16_t;

template<typename T, SIZE Capacity>
class MyList {
public:
    // 非const迭代器
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator(MyList* list, INDEX current)
            : list_(list), current_(current)
        {}
        reference operator*() const { return *(list_->ptr(current_)); }
        pointer operator->() const { return list_->ptr(current_); }
        

        Iterator& operator++() {
            if (current_ != -1) {
                current_ = list_->node_[current_].next;
            }
            return *this;
        }
        Iterator operator++(int) {
            Iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        bool operator==(const Iterator& other) const {
            return current_ == other.current_;
        }
        bool operator!=(const Iterator& other) const {
            return current_ != other.current_;
        }
    private:
        MyList*         list_;
        INDEX           current_;
        
        friend class MyList;
    };

    // const迭代器
    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = const T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator(const MyList* list, INDEX current)
            : list_(list), current_(current)
        {}
        reference operator*() const { return *list_->ptr(current_); }
        pointer operator->() const { return list_->ptr(current_); }
        

        ConstIterator& operator++() {
            if (current_ != -1) {
                current_ = list_->node_[current_].next;
            }
            return *this;
        }
        ConstIterator operator++(int) {
            ConstIterator tmp = *this;
            ++(*this);
            return tmp;
        }
        bool operator==(const ConstIterator& other) const {
            return current_ == other.current_;
        }
        bool operator!=(const ConstIterator& other) const {
            return current_ != other.current_;
        }
    private:
        const MyList*   list_;
        INDEX           current_;
        
        friend class MyList;
    };

    MyList() {
        for (SIZE i = 0; i < Capacity; i++) {
            node_[i].prev = -1;
            node_[i].next = i + 1;
        }
        node_[Capacity - 1].next = -1;
        free_head_ = 0;
        used_head_ = -1;
        used_tail_ = -1;
        size_ = 0;
    }
    ~MyList() { clear(); }

    MyList(const MyList&) = delete;
    MyList& operator=(const MyList&) = delete;
    MyList(MyList&&) = delete;
    MyList& operator=(MyList&&) = delete;
    
    SIZE size() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return size_; 
    }
    bool empty() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0; 
    }
    bool full() const { 
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == Capacity; 
    }
    static constexpr SIZE capacity() { return Capacity; }
    
    // 非const版本的begin/end
    Iterator begin() { return Iterator(this, used_head_); }
    Iterator end() { return Iterator(this, -1); }
    
    // const版本的begin/end  
    ConstIterator begin() const { return ConstIterator(this, used_head_); }
    ConstIterator end() const { return ConstIterator(this, -1); }
    
    // 明确标记的const版本
    ConstIterator cbegin() const { return ConstIterator(this, used_head_); }
    ConstIterator cend() const { return ConstIterator(this, -1); }

    template<typename U>
    bool push_back(U&& data) {
        INDEX index;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (free_head_ == -1) {
                return false;
            }
            index = free_head_;
            free_head_ = node_[free_head_].next;
        }
        
        // 直接在其上使用placement new构造对象
        new (ptr(index)) T(std::forward<U>(data));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            node_[index].prev = used_tail_;
            node_[index].next = -1;

            if (used_tail_ != -1) {
                node_[used_tail_].next = index;
            } else {
                used_head_ = index;
            }
            used_tail_ = index;
            size_ ++;
        }
        return true;
    }
    
    /// @brief 直接在容器上进行构造[new (obj) Obj(args...);]
    /// @tparam Construct 用于构造对象的函数，它需要能够在指定地址构造
    /// @param fn 接受一个地址参数，应该在该地址上构造对象
    template<typename Construct>
    bool construct(Construct fn) {
        INDEX index;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (free_head_ == -1) {
                return false;
            }
            index = free_head_;
            free_head_ = node_[free_head_].next;
        }
        
        fn(ptr(index));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            node_[index].prev = used_tail_;
            node_[index].next = -1;

            if (used_tail_ != -1) {
                node_[used_tail_].next = index;
            } else {
                used_head_ = index;
            }
            used_tail_ = index;
            size_ ++;
        }

        return true;
    }

    std::unique_ptr<T> pop_front() {
        INDEX old_head;
        T* data_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (used_head_ == -1) {
                return nullptr;
            }
            auto old_head = used_head_;
            used_head_ = node_[old_head].next;
            if (used_head_ != -1) {
                node_[used_head_].prev = -1;
            } else {
                used_tail_ = -1;
            }
        }
        
        auto result = std::make_unique<T>(std::move(*ptr(old_head)));
        // 保留手动析构（因为这个对象会一直存在容器中）
        ptr(old_head)->~T();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            node_[old_head].next = free_head_;
            node_[old_head].prev = -1;
            free_head_ = old_head;
            size_ --;
        }

        return result;
    }
  
    /// @brief 消费头部元素（处理并销毁）
    /// @tparam Consumer 消费对象的函数
    /// @param fn 接受一个T*参数，处理对象后该对象会被立即销毁
    /// @note 消费函数不应抛出异常，否则会导致资源泄漏
    template<typename Consumer>
    void consume_front(Consumer fn) {
        INDEX old_head;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (used_head_ == -1) {
                return;
            }
            old_head = used_head_;
            // 更新链表结构
            used_head_ = node_[old_head].next;
            if (used_head_ != -1) {
                node_[used_head_].prev = -1;
            } else {
                used_tail_ = -1;
            }
        }
        
        // 先处理对象
        fn(ptr(old_head));
        // 然后销毁
        ptr(old_head)->~T();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            node_[old_head].next = free_head_;
            node_[old_head].prev = -1;
            free_head_ = old_head;
            size_ --;
        }
    }

    template<typename Predicate>
    SIZE remove_if(Predicate pred) {
        SIZE removed_count = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        auto current = used_head_;

        while(current != -1) {
            auto next = node_[current].next;
            if (pred(*ptr(current))) {
                remove_node_unsafe(current);
                removed_count ++;
            }
            current = next;
        }
        return removed_count;
    }
    
    template<typename Predicate>
    void clear(Predicate pred) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto current = used_head_;
        while (current != -1) {
            auto next = node_[current].next;
            if (pred) pred(*ptr(current));
            ptr(current)->~T();
            current = next;
        }
        for (SIZE i = 0; i < Capacity; i++) {
            node_[i].prev = -1;
            node_[i].next = i + 1;
        }
        node_[Capacity - 1].next = -1;
        free_head_ = 0;
        used_head_ = -1;
        used_tail_ = -1;
        size_ = 0;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto current = used_head_;
        while (current != -1) {
            auto next = node_[current].next;
            ptr(current)->~T();
            current = next;
        }
        for (SIZE i = 0; i < Capacity; i++) {
            node_[i].prev = -1;
            node_[i].next = i + 1;
        }
        node_[Capacity - 1].next = -1;
        free_head_ = 0;
        used_head_ = -1;
        used_tail_ = -1;
        size_ = 0;
    }

private:
    T* ptr(INDEX index) {
        return reinterpret_cast<T*>(&node_[index].data);
    }
    
    const T* ptr(INDEX index) const {
        return reinterpret_cast<const T*>(&node_[index].data);
    }
    
    void remove_node_unsafe(INDEX index) {
        if (node_[index].prev == -1) {
            used_head_ = node_[index].next;
        } else {
            node_[node_[index].prev].next = node_[index].next;
        }

        if(node_[index].next == -1) {
            used_tail_ = node_[index].prev;
        } else {
            node_[node_[index].next].prev = node_[index].prev;
        }

        ptr(index)->~T();       // 析构对象

        node_[index].next = free_head_;
        node_[index].prev = -1;
        free_head_ = index;

        size_ --;
    }

    struct Node {
        INDEX   prev = -1;
        INDEX   next = -1;
        alignas(T) std::byte data[sizeof(T)];
    };

    mutable std::mutex          mutex_;
    std::array<Node, Capacity>  node_;
    INDEX                       free_head_ = 0;
    INDEX                       used_head_ = -1;
    INDEX                       used_tail_ = -1;
    SIZE                        size_ = 0;
};

#endif
