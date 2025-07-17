An unnamed game engine with advanced features including Vulkan-based rendering, Python-powered reflection/serialization, and a flexible component-based game framework.

![engine_editor](./assets/engine_editor.png)

## Building the Engine

### Dependencies

- GCC 14 or greater
- CMake
- Python 3
- Vulkan SDK 1.3 or greater (Tested on 1.4.313)
- SDL3 (Tested on 3.2.18)

Other vendored dependencies can be found in the `third_party` directory.

When working on Windows, use of MSYS2 is suggested. You can set up the environment with
```sh
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake
```
which installs GCC and CMake for the UCRT64 subsystem.

Vulkan SDK should be downloaded and installed from LunarG, but *not* from MSYS2 repo with `pacman`, which misses some components and is difficult to integrate with CMake.

It is suggested that SDL3 should also be installed manually.
You can fetch it from [its release page](https://github.com/libsdl-org/SDL/releases/).
Pick `SDL3-devel-3.X.XX-mingw.tar.gz`, extract it somewhere, and add a `PATH` entry pointing to `SDL3-3.X.XX\x86_64-w64-mingw32\bin`.
CMake should be able to detect it automatically.

### Build Steps

1. `git clone` this repository (with `--recursive` flag)
2. Configure and build project (with Vulkan SDK installed) using cmake. Out-of-source build is preferred:
```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles"
mingw32-make
```

Or you can use your favorite IDE to do so.

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

## Build Targets

- **editor**: Executable (in `/editor`) that runs the engine editor interface
- **engine**: Static library (in `/engine`) containing core engine functionality  
- **tests**: Executable demos and test cases (runnable directly or via CTest)  
- **third_party**: Static third party libraries linked to `engine` containing dependencies  

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
