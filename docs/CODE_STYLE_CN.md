# 代码规范

## 1. 代码格式化

所有代码必须通过 `.clang-format` 配置文件进行格式化。提交代码前请运行：

```bash
clang-format -i <file>.cpp
```

或对整个目录：

```bash
find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## 2. 命名规范

### 2.1 类型命名

- 类、结构体、枚举：PascalCase（首字母大写，无下划线）
- 示例：`GameObject`, `TransformComponent`, `AssetManager`

### 2.2 函数命名

- 成员函数和自由函数：PascalCase
- 示例：`GetTransform()`, `AddComponent()`, `LoadFromArchive()`

### 2.3 变量命名

- 成员变量：`m_` 前缀 + snake\_case
- 示例：`m_position`, `m_rotation`, `m_scale`
- 局部变量和函数参数：snake\_case（全小写，下划线分隔）
- 示例：`position`, `rotation`, `delta_time`

### 2.4 常量和宏

- 全大写 + 下划线分隔
- 示例：`MAX_BUFFER_SIZE`, `REFL_SER_CLASS`

### 2.5 命名空间

- 使用全小写命名空间
- 示例：`namespace Engine`

## 3. 注释规范

### 3.1 文档注释

- 使用 Doxygen 风格，`/** */` 格式
- 示例：
  ```cpp
  /**
   * @brief 获取变换矩阵
   * @param transform 变换对象
   * @return 变换后的矩阵
   */
  glm::mat4 GetTransformMatrix() const;
  ```

### 3.2 行内注释

- 使用 `//` 开头
- 注释应解释代码意图，避免冗余

### 3.3 特殊标记

- 使用 `[[deprecated]]` 标记废弃函数

## 4. 代码组织

### 4.1 头文件结构

```cpp
#ifndef MODULE_PATH_FILE_INCLUDED
#define MODULE_PATH_FILE_INCLUDED

// 1. 本项目头文件
// 2. 第三方库头文件
// 3. 标准库头文件

namespace Engine {
    // 代码
} // namespace Engine

#endif // MODULE_PATH_FILE_INCLUDED
```

### 4.2 类结构顺序

- public -> protected -> private
- 构造函数/析构函数 -> 成员函数 -> 成员变量

### 4.3 头文件依赖与编译优化

- 头文件中尽量减少对其他头文件的依赖
- 优先使用前向声明（Forward Declaration），避免不必要的 `#include`
- 仅在必须时（如需要完整类型信息、模板实例化等）才包含头文件
- 必要时可使用 **Pimpl idiom**（Pointer to Implementation）将实现细节隐藏到 `.cpp` 文件中
- 示例：

  ```cpp
  // .h 文件 - 使用前向声明
  namespace Engine {
      class Scene; // 前向声明，而非 #include "Scene.h"

      class GameObject {
      public:
          GameObject(Scene* scene); // 构造函数只需要前向声明
          Scene* GetScene() const;

      private:
          Scene* m_scene; // 只需要指针大小，无需完整定义
      };
  }

  // .cpp 文件 - 实际需要时再包含
  #include "Scene.h"
  #include "GameObject.h"
  ```

  或使用 Pimpl：

  ```cpp
  // .h 文件
  class GameObject {
  public:
      GameObject();
      ~GameObject();
      void SetName(const std::string& name);
      const std::string& GetName() const;

  private:
      class Impl; // 前向声明实现类
      std::unique_ptr<Impl> m_impl; // 将成员变量隐藏到 Impl 中
  };

  // .cpp 文件
  #include "GameObject.h"
  #include <string>

  class GameObject::Impl {
  public:
      std::string m_name;
  };

  GameObject::GameObject() : m_impl(std::make_unique<Impl>()) {}
  GameObject::~GameObject() = default;
  ```

## 5. 其他

### 5.1 noexcept

- 不会抛出异常的函数应标记 `noexcept`

### 5.2 const

- 不修改成员变量的成员函数应标记 `const`

### 5.3 override

- 重写基类虚函数时必须使用 `override`

### 5.4 错误处理

- 使用 `assert` 进行断言检查
- 示例：`assert(ptr != nullptr && "Pointer must not be null");`
