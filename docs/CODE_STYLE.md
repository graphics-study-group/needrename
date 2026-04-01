# Code Style Guide

## 1. Code Formatting

All code must be formatted using the `.clang-format` configuration file. Before committing, run:

```bash
clang-format -i <file>.cpp
```

Or for an entire directory:

```bash
find . -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## 2. Naming Conventions

### 2.1 Type Naming

- Classes, structs, enums: PascalCase (first letter capitalized, no underscores)
- Examples: `GameObject`, `TransformComponent`, `AssetManager`

### 2.2 Function Naming

- Member functions and free functions: PascalCase
- Examples: `GetTransform()`, `AddComponent()`, `LoadFromArchive()`

### 2.3 Variable Naming

- Member variables: `m_` prefix + snake\_case
- Examples: `m_position`, `m_rotation`, `m_scale`
- Local variables and function parameters: snake\_case (lowercase with underscores)
- Examples: `position`, `rotation`, `delta_time`

### 2.4 Constants and Macros

- ALL\_CAPS with underscores
- Examples: `MAX_BUFFER_SIZE`, `REFL_SER_CLASS`

### 2.5 Namespaces

- **Regular namespaces**: PascalCase
  - Examples: `namespace Engine`, `namespace Serialization`
- **`detail` namespace**: lowercase, reserved for internal implementation details
  - Used for helper types/functions not part of public API
- **Anonymous namespaces**: file-local symbols
  - Use `namespace { ... }` for functions/variables that should not be externally linked

```cpp
namespace Engine {

    namespace detail {      // Internal implementation details
        class HelperClass { ... };
    }

    class PublicClass { ... };

} // namespace Engine

namespace {                  // File-local, internal linkage
    void helper() { ... }
}
```

## 3. Commenting Guidelines

### 3.1 Documentation Comments

- Use Doxygen style with `/** */` format
- Example:
  ```cpp
  /**
   * @brief Get the transform matrix
   * @param transform The transform object
   * @return The transformed matrix
   */
  glm::mat4 GetTransformMatrix() const;
  ```

### 3.2 Inline Comments

- Use `//` prefix
- Comments should explain intent, avoid redundancy

### 3.3 Special Markers

- Use `[[deprecated]]` to mark deprecated functions

## 4. Code Organization

### 4.1 Header File Structure

```cpp
#ifndef MODULE_PATH_FILE_INCLUDED
#define MODULE_PATH_FILE_INCLUDED

// 1. Project header files
// 2. Third-party library header files
// 3. Standard library header files

namespace Engine {
    // Code
} // namespace Engine

#endif // MODULE_PATH_FILE_INCLUDED
```

### 4.2 Class Structure Order

- public -> protected -> private
- Constructor/destructor -> member functions -> member variables

### 4.3 Header Dependencies and Build Optimization

- Minimize dependencies on other headers in header files
- Prefer Forward Declaration to avoid unnecessary `#include`
- Only include headers when full type information is required (e.g., template instantiation)
- Consider using **Pimpl idiom** (Pointer to Implementation) to hide implementation details in `.cpp` files
- Examples:

  ```cpp
  // .h file - use forward declaration
  namespace Engine {
      class Scene; // Forward declaration, not #include "Scene.h"

      class GameObject {
      public:
          GameObject(Scene* scene); // Constructor only needs forward declaration
          Scene* GetScene() const;

      private:
          Scene* m_scene; // Only needs pointer size, no full definition required
      };
  }

  // .cpp file - include headers only when actually needed
  #include "Scene.h"
  #include "GameObject.h"
  ```

  Or using Pimpl:

  ```cpp
  // .h file
  class GameObject {
  public:
      GameObject();
      ~GameObject();
      void SetName(const std::string& name);
      const std::string& GetName() const;

  private:
      class Impl; // Forward declaration of implementation class
      std::unique_ptr<Impl> m_impl; // Hide member variables in Impl
  };

  // .cpp file
  #include "GameObject.h"
  #include <string>

  class GameObject::Impl {
  public:
      std::string m_name;
  };

  GameObject::GameObject() : m_impl(std::make_unique<Impl>()) {}
  GameObject::~GameObject() = default;
  ```

## 5. Miscellaneous

### 5.1 noexcept

- Functions that do not throw exceptions should be marked `noexcept`

### 5.2 const

- Member functions that do not modify member variables should be marked `const`

### 5.3 override

- Must use `override` when overriding base class virtual functions

### 5.4 Error Handling

- Use `assert` for assertion checks
- Example: `assert(ptr != nullptr && "Pointer must not be null");`
