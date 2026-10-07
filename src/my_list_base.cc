#include "my_list_base.h"

// ============================================================
// ListBase 实现
// ============================================================
//
// 所有不依赖 T 的链表操作集中在这里——非模板函数，
// 全局只编译一次，被所有 MyList<T, N> 实例共享。

// ---------- 初始化 ----------

void ListBase::init_free_list(List& free, const NodeView& v,
                              Size capacity) noexcept {
    for (Size i = 0; i < capacity; ++i) {
        auto* n = node_at(v, i);
        n->prev = (i > 0) ? static_cast<Index>(i - 1) : -1;
        n->next = (i + 1 < capacity) ? static_cast<Index>(i + 1) : -1;
    }
    free.head = 0;
    free.tail = static_cast<Index>(capacity - 1);
    free.size = capacity;
}

// ---------- free 链 ----------

ListBase::Index ListBase::pop_free_unlocked(List& free,
                                            const NodeView& v) noexcept {
    if (free.head == -1) return -1;

    Index idx = free.head;
    auto* node = node_at(v, idx);

    free.head = node->next;
    if (free.head == -1) {
        free.tail = -1;
    } else {
        node_at(v, free.head)->prev = -1;
    }
    --free.size;
    return idx;
}

void ListBase::push_free_unlocked(List& free, const NodeView& v,
                                  Index idx) noexcept {
    auto* node = node_at(v, idx);
    node->prev = -1;
    node->next = free.head;

    if (free.head == -1) {
        free.tail = idx;
    } else {
        node_at(v, free.head)->prev = idx;
    }
    free.head = idx;
    ++free.size;
}

// ---------- used 链 ----------

ListBase::Index ListBase::pop_used_unlocked(List& used,
                                            const NodeView& v) noexcept {
    if (used.head == -1) return -1;

    Index idx = used.head;
    auto* node = node_at(v, idx);

    used.head = node->next;
    if (used.head == -1) {
        used.tail = -1;
    } else {
        node_at(v, used.head)->prev = -1;
    }
    --used.size;
    return idx;
}

// ---------- 通用链表操作 ----------

void ListBase::push_back_unlocked(List& list, const NodeView& v,
                                  Index idx) noexcept {
    auto* node = node_at(v, idx);
    node->prev = list.tail;
    node->next = -1;

    if (list.tail == -1) {
        list.head = idx;
    } else {
        node_at(v, list.tail)->next = idx;
    }
    list.tail = idx;
    ++list.size;
}

void ListBase::unlink_unlocked(List& list, const NodeView& v,
                               Index idx) noexcept {
    auto* node = node_at(v, idx);
    Index prev = node->prev;
    Index next = node->next;

    if (prev == -1) {
        list.head = next;
    } else {
        node_at(v, prev)->next = next;
    }
    if (next == -1) {
        list.tail = prev;
    } else {
        node_at(v, next)->prev = prev;
    }

    --list.size;
    node->prev = -1;
    node->next = -1;
}

void ListBase::splice_unlocked(List& dst, List& src,
                               const NodeView& v) noexcept {
    if (src.head == -1) return;

    if (dst.tail == -1) {
        dst.head = src.head;
        dst.tail = src.tail;
    } else {
        node_at(v, dst.tail)->next = src.head;
        node_at(v, src.head)->prev = dst.tail;
        dst.tail = src.tail;
    }
    dst.size = static_cast<Size>(dst.size + src.size);
    src = List{};
}