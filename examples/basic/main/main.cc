// ════════════════════════════════════════════════════════════════════════════
//  my-linklist 示例程序
//
//  目的：
//    - 验证组件能在 ESP-IDF 环境下正确编译
//    - 展示 MyList 的核心 API 用法
//    - 在真机上跑一遍主要功能，日志输出结果
//
//  运行：
//    idf.py set-target esp32c3
//    idf.py build flash monitor
// ════════════════════════════════════════════════════════════════════════════

#include "my_linklist.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>

static const char* TAG = "my-linklist-demo";

// ════════════════════════════════════════════════════════════════════════════
//  Demo 1：基础 FIFO
// ════════════════════════════════════════════════════════════════════════════

static void demo_basic_fifo() {
    ESP_LOGI(TAG, "=== Demo 1: 基础 FIFO ===");

    MyList<int, 8> list;

    // 入队
    for (int i = 0; i < 5; i++) {
        list.emplace_back(i * 10);
    }
    ESP_LOGI(TAG, "入队 5 个：used=%d free=%d",
             (int)list.used_size(), (int)list.free_size());

    // 出队
    int out = 0;
    while (list.pop_front(out)) {
        ESP_LOGI(TAG, "  弹出：%d", out);
    }
    ESP_LOGI(TAG, "全部弹出后：empty=%d", (int)list.empty());
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 2：容量边界
// ════════════════════════════════════════════════════════════════════════════

static void demo_capacity() {
    ESP_LOGI(TAG, "=== Demo 2: 容量边界 ===");

    MyList<int, 3> list;

    // 填满
    for (int i = 0; i < 3; i++) {
        bool ok = list.emplace_back(i);
        ESP_LOGI(TAG, "  emplace_back(%d) -> %s", i, ok ? "OK" : "FAIL");
    }

    // 再入队——应拒绝
    bool ok = list.emplace_back(99);
    ESP_LOGI(TAG, "  满队后 emplace_back(99) -> %s", ok ? "OK" : "REJECT");
    ESP_LOGI(TAG, "  full=%d", (int)list.full());
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 3：consume_front 逐个消费
// ════════════════════════════════════════════════════════════════════════════

static void demo_consume_front() {
    ESP_LOGI(TAG, "=== Demo 3: consume_front ===");

    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i);

    int count = 0;
    while (list.consume_front([&](int& v) noexcept {
        ESP_LOGI(TAG, "  消费：%d", v);
        count++;
    })) {
        // 每次消费后 used_size 递减
    }
    ESP_LOGI(TAG, "总共消费 %d 个", count);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 4：consume 批量消费
// ════════════════════════════════════════════════════════════════════════════

static void demo_consume_all() {
    ESP_LOGI(TAG, "=== Demo 4: consume 批量 ===");

    MyList<int, 8> list;
    for (int i = 0; i < 5; i++) list.emplace_back(i * 100);

    int sum = 0;
    list.consume([&](int& v) noexcept {
        sum += v;
        ESP_LOGI(TAG, "  批处理：%d", v);
    });
    ESP_LOGI(TAG, "累加和 = %d", sum);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 5：consume_to 条件消费
// ════════════════════════════════════════════════════════════════════════════

static void demo_consume_to() {
    ESP_LOGI(TAG, "=== Demo 5: consume_to（遇到 3 停止）===");

    MyList<int, 8> list;
    for (int i = 0; i < 6; i++) list.emplace_back(i);

    auto n = list.consume_to([](int& v) noexcept -> bool {
        return v == 3;                  // 遇到 3 停止，保留它
    });
    ESP_LOGI(TAG, "消费了 %d 个；used_size = %d", (int)n, (int)list.used_size());

    int head = 0;
    list.pop_front(head);
    ESP_LOGI(TAG, "剩下的队头 = %d（应为 3）", head);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 6：erase_if 条件删除
// ════════════════════════════════════════════════════════════════════════════

static void demo_erase_if() {
    ESP_LOGI(TAG, "=== Demo 6: erase_if（删除偶数）===");

    MyList<int, 8> list;
    for (int i = 0; i < 6; i++) list.emplace_back(i);

    auto removed = list.erase_if([](const int& v) noexcept {
        return v % 2 == 0;              // 删除偶数
    });
    ESP_LOGI(TAG, "删除了 %d 个；used_size = %d",
             (int)removed, (int)list.used_size());

    list.for_each([](int& v) noexcept {
        ESP_LOGI(TAG, "  剩余：%d", v);
        return false;
    });
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 7：for_each 遍历 + 提前退出
// ════════════════════════════════════════════════════════════════════════════

static void demo_for_each() {
    ESP_LOGI(TAG, "=== Demo 7: for_each ===");

    MyList<int, 8> list;
    for (int i = 1; i <= 5; i++) list.emplace_back(i * i);

    // 完整遍历
    int sum = 0;
    list.for_each([&](int& v) noexcept {
        sum += v;
        return false;
    });
    ESP_LOGI(TAG, "所有元素之和 = %d", sum);

    // 提前退出：遇到 9 停止
    int visited = 0;
    list.for_each([&](int& v) noexcept {
        visited++;
        return v == 9;
    });
    ESP_LOGI(TAG, "遇到 9 时已访问 %d 个", visited);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 8：last 访问尾部
// ════════════════════════════════════════════════════════════════════════════

static void demo_last() {
    ESP_LOGI(TAG, "=== Demo 8: last ===");

    MyList<int, 8> list;

    // 空列表
    bool ok = list.last([](int& v) noexcept {
        ESP_LOGI(TAG, "  尾部：%d", v);
    });
    ESP_LOGI(TAG, "空列表 last -> %d", (int)ok);

    // 非空
    list.emplace_back(10);
    list.emplace_back(20);
    list.emplace_back(30);

    int tail = 0;
    ok = list.last([&](int& v) noexcept {
        tail = v;
    });
    ESP_LOGI(TAG, "尾部值 = %d（应为 30）", tail);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 9：construct 自定义构造
// ════════════════════════════════════════════════════════════════════════════

struct Point {
    int x;
    int y;

    Point(int x_, int y_) noexcept : x(x_), y(y_) {}
};

static void demo_construct() {
    ESP_LOGI(TAG, "=== Demo 9: construct 自定义构造 ===");

    MyList<Point, 4> list;

    list.construct([](Point* p) noexcept {
        new (p) Point(1, 2);
    });
    list.construct([](Point* p) noexcept {
        new (p) Point(3, 4);
    });

    list.for_each([](Point& p) noexcept {
        ESP_LOGI(TAG, "  Point(%d, %d)", p.x, p.y);
        return false;
    });
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 10：非平凡类型（生命周期验证）
// ════════════════════════════════════════════════════════════════════════════

struct Tracked {
    static int constructed;
    static int destroyed;

    char name[16];

    explicit Tracked(const char* n) noexcept {
        strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        constructed++;
    }
    ~Tracked() { destroyed++; }
};
int Tracked::constructed = 0;
int Tracked::destroyed   = 0;

static void demo_nontrivial_lifecycle() {
    ESP_LOGI(TAG, "=== Demo 10: 非平凡类型生命周期 ===");

    {
        MyList<Tracked, 4> list;
        list.emplace_back("hello");
        list.emplace_back("world");

        ESP_LOGI(TAG, "  作用域内：constructed=%d destroyed=%d",
                 Tracked::constructed, Tracked::destroyed);
    }
    // 离开作用域 → MyList 析构 → clear → 全部析构

    ESP_LOGI(TAG, "  作用域外：constructed=%d destroyed=%d（应相等）",
             Tracked::constructed, Tracked::destroyed);
}

// ════════════════════════════════════════════════════════════════════════════
//  Demo 11：NoLock 版本（单线程用）
// ════════════════════════════════════════════════════════════════════════════

static void demo_unsafe_list() {
    ESP_LOGI(TAG, "=== Demo 11: UnsafeList（NoLock）===");

    UnsafeList<int, 8> list;

    list.emplace_back(1);
    list.emplace_back(2);
    list.emplace_back(3);

    int out = 0;
    while (list.pop_front(out)) {
        ESP_LOGI(TAG, "  弹出：%d", out);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  入口
// ════════════════════════════════════════════════════════════════════════════

extern "C" void app_main() {
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  my-linklist 示例程序                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");

    demo_basic_fifo();
    demo_capacity();
    demo_consume_front();
    demo_consume_all();
    demo_consume_to();
    demo_erase_if();
    demo_for_each();
    demo_last();
    demo_construct();
    demo_nontrivial_lifecycle();
    demo_unsafe_list();

    ESP_LOGI(TAG, "全部示例执行完毕 ✓");
}