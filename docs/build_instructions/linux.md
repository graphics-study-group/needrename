# Linux Build Instructions

This project builds on Linux (Ubuntu 24.04 WSL2 verified) with the **Clang** toolchain and the **Ninja** generator, in addition to Windows MSYS2 CLANG64. Before any cmake, build, ctest, or executable command, make sure the toolchain and the Vulkan SDK environment are available.

## Environment Setup

Two things must be available in your shell:

1. **Clang + Ninja** on `PATH` (the `linux-*` presets call `clang`/`clang++`).
2. **Vulkan SDK** environment (`VULKAN_SDK`, `glslangValidator` on `PATH`, loader on `LD_LIBRARY_PATH`, validation layers via `VK_ADD_LAYER_PATH`, CMake prefix paths). This comes from sourcing the SDK's `setup-env.sh`.

The recommended setup sources the Vulkan SDK `setup-env.sh` from `~/.bashrc`, so interactive shells (including VS Code's integrated terminal and CMake Tools) pick it up automatically:

```sh
echo 'source ~/VulkanSDK/current/setup-env.sh' >> ~/.bashrc
```

> **Non-interactive shells** (scripts, CI, cron) do not read `~/.bashrc`. For those, source `setup-env.sh` explicitly at the start of the script:
> ```sh
> source ~/VulkanSDK/current/setup-env.sh
> ```

## Dependencies

| Dependency | Provisioning | Purpose |
|---|---|---|
| Clang (>= 19, verified 22) | **llvm.org apt preferred** (Ubuntu's `apt install clang` is usually an old 18.x); see below | C++ compiler |
| Ninja | `apt install ninja-build` | Build generator |
| CMake | `apt install cmake` (>= 3.20) | Build system |
| Vulkan SDK (>= 1.4.x) | **Manual install from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home)**, NOT apt | Headers (`vulkan.hpp`), loader, `glslangValidator`, validation layers |
| SDL3 | **Manual source build** (not in Ubuntu 24.04 apt) | Windowing / input |
| Python 3 + venv | `apt install python3-venv` | Reflection parser interpreter (user-managed `.venv`) |
| uuid | `apt install uuid-dev` | GUID generation |
| Mesa Vulkan drivers | `apt install mesa-vulkan-drivers` (usually already present) | Vulkan ICDs (WSLg GPU passthrough + lavapipe software) |
| Doxygen (optional) | `apt install doxygen` | Documentation generation |

### Install via apt

```sh
sudo apt update
sudo apt install ninja-build cmake python3-venv uuid-dev \
                 mesa-vulkan-drivers doxygen
```

### Clang

Ubuntu 24.04's default `apt install clang` provides an old 18.x which has known compiler crashes on this codebase. Install a recent version (>= 19, verified 22) from the [llvm.org apt repository](https://apt.llvm.org):

```sh
# llvm.org repo setup (example for clang-22 on Ubuntu 24.04 / noble)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 22

# Make clang-22 the default `clang`/`clang++` on PATH
sudo update-alternatives --install /usr/bin/clang clang /usr/lib/llvm-22/bin/clang 100 \
                         --slave /usr/bin/clang++ clang++ /usr/lib/llvm-22/bin/clang++
```

### Vulkan SDK (manual, >= 1.4.x)

The Vulkan SDK is **not** installed from the Linux package manager. The apt-provided `libvulkan-dev` (1.3.275 on Ubuntu 24.04) is too old for this engine (its `vulkan.hpp` lacks `vk::detail::resultCheck` and other APIs). Download a **>= 1.4.x** SDK tarball from <https://vulkan.lunarg.com/sdk/home>, extract it, and source its `setup-env.sh`:

```sh
# Example for 1.4.357.1 (replace with the version you download)
mkdir -p ~/VulkanSDK/1.4.357.1
tar -xf vulkansdk-linux-x86_64-1.4.357.1.tar.xz -C ~/VulkanSDK/1.4.357.1
ln -s ~/VulkanSDK/1.4.357.1 ~/VulkanSDK/current
echo 'source ~/VulkanSDK/current/setup-env.sh' >> ~/.bashrc
source ~/VulkanSDK/current/setup-env.sh
```

The SDK provides the `glslangValidator`/`glslc` shader compilers, the Vulkan loader, and the validation layers — no separate `glslang-tools` or `vulkan-validationlayers` apt packages are needed.

> **Do NOT install `libvulkan-dev` from apt.** The SDK's headers must win so the version is consistent. Keep `libvulkan1` and `mesa-vulkan-drivers` (system loader fallback and ICDs).

### SDL3 (manual source build)

Ubuntu 24.04 does not ship an SDL3 package (it appears in 24.10+ as `libsdl3-dev`). Build it from source (needs Wayland/X11 dev headers for WSLg):

```sh
sudo apt install libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxext-dev
git clone --depth 1 --branch release-3.2.0 https://github.com/libsdl-org/SDL.git
cd SDL && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && sudo cmake --install build
```

The engine's `find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)` locates the installed SDL3 (default `/usr/local` is on the CMake search path).

### Python interpreter for the reflection parser

The reflection parser uses a **user-provided** Python interpreter (no venv is auto-created). It must have `clang` and `mako` installed. Create a `.venv` in the project root and install the parser requirements:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r third_party/AnnoRefl/parser/requirements.txt
```

The shared `linux-debug`/`linux-release` presets set `Python3_EXECUTABLE` to `${sourceDir}/.venv/bin/python` (a repo-local `.venv` convention). If you use a different venv location, override it in a gitignored `CMakeUserPresets.json`:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "linux-debug-user",
            "inherits": "linux-debug",
            "cacheVariables": {
                "Python3_EXECUTABLE": "/absolute/path/to/your/.venv/bin/python"
            }
        }
    ]
}
```

If the interpreter lacks `clang`/`mako`, configuration fails with an actionable error message.

## Build Steps

```sh
# Configure (clang toolchain is set by the preset)
cmake --preset linux-debug

# Build
cmake --build --preset linux-debug

# Test
ctest --preset linux-debug
```

A `linux-release` preset is also available.
