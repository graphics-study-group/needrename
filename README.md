An unnamed game engine with advanced features including Vulkan-based rendering, Python-powered reflection/serialization, and a flexible component-based game framework.

![engine_editor](./assets/engine_editor.png)

## Building the Engine

### Prerequisites

- Vulkan SDK
- Python
- CMake
- GCC

### Build Steps

1. git clone this repository
2. configure project (with Vulkan SDK installed) using cmake
3. create a virtual python environment at `reflection_parser/parser_env` with `reflection_parser/requirements.txt`
4. build project

## Project Structure

```
assets/                   # Some raw resources
builtin_assets/           # Some assets frequently-used in any games
editor/                   # Code of engine editor
engine/                   
├── __generated__/        # Auto-generated reflection code of the engine
├── Asset/                # Asset management
├── Core/                 # Core features such as Math
├── Exception             # Engine exceptions
├── Framework             # Main framework such as GameObject, Component
├── Functional            # Some small functional systems
├── Input                 # Input systems (mouse, keyboard, gamepad)
├── Reflection            # Reflection and serialization
├── Render                # Render systems
└── Docs/                 # Engine documents
example/                  # Some runable game examples
projects/                 # Some example game project (Can be opened by the engine)
reflection_parser/        # Python codes of the parser for C++ reflection and serialization
shader/                   # Shader codes
test/                     # Some runable test
thirdparty/               # External dependencies
```

## Key Features

### 1. Vulkan Rendering System

- Multi-tier descriptor set architecture for uniforms
- Frame-in-flight optimized buffer management
- JSON-defined materials with shader pipeline configuration
- Automatic descriptor set allocation and binding
- Push constant support for efficient matrix updates

### 2. Advanced Reflection & Serialization

- Python-powered C++ header parsing for runtime type information
- Automatic generation of reflection metadata during compilation
- Dynamic class instantiation, method invocation, and property access
- Customizable serialization with STL container and smart pointer support
- JSON-based serialization format with object relationship tracking

### 3. Asset Management

- GUID-based asset identification system
- Custom serialization for specialized asset types
- External resource import pipeline

### 4. GameObject Framework

- Hierarchical object system with parent-child relationships
- Component-based architecture for game logic
- World management with controlled instantiation