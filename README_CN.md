> 🇨🇳 中文版 | 🇺🇸 [English](./README.md)

一款游戏引擎，包含基于 Vulkan 的渲染系统、Python 驱动的反射/序列化机制，以及灵活的组件化游戏框架。

![engine_editor](./assets/engine_editor.png)

## 构建引擎

### 依赖项

- GCC 14 或更高版本
- CMake
- Python 3
- Vulkan SDK 1.3 或更高版本（已在 1.4.313 上测试）
- SDL3（已在 3.2.18 上测试）

其他第三方依赖可在 `third_party` 目录中找到。

在 Windows 环境下，建议使用 MSYS2。可通过以下命令配置环境：
```sh
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake
```
这会为 UCRT64 子系统安装 GCC 和 CMake。

Vulkan SDK 应从 LunarG 下载安装，**不要**从 MSYS2 的 `pacman` 仓库安装，因为后者缺少一些组件，且与 CMake 集成困难。

SDL3 也建议手动安装。
可以从 [其发布页面](https://github.com/libsdl-org/SDL/releases/) 获取。
选择 `SDL3-devel-3.X.XX-mingw.tar.gz`，解压到某处后：
- 新增环境变量 `SDL3_DIR`，指向 `SDL3-3.X.XX\cmake`
- 在 `PATH` 中添加 `SDL3-3.X.XX\x86_64-w64-mingw32\bin`
CMake 应能自动检测。

### 构建步骤

1. `git clone` 本仓库（带 `--recursive` 参数）
2. 配置并构建项目（需安装 Vulkan SDK）：
```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles"
mingw32-make
```

也可以使用你喜欢的 IDE。
如果在 Windows 上使用 Visual Studio Code，更新环境变量后，集成终端和 CMake 可能不会使用更新后的值。
确保在重启前杀死所有后台 VSCode 进程，或直接[重新登录/重启电脑](https://github.com/microsoft/vscode/issues/69289#issuecomment-467595799)。

## 项目结构

```
docs/                     # 贡献指南和代码规范
wiki/                    # 技术文档
assets/                  # 原始资源文件
builtin_assets/          # 内置资源，供所有项目通用
editor/                  # 引擎编辑器代码
engine/
    Asset/               # 资源管理
    Core/                # 核心功能（数学库、功能模块）
    Framework/           # GameObject、Component、Scene
    Reflection/          # 反射和序列化
    Render/              # Vulkan 渲染系统
    UserInterface/       # GUI 系统
example/                 # 可运行的示例游戏
projects/                # 示例游戏项目
reflection_parser/       # C++ 反射的 Python 解析器
test/                    # 测试程序
third_party/             # 第三方依赖（glm、SPIRV-Cross 等）
```

## 构建目标

- **editor**：运行引擎编辑器界面的可执行文件
- **engine**：包含核心引擎功能的静态库
- **tests**：可运行的演示程序和测试用例（可通过 CTest 运行）
- **third_party**：链接到 engine 的第三方静态库

## 核心特性

### 1. Vulkan 渲染系统

- 多层描述符集架构管理 uniforms
- 帧间优化的缓冲区管理
- JSON 定义的材质与着色器管线配置
- 自动描述符集分配和绑定
- Push constants 支持高效的矩阵更新

### 2. 高级反射与序列化

- Python 驱动的 C++ 头文件解析，生成运行时类型信息
- 编译时自动生成反射元数据
- 动态类实例化、方法调用和属性访问
- 支持 STL 容器和智能指针的可定制序列化
- 基于 JSON 的序列化格式，带对象关系追踪

### 3. 资源管理系统

- 基于 GUID 的资源标识系统
- 针对专用资源类型的自定义序列化
- 外部资源导入管线

### 4. GameObject 框架

- 层级对象系统，支持父子关系
- 组件化架构处理游戏逻辑
- 受控实例化的世界管理系统

## 文档

- [代码规范](./docs/CODE_STYLE_CN.md) - 编码约定和最佳实践
- [贡献指南](./docs/CONTRIBUTING_CN.md) - 如何参与项目贡献
- [技术维基](./wiki/) - 架构和 API 文档

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](./LICENSE)。
