// ════════════════════════════════════════════════════════════════════════════
//  my-linklist 单元测试
//
//  覆盖：
//    - 基础 FIFO / 容量边界 / 空列表边界
//    - 状态查询 / size 不变量 / reserved_size
//    - 所有消费接口（pop / consume / consume_front / consume_to）
//    - 所有销毁接口（clear / erase_if）
//    - 所有生产接口（emplace_back / construct）
//    - 所有遍历接口（for_each / last）
//    - NoLock 版本 / 非平凡类型 / 容量 1
//
//  契约说明：
//    MyList<T> 要求 T 从 Args 构造必须 noexcept——否则编译期就报错。
//    本文件里的所有辅助类型（Tracked / StringLike）都遵守这个约束。
// ════════════════════════════════════════════════════════════════════════════

#include "my_linklist.h"
#include "test_helpers.h"

#include <cstring>
#include <cstddef>

// ════════════════════════════════════════════════════════════════════════════
//  辅助类型
// ════════════════════════════════════════════════════════════════════════════

/// 追踪析构次数
struct Tracked {
    static int destroyed;
    int value;

    Tracked(int v = 0) noexcept : value(v) {}   // ← 必须 noexcept
    ~Tracked() { destroyed++; }
};
int Tracked::destroyed = 0;

/// 模拟"带资源的类型"——用栈缓冲区，构造 noexcept
/// 不真用堆分配——因为 new 可能抛 std::bad_alloc，无法满足 noexcept
struct StringLike {
    static int constructed;
    static int destroyed;

    static constexpr std::size_t kBufSize = 32;
    char data[kBufSize]{};

    explicit StringLike(const char* s) noexcept {
        std::size_t i = 0;
        while (s[i] && i < kBufSize - 1) {
            data[i] = s[i];
            ++i;
        }
        data[i] = '\0';
        constructed++;
    }
    ~StringLike() { destroyed++; }
};
int StringLike::constructed = 0;
int StringLike::destroyed   = 0;

// ════════════════════════════════════════════════════════════════════════════
//  一、基础 FIFO 行为
// ════════════════════════════════════════════════════════════════════════════

/// 新建列表：应为空，全部容量可用
static bool test_new_list_is_empty() {
    MyList<int, 8> list;

    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQ(list.used_size(), 0, "新建列表 used_size");
    TEST_ASSERT_EQ(list.free_size(), 8, "新建列表 free_size");
    TEST_ASSERT_EQ(list.capacity(),  8, "新建列表 capacity");
    TEST_ASSERT_FALSE(list.full());
    return true;
}

/// 入队一个元素：能弹出相同的值
static bool test_push_then_pop_same_value() {
    MyList<int, 8> list;

    TEST_ASSERT_TRUE(list.emplace_back(42));
    TEST_ASSERT_EQ(list.used_size(), 1, "入队后 used_size");
    TEST_ASSERT_EQ(list.free_size(), 7, "入队后 free_size");

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 42, "弹出的值");
    TEST_ASSERT_TRUE(list.empty());
    return true;
}

/// 多个元素：严格按 FIFO 顺序弹出
static bool test_fifo_order() {
    MyList<int, 8> list;
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_TRUE(list.emplace_back(i));
    }

    for (int expected = 0; expected < 8; expected++) {
        int out = 0;
        TEST_ASSERT_TRUE(list.pop_front(out));
        TEST_ASSERT_EQ(out, expected, "FIFO 弹出顺序");
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  二、容量边界
// ════════════════════════════════════════════════════════════════════════════

/// 满队后再入队：应被拒绝
static bool test_reject_when_full() {
    MyList<int, 3> list;

    TEST_ASSERT_TRUE (list.emplace_back(1));
    TEST_ASSERT_TRUE (list.emplace_back(2));
    TEST_ASSERT_TRUE (list.emplace_back(3));
    TEST_ASSERT_FALSE(list.emplace_back(4));    // 满了
    TEST_ASSERT_TRUE (list.full());
    return true;
}

/// 满队时 emplace_back 返回 false，used_size 不应改变
static bool test_emplace_back_full_no_side_effect() {
    MyList<int, 2> list;
    list.emplace_back(1);
    list.emplace_back(2);

    TEST_ASSERT_EQ(list.used_size(), 2, "入队 2 个后 used_size");
    TEST_ASSERT_FALSE(list.emplace_back(3));
    TEST_ASSERT_EQ(list.used_size(), 2, "拒绝后 used_size 不变");
    TEST_ASSERT_EQ(list.free_size(), 0, "拒绝后 free_size 不变");
    return true;
}

/// construct 满队时也应返回 false
static bool test_construct_rejects_when_full() {
    MyList<int, 2> list;

    TEST_ASSERT_TRUE (list.construct([](int* p) noexcept { new (p) int(1); }));
    TEST_ASSERT_TRUE (list.construct([](int* p) noexcept { new (p) int(2); }));
    TEST_ASSERT_FALSE(list.construct([](int* p) noexcept { new (p) int(3); }));

    TEST_ASSERT_TRUE(list.full());
    TEST_ASSERT_EQ(list.used_size(), 2, "满队后 used_size");
    return true;
}

/// 容量 1：入队、满队、弹出、再入队（槽位复用）
static bool test_capacity_one() {
    MyList<int, 1> list;

    TEST_ASSERT_TRUE(list.emplace_back(42));
    TEST_ASSERT_TRUE(list.full());

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 42, "值");
    TEST_ASSERT_TRUE(list.empty());

    TEST_ASSERT_TRUE(list.emplace_back(99));
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 99, "槽位复用");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  三、空列表边界
// ════════════════════════════════════════════════════════════════════════════

/// 空列表：所有消费接口应返回 false / 0 且不崩
static bool test_empty_list_operations() {
    MyList<int, 8> list;

    int out = 0;
    TEST_ASSERT_FALSE(list.pop_front(out));

    TEST_ASSERT_FALSE(list.consume_front([](int&) noexcept {}));

    TEST_ASSERT_EQ(list.consume_to([](int&) noexcept { return false; }),
                   0, "空列表 consume_to");

    TEST_ASSERT_FALSE(list.last([](int&) noexcept {}));

    int visited = 0;
    list.for_each([&](int&) noexcept {
        visited++;
        return false;
    });
    TEST_ASSERT_EQ(visited, 0, "空列表 for_each 不执行回调");

    list.clear();
    TEST_ASSERT_TRUE(list.empty());
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  四、状态查询 / 不变量
// ════════════════════════════════════════════════════════════════════════════

/// 正常状态下 reserved_size 应为 0
static bool test_reserved_size_zero_when_idle() {
    MyList<int, 8> list;
    TEST_ASSERT_EQ(list.reserved_size(), 0, "初始 reserved_size");

    list.emplace_back(1);
    list.emplace_back(2);
    TEST_ASSERT_EQ(list.reserved_size(), 0, "入队后 reserved_size");

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));              // ← 检查返回值
    TEST_ASSERT_EQ(list.reserved_size(), 0, "弹出后 reserved_size");
    return true;
}

/// size() 应始终等于 used_size() + reserved_size()
static bool test_size_invariant() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    TEST_ASSERT_EQ(list.size(),
                   list.used_size() + list.reserved_size(),
                   "size = used + reserved");

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));              // ← 检查返回值
    TEST_ASSERT_EQ(list.size(),
                   list.used_size() + list.reserved_size(),
                   "弹出后不变量");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  五、consume_front —— 逐个消费
// ════════════════════════════════════════════════════════════════════════════

/// 每次只消费一个元素，每次消费后 used_size 递减
static bool test_consume_front_one_by_one() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    int consumed = 0;
    while (list.consume_front([&](int&) noexcept {
        consumed++;
    })) {
        TEST_ASSERT_EQ(list.used_size(), 5 - consumed, "消费后 used_size");
    }

    TEST_ASSERT_EQ(consumed, 5, "总消费数");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  六、consume —— 批量消费
// ════════════════════════════════════════════════════════════════════════════

/// 一次性消费全部元素
static bool test_consume_all() {
    MyList<Tracked, 8> list;
    list.emplace_back();
    list.emplace_back();
    list.emplace_back();

    int count = 0;
    list.consume([&](Tracked&) noexcept {
        count++;
    });

    TEST_ASSERT_EQ(count, 3,            "consume 回调次数");
    TEST_ASSERT_EQ(list.used_size(), 0, "consume 后 used_size");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  七、consume_to —— 锁内条件消费
// ════════════════════════════════════════════════════════════════════════════

/// 消费到条件成立（节点值 == 4）为止，该节点保留
static bool test_consume_to_stops_at_predicate() {
    MyList<int, 8> list;
    for (int i = 0; i < 6; i++) list.emplace_back(i);

    auto consumed = list.consume_to([](int& v) noexcept -> bool {
        return v == 4;
    });

    TEST_ASSERT_EQ(consumed, 4,         "被消费的数量");
    TEST_ASSERT_EQ(list.used_size(), 2, "剩余数量");

    int value = 0;
    TEST_ASSERT_TRUE(list.pop_front(value));
    TEST_ASSERT_EQ(value, 4, "队头应是 4");
    return true;
}

/// 谓词永不返回 true → 应消费全部
static bool test_consume_to_consumes_all() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    auto consumed = list.consume_to([](int&) noexcept {
        return false;
    });

    TEST_ASSERT_EQ(consumed, 5,         "消费全部");
    TEST_ASSERT_EQ(list.used_size(), 0, "列表已空");
    return true;
}

/// 谓词立即返回 true → 应消费 0 个
static bool test_consume_to_stops_immediately() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    auto consumed = list.consume_to([](int&) noexcept {
        return true;
    });

    TEST_ASSERT_EQ(consumed, 0,         "消费 0 个");
    TEST_ASSERT_EQ(list.used_size(), 5, "列表不变");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  八、erase_if —— 按条件删除
// ════════════════════════════════════════════════════════════════════════════

/// 删除所有奇数：验证删除数量、剩余数量、剩余内容
static bool test_erase_if_removes_odd() {
    MyList<int, 8> list;
    for (int i = 0; i < 6; i++) list.emplace_back(i);

    int del[3] = {0};
    int index = 0;
    auto removed = list.erase_if([&](const int& item) noexcept {
        if (item % 2 != 0) {
            del[index++] = item;
            return true;
        }
        return false;
    });

    TEST_ASSERT_EQ(removed, 3,     "被删除的数量");
    TEST_ASSERT_EQ(list.size(), 3, "剩余数量");
    TEST_ASSERT_EQ(del[0], 1,      "被删除的第 1 项");
    TEST_ASSERT_EQ(del[1], 3,      "被删除的第 2 项");
    TEST_ASSERT_EQ(del[2], 5,      "被删除的第 3 项");

    for (int i = 0; i < 3; i++) {
        int out = 0;
        TEST_ASSERT_TRUE(list.pop_front(out));
        TEST_ASSERT_EQ(out, i * 2, "剩余元素应为偶数");
    }
    return true;
}

/// 谓词永不匹配 → 删除 0 个
static bool test_erase_if_no_match() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    auto removed = list.erase_if([](const int&) noexcept {
        return false;
    });

    TEST_ASSERT_EQ(removed, 0,          "删除 0 个");
    TEST_ASSERT_EQ(list.used_size(), 5, "列表不变");
    return true;
}

/// 谓词全部匹配 → 删除全部
static bool test_erase_if_removes_all() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    auto removed = list.erase_if([](const int&) noexcept {
        return true;
    });

    TEST_ASSERT_EQ(removed, 5,          "删除全部");
    TEST_ASSERT_EQ(list.used_size(), 0, "列表已空");
    TEST_ASSERT_EQ(list.free_size(), 8, "全部归还空闲链");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  九、clear —— 析构语义
// ════════════════════════════════════════════════════════════════════════════

/// clear() 必须逐个调用元素析构
static bool test_clear_destructs_all_elements() {
    Tracked::destroyed = 0;
    {
        MyList<Tracked, 8> list;
        list.emplace_back(1);
        list.emplace_back(2);
        list.emplace_back(3);

        TEST_ASSERT_EQ(Tracked::destroyed, 0, "clear 之前析构次数");
        list.clear();
        TEST_ASSERT_EQ(Tracked::destroyed, 3, "clear 之后析构次数");
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  十、construct —— 自定义构造
// ════════════════════════════════════════════════════════════════════════════

/// 在空槽上执行自定义构造逻辑，并验证尾部节点
static bool test_construct_with_custom_fn() {
    MyList<Tracked, 8> list;

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(list.construct([&](Tracked* t) noexcept {
            new (t) Tracked(i == 2 ? 200 : i);
        }));
    }

    int last_value = 0;
    TEST_ASSERT_TRUE(list.last([&](Tracked& t) noexcept {
        last_value = t.value;
    }));
    TEST_ASSERT_EQ(last_value, 200, "尾部节点的值");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  十一、for_each —— 锁内遍历
// ════════════════════════════════════════════════════════════════════════════

/// 回调返回 true 时提前退出
static bool test_for_each_early_exit() {
    MyList<Tracked, 8> list;
    list.emplace_back(0);
    list.emplace_back(1);
    list.emplace_back(200);

    int result[3] = {0, 0, 88};
    int index = 0;
    list.for_each([&](Tracked& t) noexcept {
        if (index == 2) return true;
        result[index++] = t.value;
        return false;
    });

    TEST_ASSERT_EQ(index,     2,  "for_each 访问的元素数");
    TEST_ASSERT_EQ(result[0], 0,  "result[0]");
    TEST_ASSERT_EQ(result[1], 1,  "result[1]");
    TEST_ASSERT_EQ(result[2], 88, "result[2] 应保持哨兵值");
    return true;
}

/// 完整遍历（永不返回 true）
static bool test_for_each_full_traversal() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i * 10);

    int sum = 0;
    int count = 0;
    list.for_each([&](int& v) noexcept {
        sum += v;
        count++;
        return false;
    });

    TEST_ASSERT_EQ(count, 5,                    "访问全部 5 个");
    TEST_ASSERT_EQ(sum, 0 + 10 + 20 + 30 + 40,  "累加和");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  十二、NoLock 版本
// ════════════════════════════════════════════════════════════════════════════

/// UnsafeList（NoLock）功能应与 ThreadSafeList 一致
static bool test_unsafe_list_basic() {
    UnsafeList<int, 8> list;

    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_TRUE(list.emplace_back(1));
    TEST_ASSERT_TRUE(list.emplace_back(2));

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 1, "FIFO 第 1 个");
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 2, "FIFO 第 2 个");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  十三、非平凡类型
// ════════════════════════════════════════════════════════════════════════════

/// 带资源的类型：构造 / 析构次数应匹配
static bool test_nontrivial_type_lifecycle() {
    StringLike::constructed = 0;
    StringLike::destroyed   = 0;

    {
        MyList<StringLike, 4> list;
        list.emplace_back("hello");
        list.emplace_back("world");

        TEST_ASSERT_EQ(StringLike::constructed, 2, "构造 2 次");
        TEST_ASSERT_EQ(StringLike::destroyed,   0, "尚未析构");
    }
    TEST_ASSERT_EQ(StringLike::destroyed, 2, "析构 2 次");
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  测试套件注册
// ════════════════════════════════════════════════════════════════════════════

static TestCase KTests[] = {
    // ── 基础 FIFO ──
    {"new_list_is_empty",                test_new_list_is_empty},
    {"push_then_pop_same_value",         test_push_then_pop_same_value},
    {"fifo_order",                       test_fifo_order},

    // ── 容量边界 ──
    {"reject_when_full",                 test_reject_when_full},
    {"emplace_back_full_no_side_effect", test_emplace_back_full_no_side_effect},
    {"construct_rejects_when_full",      test_construct_rejects_when_full},
    {"capacity_one",                     test_capacity_one},

    // ── 空列表边界 ──
    {"empty_list_operations",            test_empty_list_operations},

    // ── 状态查询 / 不变量 ──
    {"reserved_size_zero_when_idle",     test_reserved_size_zero_when_idle},
    {"size_invariant",                   test_size_invariant},

    // ── 逐个消费 ──
    {"consume_front_one_by_one",         test_consume_front_one_by_one},

    // ── 批量消费 ──
    {"consume_all",                      test_consume_all},

    // ── 条件消费 ──
    {"consume_to_stops_at_predicate",    test_consume_to_stops_at_predicate},
    {"consume_to_consumes_all",          test_consume_to_consumes_all},
    {"consume_to_stops_immediately",     test_consume_to_stops_immediately},

    // ── 条件删除 ──
    {"erase_if_removes_odd",             test_erase_if_removes_odd},
    {"erase_if_no_match",                test_erase_if_no_match},
    {"erase_if_removes_all",             test_erase_if_removes_all},

    // ── 析构语义 ──
    {"clear_destructs_all_elements",     test_clear_destructs_all_elements},

    // ── 自定义构造 ──
    {"construct_with_custom_fn",         test_construct_with_custom_fn},

    // ── 锁内遍历 ──
    {"for_each_early_exit",              test_for_each_early_exit},
    {"for_each_full_traversal",          test_for_each_full_traversal},

    // ── NoLock 版本 ──
    {"unsafe_list_basic",                test_unsafe_list_basic},

    // ── 非平凡类型 ──
    {"nontrivial_type_lifecycle",        test_nontrivial_type_lifecycle},
};

TEST_SUITE(test_my_linklist, KTests)