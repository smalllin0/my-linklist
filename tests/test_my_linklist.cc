#include "my_linklist.h"
#include "test_helpers.h"

// ==============================================================================
// 基础测试
// ==============================================================================

static bool test_empty() {
    MyList<int, 8> list;
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQ(list.size(), 0, "used_size");
    TEST_ASSERT_EQ(list.free_size(), 8, "capacity");
    TEST_ASSERT_FALSE(list.full());
    return true;
}

static bool test_emplace_pop() {
    MyList<int, 8> list;
    TEST_ASSERT_TRUE(list.emplace_back(42));
    TEST_ASSERT_EQ(list.capacity(), 8, "capacity");
    TEST_ASSERT_EQ(list.used_size(), 1, "used_size");
    TEST_ASSERT_EQ(list.free_size(), 7, "free_size");

    int out = 0;
    TEST_ASSERT_TRUE(list.pop_front(out));
    TEST_ASSERT_EQ(out, 42, "pop value");
    TEST_ASSERT_TRUE(list.empty());
    return true;
}

static bool test_full() {
    MyList<int, 3> list;
    TEST_ASSERT_TRUE(list.emplace_back(1));
    TEST_ASSERT_TRUE(list.emplace_back(2));
    TEST_ASSERT_TRUE(list.emplace_back(3));    
    TEST_ASSERT_FALSE(list.emplace_back(4));    // 满
    TEST_ASSERT_TRUE(list.full());
    return true;
}

static bool test_fifo_order() {
    MyList<int, 8> list;
    for (int i = 0; i < 8; i++) list.emplace_back(i);

    for (int i = 0; i < 8; i++) {
        int out = 0;
        TEST_ASSERT_TRUE(list.pop_front(out));
        TEST_ASSERT_EQ(out, i, "FIFO order");
    }
    return true;
}

// ==========================================================
// consume_front 逐个消费
// ==========================================================
static bool test_consume_front() {
    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);
    
    int consumed = 0;
    while (list.consume_front([&](int&) noexcept {
        consumed++;
    })) {
        TEST_ASSERT_EQ(list.used_size(), 5 - consumed, "used_size after consume.");
    }
    TEST_ASSERT_EQ(consumed, 5, "total consumed.");
    return true;
}

// ==========================================================
// clear 执行析构
// ==========================================================
struct Tracked {
    static int destroyed;
    int value;
    ~Tracked() { destroyed++; }
};
int Tracked::destroyed = 0;

static bool test_clear_destroy() {
    MyList<Tracked, 8> list;
    list.emplace_back();
    list.emplace_back();
    list.emplace_back();
    TEST_ASSERT_EQ(Tracked::destroyed, 0, "before clear");
    list.clear();
    TEST_ASSERT_EQ(Tracked::destroyed, 3, "after clear");
    return true;
}

// =========================================================
// erase_if 匹配清除
// =========================================================
static bool test_erase_if() {
    MyList<int, 8> list;
    for (int i = 0; i < 6; i++) list.emplace_back(i);   // 0, 1, 2, 3, 4, 5

    int del[3];
    int index = 0;
    auto removed = list.erase_if([&](const int& item) noexcept { 
        if (item % 2 != 0) {
            del[index++] = item;
            return true;
        }
        return false;
    });
    TEST_ASSERT_EQ(removed, 3, "removed count");
    TEST_ASSERT_EQ(list.size(), 3, "remaining count");
    TEST_ASSERT_EQ(del[0], 1, "del[0]");
    TEST_ASSERT_EQ(del[1], 3, "del[1]");
    TEST_ASSERT_EQ(del[2], 5, "del[2]");

    for (int i = 0; i < 3; i++) {
        int out = 0;
        TEST_ASSERT_TRUE(list.pop_front(out));
        TEST_ASSERT_EQ(out, i * 2, "poped item is true");
    }

    return true;
}

static bool test_construct() {
    MyList<Tracked, 8> list;

    for (int i = 0; i < 3; i++) {
        list.construct([&](Tracked* t) noexcept {
            new (t) Tracked(i == 2 ? 200 : i);
        });
    }
    int lastv = 0;
    bool ok = list.last([&](const Tracked t) noexcept{
        lastv = t.value;
    });
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQ(lastv, 200, "last value");


    int result[3]; result[2] = 88;
    int index = 0;
    list.for_each([&](Tracked& t) noexcept {
        if (index == 2) {
            return true;
        }
        result[index++] = t.value;
        return false;
    });
    TEST_ASSERT_EQ(index, 2, "for_each index");
    TEST_ASSERT_EQ(result[0], 0, "result[0]");
    TEST_ASSERT_EQ(result[1], 1, "result[1]");
    TEST_ASSERT_EQ(result[2], 88, "result[2]");
    
    int count = 0;
    list.consume([&](Tracked&) noexcept {
        count++;
    });
        TEST_ASSERT_EQ(3, count++, "consume");

    MyList<int, 8> list2;
    for (int i = 0; i < 6; i++) {
        list2.emplace_back(i);
    }
    list2.consume_to([](int& v) noexcept -> bool {
        return v == 4;
    });
    int value = 0;
    ok = list2.pop_front(value);
    ok = false;
    TEST_ASSERT_EQ(value, 4, "consume_to value");

    return true;
}


// =========================================================
// 测试套件
// =========================================================
static TestCase KTests[] = {
    {"empty", test_empty},
    {"emplace_pop", test_emplace_pop},
    {"full", test_full},
    {"fifo_order", test_fifo_order},
    {"consume_front", test_consume_front},
    {"clear_destroy", test_clear_destroy},
    {"erase_if", test_erase_if},
    {"construct", test_construct}
};

TEST_SUITE(test_my_linklist, KTests);
