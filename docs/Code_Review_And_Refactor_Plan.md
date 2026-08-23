# 项目代码与结构审查报告 (Code Review Report)

在对当前项目的代码规范、文件结构以及编译产物处理方式进行审查后，发现了一些不规范和不合理的地方。以下是详细的审查结果，供重构前 Review 使用。

## 1. 文件结构 (File Structure)
目前的文件结构比较混乱，缺乏标准 C++ 项目的模块化管理。
- **源码与根目录混杂**：像 `Main.cpp`, `IoUring.cpp`, `IoUring.h`, `json.hpp` 等核心源码或第三方库直接放在项目根目录，没有统一的 `src` 和 `include` 或 `third_party` 目录。
- **与非代码文件混杂**：根目录下充斥着 Python 脚本 (`*.py`)、庞大的日志文件 (`WebServer.log` 超过 100MB)、甚至有文档 (`王永刚.docx`)，这严重影响了项目的整洁度。
- **测试代码未完全分离**：存在 `tests` 目录，但根目录下也有 `LoggingTest` 等相关产物和文件。

## 2. 编译体系与产物处理 (Build System & Artifacts)
- **多套编译系统共存冲突**：项目同时存在 `Makefile` 和 `CMakeLists.txt`。其中 `CMakeLists.txt` 中的源码路径甚至已经过时（例如将 `Channel.cpp` 视作在根目录，而实际上在 `channel/` 中），维护两套系统容易造成不一致。
- **构建产物污染源码树**：不管是 `.o` 目标文件（如 `IoUring.o`, `Channel.o` 等）还是最终的可执行文件（如 `WebServer`, `HTTPClient` 等）都直接生成在源码目录中（Source内构建），没有做到 Out-of-source build。
- **日志产物无统一管理**：庞大的日志直接输出在项目根目录，应该建立专门的 `logs/` 文件夹，并配置日志切割或大小限制。
- **清理命令不规范**：`Makefile` 中的 `clean` 操作使用了 `find . -name '*.o' | xargs rm -f`，这种方式在较大项目中容易误删或效率低下，且不符合标准。

## 3. 代码规范与质量 (Code Specifications & Quality)
通过抽查 `IoUring.cpp`, `IoUring.h` 及 `Main.cpp` 等文件，发现以下问题：
- **命名规范不统一**：方法命名存在多种风格混用。例如大驼峰 `EqualAndUpdateLastEvents`、小驼峰 `add_timer`、下划线风格 `epoll_add` 等。成员变量命名也不统一（部分有 `_` 后缀，部分没有）。
- **错误处理过于粗暴**：在 `IoUring` 的构造函数初始化失败或 `Main.cpp` 中参数解析出错时，直接调用 `abort()` 强制退出程序。更优雅的做法是抛出异常或返回错误码交由上层处理。
- **C 风格头文件**：C++ 代码中大量包含了 `<assert.h>`, `<errno.h>`, `<poll.h>` 等 C 语言头文件，建议替换为标准 C++ 的 `<cassert>`, `<cerrno>` 等。
- **内存与资源分配不合理**：例如在 `IoUring.h` 中，直接在类内定义了大小为 100000 的大数组（如 `fd2chan_[MAXFDS]`）。对于智能指针数组来说，这样会导致实例创建时无论实际连接数多少都占用较大内存，建议改用 `std::unordered_map` 或动态扩展的 `std::vector`。
- **命名空间污染**：在全局作用域使用 `using namespace std;`（如 `IoUring.cpp` 中），这在大型工程中极易引起命名冲突。
- **魔术数字**：代码中出现了 hardcode 的魔数（如 `4096`, 端口号 `8088` 等），应当抽象为常量、宏或外部配置项。

## 4. 重构建议 (Refactoring Proposals)
1. **重组目录结构**：
   - 创建 `src/` 存放所有核心实现的 `*.cpp` 文件。
   - 创建 `include/` 存放所有公共模块暴露的 `*.h` 头文件。
   - 创建 `scripts/` 存放各种 `.py` 辅助构建和测试脚本。
   - 移动外部依赖到 `third_party/` (如 `json.hpp`)。
   - 创建 `docs/` (或者保留当前的 `doc/`) 存放设计文档及个人的 `.docx` 文件。
   - 运行时创建 `logs/` 目录用于存放日志。
2. **规范构建系统**：
   - 建议废弃 `Makefile`，全面统一至现代的 `CMake`，并修复 `CMakeLists.txt` 中的文件路径映射。
   - 强制执行 CMake 的 Out-of-source 构建模式，要求统一在 `build/` 目录下编译，可执行文件输出到 `bin/`。
   - 完善 `.gitignore`，忽略所有的构建产物和日志文件。
3. **代码清理与规范**：
   - 制定统一的命名规范（推荐基于 Google C++ Style Guide）并对现存代码进行重构。
   - 移除不必要的 `using namespace std;` 声明。
   - 优化内存申请逻辑，移除或改造定长超大数组。
   - 改进错误处理机制，移除直接调用的 `abort()`。

---
请您 Review 此文档，如果您对上述发现和重构建议没有异议，我们可以开始分步骤进行重构。
