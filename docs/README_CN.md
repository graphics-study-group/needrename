> 🇨🇳 中文版 | 🇺🇸 [English](../README.md)

一款游戏引擎，包含 GPU 加速的物理仿真、基于 Vulkan 的渲染系统、Python 驱动的反射/序列化机制，以及灵活的组件化游戏框架。

![engine_editor](../assets/img/engine_editor.png)

## 构建引擎

引擎支持两个平台：

- **Windows** — 使用 Visual Studio（VS2026，最低 VS2022）的 **MSVC** 工具链（多配置生成器，构建目录 `build/msvc`）。完整安装与构建说明：[`windows_msvc.md`](./build_instructions/windows_msvc.md)
- **Linux (WSL2)** — Clang + Ninja，需手动安装 LunarG Vulkan SDK 与 SDL3。完整安装与构建说明：[`linux.md`](./build_instructions/linux.md)

公共 CMake presets（`debug` / `release`）定义在 `CMakePresets.json`：Windows 增加 `msvc` + `msvc-debug` / `msvc-release`，Linux 增加 `linux-debug` / `linux-release`。平台相关的依赖安装、环境变量、Python 解释器配置见各平台文档。

## 项目结构

```
docs/                     # 贡献指南和代码规范
wiki/                    # 技术文档
assets/                  # 原始资源文件
builtin_assets/          # 内置资源，供所有项目通用
editor/                  # 引擎编辑器代码
engine/
    Asset/               # 资源核心基础设施（Asset、AssetRef、AssetManager、AssetDatabase）
    Core/                # 核心功能（数学库、功能模块）
    Framework/           # 顶层编排：MainClass、World/Scene/GameObject/Component、资源导入、Input
    Physics/             # GPU 加速的物理引擎
    Render/              # Vulkan 渲染系统、渲染资源与 GUISystem
    Rhi/                 # GPU 抽象层（缓冲区、纹理、管线、提交）
example/                 # 可运行的示例游戏
    physics_example/     # 物理仿真演示
projects/                # 示例游戏项目
test/                    # 测试程序
third_party/             # 第三方依赖（glm、SPIRV-Cross、AnnoRefl 等）
    AnnoRefl/            # 反射/序列化运行时与 Python 解析器
```

## 构建目标

引擎被拆分为多个模块化的共享库（DLL），依赖关系单向，由单个 `Engine` INTERFACE 聚合目标统一对外，使用方只需链接 `Engine`：

- **AnnoRefl** — 反射/序列化运行时与 Python 解析器（`third_party/AnnoRefl`）
- **EngineCore** — 数学库与功能模块
- **EngineRhi** — GPU 抽象层（缓冲区、纹理、管线、提交）
- **EngineAssetCore** — 资源核心基础设施（Asset、AssetRef、AssetManager、AssetDatabase）
- **EnginePhysics** — GPU 加速的物理引擎
- **EngineRender** — Vulkan 渲染系统、渲染资源与 GUISystem
- **EngineFramework** — 顶层编排（MainClass、World/Scene/GameObject/Component、资源导入、Input）
- **EngineEditor** — 引擎编辑器，由编辑器示例可执行文件加载
- **tests** — 可运行的演示程序和测试用例（可通过 CTest 运行）

所有可执行文件与 DLL 统一输出到 `bin/` 目录（导入库输出到 `lib/`）。

## 核心特性

### 1. GPU 物理仿真

![physics_example1](../assets/img/physics_example1.gif) ![physics_example2](../assets/img/physics_example2.gif)

- **XPBD 求解器** — GPU 加速的基于位置的动力学，支持子步积分、每步碰撞检测、Jacobi 位置/速度约束求解
- **碰撞检测管线** — 空间哈希粗筛阶段配合 AABB 重叠剪枝，随后进入基于 MPR 的窄相接触生成，包含平面拟合和旋转卡壳流形化简
- **碰撞形状** — 盒体、球体、圆柱体三种基本形状，各自具有惯性函数和通用的 `feature` vec3 接口
- **关节约束** — 固定关节（保持相对位姿）和铰链关节（单轴旋转，可配置限位），作为 XPBD 约束在 GPU 上求解
- **刚体动力学** — 重力、力/力矩积分、线速度/角速度阻尼、动态/运动学类型、摩擦和弹性恢复
- **GPU 并行算法** — 可复用的计算模块：工作高效的并行前缀扫描、8 位 LSD 基数排序、有序数组的去重压缩
- **物理组件** — `RigidBodyComponent`、`CollisionShapeComponent`、`PhysicsConstraintComponent` 与 GameObject 框架集成；碰撞形状自动挂载到祖先刚体
- **场景构建器** — 声明式 `SceneBuilder` API（`AddBox`、`AddSphere`、`AddCylinder`、`AddDoublePendulum`），快速搭建物理场景

### 2. Vulkan 渲染系统

- 多层描述符集架构管理 uniforms
- 帧间优化的缓冲区管理
- JSON 定义的材质与着色器管线配置
- 自动描述符集分配和绑定
- Push constants 支持高效的矩阵更新
- 物理与渲染子系统使用独立的渲染图

### 3. 高级反射与序列化

- Python 驱动的 C++ 头文件解析，生成运行时类型信息
- 编译时自动生成反射元数据
- 动态类实例化、方法调用和属性访问
- 支持 STL 容器和智能指针的可定制序列化
- 基于 JSON 的序列化格式，带对象关系追踪

### 4. 资源管理系统

- 基于 GUID 的资源标识系统
- 针对专用资源类型的自定义序列化
- 外部资源导入管线

### 5. GameObject 框架

- 层级对象系统，支持父子关系
- 组件化架构处理游戏逻辑
- 受控实例化的世界管理系统

## 文档

- [代码规范](./CODE_STYLE_CN.md) - 编码约定和最佳实践
- [贡献指南](./CONTRIBUTING_CN.md) - 如何参与项目贡献
- [技术维基](../wiki/) - 架构和 API 文档

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](../LICENSE)。
