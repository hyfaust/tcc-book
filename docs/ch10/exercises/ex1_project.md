# 练习 1: 扩展脚本引擎

## 背景

本练习基于 `examples/script_engine.c` 中的脚本引擎，要求你扩展其功能。

## 任务

### 基础扩展

1. **添加更多宿主 API**：在 `register_host_functions()` 中注册以下函数：
   - `host_read_file(const char *path)` - 读取文件内容返回字符串
   - `host_write_file(const char *path, const char *content)` - 写入文件
   - `host_getenv(const char *name)` - 获取环境变量
   - `host_sleep_ms(int ms)` - 休眠指定毫秒数

2. **添加 REPL 模式**：当脚本中定义了 `on_command(const char *line)` 函数时，用户输入的每一行都作为命令传递给脚本处理，实现交互式脚本。

3. **添加错误恢复**：当脚本编译失败时，保持旧版本脚本继续运行。修改 `script_load()` 使其在失败时不改变 `eng` 的状态。

### 高级扩展

4. **多脚本支持**：修改引擎支持同时加载多个脚本文件，每个脚本有自己的命名空间：
   ```
   load module_a.c
   load module_b.c
   call module_a.init()
   ```

5. **事件优先级**：修改事件分发机制，支持事件优先级和取消传播：
   ```c
   /* 脚本中返回 0 表示继续传播，非 0 表示取消 */
   int on_event(const char *event, const char *data) {
       if (!strcmp(event, "shutdown"))
           return 1;  /* 阻止关闭 */
       return 0;
   }
   ```

6. **性能监控**：添加事件处理时间统计，记录每个脚本每个事件的平均处理时间。

## 验证标准

- 基础扩展的所有新 API 能被脚本正确调用
- 编译失败时旧脚本继续运行
- REPL 模式正确工作
- 能用 GDB 调试引擎和脚本
