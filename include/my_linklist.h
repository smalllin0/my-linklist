#ifndef MY_LINKLIST_
#define MY_LINKLIST_

#include <array>
#include <mutex>
#include <optional>
#include <memory>
#include <type_traits>

using Index = int16_t;
using SizeType = int16_t;

template<typename T, SizeType Capacity>
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

        Iterator(MyList* owner, Index current_index)
            : owner_(owner), current_index_(current_index)
        {}
        reference operator*() const { return *(owner_->element_ptr(current_index_)); }
        pointer operator->() const { return owner_->element_ptr(current_index_); }
        

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
            return current_index_ == other.current_index_;
        }
        bool operator!=(const Iterator& other) const {
            return current_index_ != other.current_index_;
        }
    private:
        MyList*         owner_;
        Index           current_index_;
        
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

        ConstIterator(const MyList* owner, Index current_index)
            : owner_(owner), current_index_(current_index)
        {}
        reference operator*() const { return *owner_->element_ptr(current_index_); }
        pointer operator->() const { return owner_->element_ptr(current_index_); }
        

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
            return current_index_ == other.current_index_;
        }
        bool operator!=(const ConstIterator& other) const {
            return current_index_ != other.current_index_;
        }
    private:
        const MyList*   owner_;
        Index           current_index_;
        
        friend class MyList;
    };

    MyList() {
        for (SizeType i = 0; i < Capacity; i++) {
            nodes_[i].prev_index = -1;
            nodes_[i].next_index = i + 1;
        }
        nodes_[Capacity - 1].next_index = -1;
        free_list_head_ = 0;
        head_index_ = -1;
        tail_index_ = -1;
        count_ = 0;
    }
    ~MyList() { clear(); }

    MyList(const MyList&) = delete;
    MyList& operator=(const MyList&) = delete;
    MyList(MyList&&) = delete;
    MyList& operator=(MyList&&) = delete;
    
    SizeType size() const { 
        std::lock_guard<std::mutex> lock(lock_);
        return count_; 
    }
    bool empty() const { 
        std::lock_guard<std::mutex> lock(lock_);
        return count_ == 0; 
    }
    bool full() const { 
        std::lock_guard<std::mutex> lock(lock_);
        return count_ == Capacity; 
    }
    static constexpr SizeType capacity() { return Capacity; }
    
    // 非const版本的begin/end
    Iterator begin() { return Iterator(this, head_index_); }
    Iterator end() { return Iterator(this, -1); }
    
    // const版本的begin/end  
    ConstIterator begin() const { return ConstIterator(this, head_index_); }
    ConstIterator end() const { return ConstIterator(this, -1); }
    
    // 明确标记的const版本
    ConstIterator cbegin() const { return ConstIterator(this, head_index_); }
    ConstIterator cend() const { return ConstIterator(this, -1); }

    template<typename U>
    bool push_back(U&& data) {
        Index index;
        {
            std::lock_guard<std::mutex> lock(lock_);
            if (free_list_head_ == -1) {
                return false;
            }
            index = free_list_head_;
            free_list_head_ = nodes_[free_list_head_].next_index;
        }
        
        // 直接在其上使用placement new构造对象
        new (element_ptr(index)) T(std::forward<U>(data));
        {
            std::lock_guard<std::mutex> lock(lock_);
            nodes_[index].prev_index = tail_index_;
            nodes_[index].next_index = -1;

            if (tail_index_ != -1) {
                nodes_[tail_index_].next_index = index;
            } else {
                head_index_ = index;
            }
            tail_index_ = index;
            count_ ++;
        }
        return true;
    }
    
    /// @brief 直接在容器上进行构造[new (obj) Obj(args...);]
    /// @tparam Construct 用于构造对象的函数，它需要能够在指定地址构造
    /// @param fn 接受一个地址参数，应该在该地址上构造对象
    template<typename Construct>
    bool construct(Construct fn) {
        Index index;
        {
            std::lock_guard<std::mutex> lock(lock_);
            if (free_list_head_ == -1) {
                return false;
            }
            index = free_list_head_;
            free_list_head_ = nodes_[free_list_head_].next_index;
        }
        
        fn(element_ptr(index));
        {
            std::lock_guard<std::mutex> lock(lock_);
            nodes_[index].prev_index = tail_index_;
            nodes_[index].next_index = -1;

            if (tail_index_ != -1) {
                nodes_[tail_index_].next_index = index;
            } else {
                head_index_ = index;
            }
            tail_index_ = index;
            count_ ++;
        }

        return true;
    }

    std::unique_ptr<T> pop_front() {
        Index old_head;
        {
            std::lock_guard<std::mutex> lock(lock_);
            if (head_index_ == -1) {
                return nullptr;
            }
            old_head = head_index_;
            head_index_ = nodes_[old_head].next_index;
            if (head_index_ != -1) {
                nodes_[head_index_].prev_index = -1;
            } else {
                tail_index_ = -1;
            }
        }
        
        auto result = std::make_unique<T>(std::move(*element_ptr(old_head)));
        // 保留手动析构（因为这个对象会一直存在容器中）
        element_ptr(old_head)->~T();

        {
            std::lock_guard<std::mutex> lock(lock_);
            
            nodes_[old_head].next_index = free_list_head_;
            nodes_[old_head].prev_index = -1;
            free_list_head_ = old_head;
            count_ --;
        }

        return result;
    }
  
    /// @brief 消费头部元素（处理并销毁）
    /// @tparam Consumer 消费对象的函数
    /// @param fn 接受一个T*参数，处理对象后该对象会被立即销毁
    /// @note 消费函数不应抛出异常，否则会导致资源泄漏
    template<typename Consumer>
    void consume_front(Consumer fn) {
        Index old_head;
        {
            std::lock_guard<std::mutex> lock(lock_);

            if (head_index_ == -1) {
                return;
            }
            old_head = head_index_;
            // 更新链表结构
            head_index_ = nodes_[old_head].next_index;
            if (head_index_ != -1) {
                nodes_[head_index_].prev_index = -1;
            } else {
                tail_index_ = -1;
            }
        }
        
        // 先处理对象
        fn(element_ptr(old_head));
        // 然后销毁
        element_ptr(old_head)->~T();

        {
            std::lock_guard<std::mutex> lock(lock_);
            nodes_[old_head].next_index = free_list_head_;
            nodes_[old_head].prev_index = -1;
            free_list_head_ = old_head;
            count_ --;
        }
    }

    template<typename Predicate>
    SizeType remove_if(Predicate pred) {
        SizeType removed_count = 0;
        std::lock_guard<std::mutex> lock(lock_);
        auto current = head_index_;

        while(current != -1) {
            auto next = nodes_[current].next_index;
            if (pred(*element_ptr(current))) {
                remove_node_nolock(current);
                removed_count ++;
            }
            current = next;
        }
        return removed_count;
    }
    
    template<typename Predicate>
    void clear(Predicate pred) {
        std::lock_guard<std::mutex> lock(lock_);
        auto current = head_index_;
        while (current != -1) {
            auto next = nodes_[current].next_index;
            if (pred) pred(*element_ptr(current));
            element_ptr(current)->~T();
            current = next;
        }
        for (SizeType i = 0; i < Capacity; i++) {
            nodes_[i].prev_index = -1;
            nodes_[i].next_index = i + 1;
        }
        nodes_[Capacity - 1].next_index = -1;
        free_list_head_ = 0;
        head_index_ = -1;
        tail_index_ = -1;
        count_ = 0;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(lock_);
        auto current = head_index_;
        while (current != -1) {
            auto next = nodes_[current].next_index;
            element_ptr(current)->~T();
            current = next;
        }
        for (SizeType i = 0; i < Capacity; i++) {
            nodes_[i].prev_index = -1;
            nodes_[i].next_index = i + 1;
        }
        nodes_[Capacity - 1].next_index = -1;
        free_list_head_ = 0;
        head_index_ = -1;
        tail_index_ = -1;
        count_ = 0;
    }

private:
    T* element_ptr(Index index) {
        return reinterpret_cast<T*>(&nodes_[index].storage);
    }
    
    const T* element_ptr(Index index) const {
        return reinterpret_cast<const T*>(&nodes_[index].storage);
    }
    
    void remove_node_nolock(Index index) {
        if (nodes_[index].prev_index == -1) {
            head_index_ = nodes_[index].next_index;
        } else {
            nodes_[nodes_[index].prev_index].next_index = nodes_[index].next_index;
        }

        if(nodes_[index].next_index == -1) {
            tail_index_ = nodes_[index].prev_index;
        } else {
            nodes_[nodes_[index].next_index].prev_index = nodes_[index].prev_index;
        }

        element_ptr(index)->~T();       // 析构对象

        nodes_[index].next_index = free_list_head_;
        nodes_[index].prev_index = -1;
        free_list_head_ = index;

        count_ --;
    }

    struct Node {
        Index   prev_index = -1;
        Index   next_index = -1;
        alignas(T) std::byte storage[sizeof(T)];
    };

    mutable std::mutex          lock_;
    std::array<Node, Capacity>  nodes_;
    Index                       free_list_head_ = 0;
    Index                       head_index_ = -1;
    Index                       tail_index_ = -1;
    SizeType                    count_ = 0;
};

#endif
