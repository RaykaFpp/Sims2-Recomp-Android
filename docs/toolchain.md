# Toolchain

## Components

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

The SDK is a git submodule at `sdk/`, from the fork
`https://github.com/abilliontokens/XboxXBERecomp.git`, branch `urbz-port-fixes`:

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

`CMakePresets.json` defines three presets, all Ninja, all clang/clang++ with `-march=x86-64-v2`, each writing to `out/build/<preset>`:

| Preset | `CMAKE_BUILD_TYPE` | Output |
| --- | --- | --- |
| `debug` | `Debug` | `out/build/debug` |
| `release` | `Release` | `out/build/release` |
| `relwithdebinfo` | `RelWithDebInfo` | `out/build/relwithdebinfo` |

```
cmake --preset release
cmake --build out/build/release
```

## Verbose RelWithDebInfo build (for debugging)

Release defines `NDEBUG`, which silences the SDK's `LOGI`/`LOGW` logging — exactly the output you want while bringing the port up. For a debuggable build with logging, optimisations and symbols, configure a **separate** build directory so it does not disturb `out/build/release`:

```
cmake -S . -B out/build/relwithdebinfo-verbose -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g"
cmake --build out/build/relwithdebinfo-verbose
```

That gives `out/build/relwithdebinfo-verbose/Sims2.exe` built `-O2 -g` without the Release `NDEBUG` behaviour. Keep using the plain `release` preset for the normal build in `out/build/release`.
