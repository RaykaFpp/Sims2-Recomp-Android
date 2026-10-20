# Toolchain

The port builds on two hosts: Windows x64 (Direct3D 12) and Linux x64 (Vulkan). The Windows exe
can also be cross-built from Linux.

## Components

### Windows

| Component | Version / notes | Path |
| --- | --- | --- |
| clang / clang++ | LLVM clang (the install `tools/env.bat` expects) | `C:\Program Files\LLVM\bin` |
| lld (lld-link) | shipped with that LLVM install | `C:\Program Files\LLVM\bin` |
| Visual Studio 2022 Build Tools | MSVC x64 toolset, imported via `vcvars64.bat` | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat` |
| ninja | on PATH | — |
| cmake | on PATH | — |
| python | 3.12 | on PATH |
| git | on PATH (the SDK is a git submodule, see below) | — |

Exact installed versions are printed by `tools\env.bat` every time it runs
(`clang++ --version`, `ninja --version`, `cmake --version`); check there rather
than trusting this page.

### Linux

| Component | Notes |
| --- | --- |
| clang / clang++ | the `linux-*` presets name them directly |
| cmake, ninja | on PATH |
| Vulkan loader and headers | `vulkan-icd-loader`, `vulkan-headers`; a Vulkan driver at run time |
| glslang, SPIRV-Tools | the renderer compiles the title's shaders to SPIR-V at run time |
| SDL3 | built from source by the SDK's FetchContent, statically linked |
| python | 3.12, for `tools/xiso_extract.py` and `tools/xbe_info.py` |

`./build.sh` checks for clang, clang++, cmake and ninja before configuring and names what is
missing.

### Cross-building the Windows exe from Linux

| Component | Notes |
| --- | --- |
| clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt | from the system LLVM |
| [xwin](https://github.com/Jake-Shadle/xwin) splat | Microsoft CRT and Windows SDK, laid out in `/winsysroot` style |
| wine (optional) | to smoke-run the resulting exe without a Windows machine |

`cmake/windows-cross.cmake` carries the one-time xwin setup at the top and defaults `XWIN_DIR`
to `~/.cache/xwin/sysroot`.

The SDK is a git submodule at `sdk/`, from the fork
`https://github.com/abilliontokens/XboxXBERecomp.git`, branch `sims2-port-fixes`:

```
git submodule update --init sdk
```

## Why Clang

The SDK's generated C++ and its CMake presets require clang/clang++ with Ninja; MSVC alone will not build it. The MSVC environment is still imported, for its headers, libraries and linker-adjacent tools.

## Usage

Run `tools\env.bat` from `D:\sims2` before invoking `build.bat`, in the same shell:

```bat
cd /d D:\sims2
tools\env.bat
build.bat
```

`tools\env.bat` prepends `C:\Program Files\LLVM\bin` to `PATH` and imports the MSVC x64 environment (via `vcvars64.bat`) into the current shell; it has no `setlocal`, so the changes persist in that shell. An already-open shell does not see a fresh LLVM install until PATH is refreshed — either run `tools\env.bat` in that shell or open a new one after installing LLVM.

`build.bat` then configures the `release` preset, builds `xbe_symbols`, writes `config/game.toml` from the XBE and the SDK's XDK 5849 signatures if it is not already written, recompiles the XBE and builds `Sims2.exe` into `out/build/release`. The first build is long: it compiles one C++ function per guest function (15,926 functions).

## CMake presets

`CMakePresets.json` defines the Windows presets, the Linux presets and the cross build, all
Ninja, all clang-family with `-march=x86-64-v2`, each writing to `out/build/<preset>`:

| Preset | Host | `CMAKE_BUILD_TYPE` | Output |
| --- | --- | --- | --- |
| `debug` | Windows | `Debug` | `out/build/debug/Sims2.exe` |
| `release` | Windows | `Release` | `out/build/release/Sims2.exe` |
| `relwithdebinfo` | Windows | `RelWithDebInfo` | `out/build/relwithdebinfo/Sims2.exe` |
| `linux-debug` | Linux | `Debug` | `out/build/linux-debug/Sims2` |
| `linux-release` | Linux | `Release` | `out/build/linux-release/Sims2` |
| `linux-relwithdebinfo` | Linux | `RelWithDebInfo` | `out/build/linux-relwithdebinfo/Sims2` |
| `host-tools` | Linux | `Release` | `xbe_recomp` and `xbe_symbols` alone (`XBE_HOST_TOOLS_ONLY`) |
| `windows-cross` | Linux | `Release` | `out/build/windows-cross/Sims2.exe` |

```
cmake --preset release          # or linux-release
cmake --build out/build/release
```

The recompiler runs on the build host, so `windows-cross` consumes the natively built
`xbe_recomp` through `XBE_HOST_TOOLS_DIR`; build `host-tools` first. The generated code is
platform-neutral, so both targets share one `generated/` tree.

## Verbose RelWithDebInfo build (for debugging)

Release defines `NDEBUG`, which silences the SDK's `LOGI`/`LOGW` logging — exactly the output you want while bringing the port up. For a debuggable build with logging, optimisations and symbols, configure a **separate** build directory so it does not disturb `out/build/release`:

```
cmake -S . -B out/build/relwithdebinfo-verbose -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g"
cmake --build out/build/relwithdebinfo-verbose
```

That gives `out/build/relwithdebinfo-verbose/Sims2.exe` built `-O2 -g` without the Release `NDEBUG` behaviour. Keep using the plain `release` preset for the normal build in `out/build/release`.
