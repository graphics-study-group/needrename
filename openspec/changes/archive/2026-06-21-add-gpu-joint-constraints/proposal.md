## Why

GPU XPBD 求解器目前仅解决碰撞接触约束 — 无法连接刚体。添加铰链和固定关节可以在正在进行的基于 Vulkan 的 GPU 物理管道中实现多体动力学（摆、链条、布娃娃、抓取机制）。

## 变更内容

- 新的 **PhysicsConstraintComponent**，可存储任意数量的 FixedJoint 或 HingeJoint 定义。位于“拥有”关节的物体（obj1）上；通过 ObjectHandle 引用 obj2。
- FixedJoint：在创建时维护两个物体之间的初始相对变换（位置和旋转）。每个关节可配置合规性。附加的局部附着点是根据初始约束条件计算得出的。
- HingeJoint：通过 obj1/obj2 局部空间中的对齐轴 + 附着点定义铰链。具有可配置合规性的 XPBD 位置级约束。
- PhysicsScene 获得用于固定和铰链关节的 CPU 端 SoA 向量，以及用于上传到 GPU 的打包 AoS 结构体缓冲区。
- 新的 GLSL 计算着色器：`accumulate_hinge_position.comp`、`accumulate_fixed_position.comp`，以及用于将拉格朗日乘数归零的清除内核。
- XPBDGpuSolver 子步循环集成了接触关节后的关节求解通道，重用相同的雅可比增量/计数缓冲区。
- SceneBuilder 获得 `AddDoublePendulum()` 演示，由顶部的运动学球体、两个由铰链连接的细长盒子以及底部通过 FixedJoint 连接的圆柱体组成。
- PhysicsScene 中的验证：obj2 必须具有 RigidBodyComponent，否则关节将被记录错误并忽略。

## 能力

### 新能力
- `physics-constraint-component`: 存储 FixedJoint 和 HingeJoint 定义并将其注册到 PhysicsScene 的 CPU 端约束组件
- `gpu-joint-buffers`: PhysicsScene 管理的 GPU 缓冲区，其中包含用于着色器调度的打包关节结构体（AoS）
- `gpu-joint-constraint-solving`: XPBD 计算着色器，用于在每个子步的位置迭代循环中并行解决铰链和固定关节
- `scene-builder-double-pendulum`: 用于创建具有铰链和固定关节的双摆演示的便利方法

### 修改的能力
- `physics-gpu-shaders`: 新的关节着色器添加到 XPBD 着色器管道中。`XPBDGpuSolver::Step()` 调度额外的关节约束计算通道。

## 影响

- 受影响的文件：
  - `engine/Framework/component/physics/` — 新的 PhysicsConstraintComponent.h/.cpp + 反射注册
  - `engine/Physics/PhysicsScene.h/.cpp` — 新的关节向量、GPU 缓冲区、注册/查找方法
  - `engine/Physics/Solver/XPBDGpuSolver.h/.cpp` — 新的 ComputeStage 成员、关节着色器加载、子步循环中的 dispatch
  - `engine/Physics/shader/solver/XPBDSolver/` — 新的 .comp 着色器 + SPIR-V
  - `example/physics_example/SceneBuilder.h/.cpp` — AddDoublePendulum 辅助方法
  - `example/physics_example/main.cpp` — 演示设置
- 对现有碰撞求解、接触速度求解或模型矩阵更新无破坏性更改 — 全部为附加性更改
- 现有演示不需要更改；新关节独立于现有实例
