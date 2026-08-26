# Windows MSYS2 CLANG64 Build Instructions

This project uses the MSYS2 CLANG64 toolchain (`x86_64-w64-windows-gnu`) with the Ninja generator on Windows. Before running any cmake, build, ctest, or executable, you must set up the MSYS2 environment.

## Environment Variables

On Windows, when building with the MSYS2 CLANG64 toolchain, you **MUST** set these env vars in PowerShell before any cmake, build, ctest, or executable command:

```powershell
$env:MSYSTEM = "CLANG64"
$env:PATH = "<msys2_root>\clang64\bin;<msys2_root>\usr\bin;$env:PATH"
$env:VK_LAYER_PATH = "<msys2_root>\clang64\bin"
```

- `MSYSTEM=CLANG64` — selects the CLANG64 environment.
- `PATH` — must have `<msys2_root>/clang64/bin` and `<msys2_root>/usr/bin` **prepended** (before the existing `$env:PATH`), so the correct compilers and tools are found first.
- `VK_LAYER_PATH` — path to Vulkan validation layers for debug builds; the engine gracefully skips if missing at runtime.

## Finding `<msys2_root>`

`<msys2_root>` is the MSYS2 installation root directory. Find it in this order:

1. **Cached file** — check agent config dirs like `.copilot`, `.claude`, `.codex`, `.kilo` — whichever exists in the project root — for a `msys2_path.txt` file. Read the path from it.
2. **Common paths** — scan `C:\msys64` and `D:\msys64`.
3. **VSCode settings** — search `.vscode/settings.json` for `msys64`.
4. **Ask the user** — if all else fails, ask the user to provide the MSYS2 installation path.

## Caching the Path

Once `<msys2_root>` is found, save it to `<agent_config_dir>/msys2_path.txt` so future sessions can skip the search. The agent config dir is whichever of `.copilot` / `.claude` / `.codex` / `.kilo` (etc.) exists in the project root. This file must **NOT** be tracked by git.

## Python Environment (AnnoRefl Parser)

The reflection parser (`third_party/AnnoRefl/parser`) needs Python packages `libclang` and `mako`, installed in a user-managed virtual environment at the repository root. Create it once (from a shell with the CLANG64 environment active, per the env vars above):

```powershell
python -m venv .venv
.venv\bin\python -m pip install -r third_party\AnnoRefl\parser\requirements.txt
```
