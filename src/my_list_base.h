#ifndef MY_LIST_BASE_H_
#define MY_LIST_BASE_H_

#include <cstddef>
#include <cstdint>

/// @brief MyList 的非模板基类：只处理链表操作，不涉及 T
///
/// @note 【内部实现细节】不应被外部代码直接使用。
///       本文件位于 src/ 下，不属于公开 API。
///
/// 【目的】
///   把所有不依赖 T 的链表操作集中到非模板类，使多个
///   MyList<T, N> 实例共享同一份链表代码，减少代码膨胀。
///
/// 【NodeView 语义】
///   通过"首节点地址 + 步长"描述节点数组。
///   派生类保证 NodeBase 位于每个节点的偏移 0 处，
///   因此 node_at(view, i) 指向第 i 个节点的 NodeBase 子对象。
///
/// 【线程安全】
///   所有函数都是纯链表操作，不含锁——调用方负责同步。
class ListBase {
protected:
    using Index = std::int16_t;
    using Size  = std::int16_t;

    /// @brief 双向索引链表头
    struct List {
        Index head = -1;
        Index tail = -1;
        Size  size = 0;
    };

    /// @brief 节点的公共前缀（只含链表元数据）
    /// @note 派生类的具体节点必须以 NodeBase 作为第一个子对象，
    ///       保证其位于偏移 0 处。
    struct NodeBase {
        Index prev = -1;
        Index next = -1;
    };

    /// @brief 节点数组视图
    struct NodeView {
        void*       base;       // 首节点地址
        std::size_t stride;     // 相邻节点间距（字节）
    };

    /// @brief 取第 i 个节点的 NodeBase 指针
    static NodeBase* node_at(const NodeView& v, Index i) noexcept {
        return reinterpret_cast<NodeBase*>(
            static_cast<std::byte*>(v.base) +
            v.stride * static_cast<std::size_t>(i));
    }

    // ---------- 链表操作（静态，实现放 .cc）----------

    /// @brief 初始化 free 链：0 -> 1 -> ... -> capacity-1
    static void init_free_list(List& free, const NodeView& v,
                               Size capacity) noexcept;

    static Index pop_free_unlocked(List& free, const NodeView& v) noexcept;
    static void  push_free_unlocked(List& free, const NodeView& v,
                                    Index idx) noexcept;

    static Index pop_used_unlocked(List& used, const NodeView& v) noexcept;

    static void  push_back_unlocked(List& list, const NodeView& v,
                                    Index idx) noexcept;

    static void  unlink_unlocked(List& list, const NodeView& v,
                                 Index idx) noexcept;

    static void  splice_unlocked(List& dst, List& src,
                                 const NodeView& v) noexcept;
};

#endif  // MY_LIST_BASE_H_