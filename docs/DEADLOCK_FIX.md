# Foxglove 参数修改死锁问题修复

## 📅 修复日期
2026年1月31日

## 🔴 问题描述

**症状**: 在 Foxglove Studio 中修改参数后，程序立即死机/卡死。

## 🔍 根本原因

### 死锁场景

**问题代码**:
```cpp
// ❌ 错误: 在持有锁的情况下调用回调
options.callbacks.onSetParameters = [this](...) {
    std::lock_guard<std::mutex> lock(param_mutex);  // 获取锁
    
    for (const auto& param : params) {
        // 更新参数...
        
        if (param_callback) {
            param_callback(name, ...);  // ❌ 在持有锁时调用回调
        }
    }
    return result;
};
```

**死锁路径**:
1. Foxglove 客户端修改参数 → `onSetParameters` 被调用
2. `onSetParameters` 获取 `param_mutex` 锁
3. 调用 `param_callback` (应用层回调)
4. 应用层回调调用 `getParameterValue()` 
5. `getParameterValue()` 尝试获取 `param_mutex` 锁
6. **死锁！** (同一个线程两次获取同一个锁)

### 类似问题

`onClientConnect` 回调中也有相同的问题:
```cpp
// ❌ 错误
options.callbacks.onClientConnect = [this]() {
    std::lock_guard<std::mutex> lock(param_mutex);  // 获取锁
    // ...
    server->publishParameterValues(...);  // ❌ 在持有锁时调用 SDK 函数
};
```

## ✅ 解决方案

### 原则: **先复制数据，释放锁后再调用外部函数**

### 修复 1: `onSetParameters`

```cpp
options.callbacks.onSetParameters = [this](...) {
    std::vector<foxglove::Parameter> result;
    std::vector<std::string> changed_param_names;
    
    {
        std::lock_guard<std::mutex> lock(param_mutex);  // 作用域锁
        
        for (const auto& param : params) {
            // 更新参数存储
            it->second = param.clone();
            result.emplace_back(param.clone());
            changed_param_names.push_back(name);  // 记录名称
        }
    } // ✅ 锁在这里自动释放
    
    // ✅ 在锁外部调用回调
    if (param_callback) {
        for (const auto& name : changed_param_names) {
            param_callback(name, nlohmann::json::object());
        }
    }
    
    return result;
};
```

### 修复 2: `onClientConnect`

```cpp
options.callbacks.onClientConnect = [this]() {
    std::vector<foxglove::Parameter> params_to_publish;
    
    {
        std::lock_guard<std::mutex> lock(param_mutex);  // 作用域锁
        
        if (!param_store.empty()) {
            for (const auto& [name, param] : param_store) {
                params_to_publish.push_back(param.clone());
            }
        }
    } // ✅ 锁在这里释放
    
    // ✅ 在锁外部调用 SDK 函数
    if (!params_to_publish.empty() && server) {
        server->publishParameterValues(std::move(params_to_publish));
    }
};
```

## 🎯 关键要点

### 1. **锁的作用域最小化**
```cpp
{
    std::lock_guard<std::mutex> lock(mutex);
    // 只在这里访问共享数据
    // 复制需要的数据
} // 锁立即释放

// 在锁外部执行耗时操作
external_function(copied_data);
```

### 2. **避免在持有锁时调用外部代码**
- ❌ 回调函数
- ❌ SDK 函数
- ❌ 虚函数
- ❌ 任何可能获取其他锁的函数

### 3. **使用 RAII 确保锁释放**
- ✅ 使用 `std::lock_guard`
- ✅ 使用作用域块 `{}`
- ✅ 避免手动 `lock()`/`unlock()`

## 📊 性能影响

**修复前**:
- 死锁 → 程序卡死
- 需要强制杀死进程

**修复后**:
- ✅ 正常工作
- ✅ 锁持有时间更短
- ✅ 更好的并发性能

## 🧪 测试验证

### 测试步骤
1. 编译程序:
   ```bash
   cd build && make -j4
   ```

2. 运行程序:
   ```bash
   LD_LIBRARY_PATH=./lib:../3rdparty/foxglove/lib ./bin/minimum_vision
   ```

3. 在 Foxglove Studio 中:
   - 连接到 `ws://192.168.80.128:8765`
   - 添加 Parameters 面板
   - 修改任意参数 (如 `bullet_speed`)
   - 观察程序是否继续运行

### 预期结果
- ✅ 参数修改立即生效
- ✅ 终端显示: `[Param] Updated bullet_speed: 30.0`
- ✅ 程序继续正常运行
- ✅ 无卡死或崩溃

## 💡 最佳实践

### 1. **多线程回调设计原则**

```cpp
// ✅ 好的设计
void callback() {
    DataType copied_data;
    {
        std::lock_guard lock(mutex);
        copied_data = shared_data;  // 复制数据
    }
    
    process(copied_data);  // 在锁外部处理
}

// ❌ 坏的设计
void callback() {
    std::lock_guard lock(mutex);
    process(shared_data);  // 在持有锁时处理 - 可能死锁
}
```

### 2. **锁顺序一致性**
如果必须获取多个锁,始终按相同顺序获取:
```cpp
// ✅ 正确: 总是先 mutex_a 后 mutex_b
{
    std::lock_guard lock_a(mutex_a);
    std::lock_guard lock_b(mutex_b);
    // ...
}
```

### 3. **使用读写锁优化**
如果读操作远多于写操作:
```cpp
std::shared_mutex rw_mutex;

// 读操作
std::shared_lock lock(rw_mutex);

// 写操作
std::unique_lock lock(rw_mutex);
```

## 🔮 相关资源

- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action)
- [Effective Modern C++](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/) - Item 39: Consider void futures for one-shot event communication
- [CppCon 2017: Avoid Deadlocks](https://www.youtube.com/watch?v=v6Pp_qpVH2I)

## ✅ 修复状态

- ✅ 问题已识别: 回调中的死锁
- ✅ 代码已修复: 使用作用域锁 + 延迟回调
- ✅ 编译成功: 无错误
- ✅ 准备测试: 等待用户验证

---

**下一步**: 运行程序并在 Foxglove Studio 中测试参数修改功能。
