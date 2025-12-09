# MyList - 线程安全静态链表容器

## 特性

- ✅ **静态内存分配** - 编译期固定容量，无动态内存分配
- ✅ **对象池管理** - 高效的对象重用机制
- ✅ **线程安全** - 所有操作均线程安全
- ✅ **零拷贝优化** - 支持原地构造和消费操作
- ✅ **异常安全** - 关键操作提供基本异常安全保证

## 元素类型要求
1. 必须支持
- 移动构造函数 - 用于 pop_front() 和移动插入
- 析构函数 - 对象生命周期管理

2. 推荐支持
- 移动赋值运算符 - 优化移动操作
- 默认构造函数 - 灵活性
- 拷贝构造函数 - 兼容性
- 拷贝赋值运算符 - 兼容性

3. 异常安全要求
- 构造/析构函数 - 不应抛出异常
- consume_front处理函数 - 不应抛出异常、不能析构对象


## API 使用指南

### 1. 添加元素
```cpp
// 移动构造（推荐）
list.push_back(std::move(your_object));

// 原地构造（避免移动）
list.construct([](T* ptr) {
    new (ptr) T(arg1, arg2);  // 在指定地址直接构造
});
```

### 2. 移除元素
```cpp
// 移除并返回对象（移动语义）
auto obj = list.pop_front();  // 返回 std::unique_ptr<T>

// 消费并销毁（零拷贝，高性能）
list.consume_front([](T* obj) {
    obj->process();  // 直接处理对象
    // 对象在此后自动销毁，无需移动
});
```

### 3. 遍历元素
```cpp
// 非const遍历
for (auto& item : list) {
    item.process();
}

// const遍历
for (const auto& item : list) {
    std::cout << item << std::endl;
}

// 明确const遍历
for (auto it = list.cbegin(); it != list.cend(); ++it) {
    // *it 是 const 引用
}
```

### 4. 条件移除
```cpp
// 移除满足条件的元素
size_t removed = list.remove_if([](const T& obj) {
    return obj.should_remove();
});
```


