# EUI-NEO-DX11

<p align="center">
  <img src="assets/icon.svg" width="104" alt="EUI icon">
</p>

<p align="center">
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/badge/license-Apache%202.0-blue"></a>
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Windows" src="https://img.shields.io/badge/platform-Windows-0078D4?logo=windows&logoColor=white">
  <img alt="Direct3D 11" src="https://img.shields.io/badge/rendering-Direct3D%2011%20%2B%20Direct2D-1f6feb">
  <img alt="Win32" src="https://img.shields.io/badge/windowing-Win32-111111">
  <img alt="MSVC v145" src="https://img.shields.io/badge/toolset-MSVC%20v145-5C2D91">
</p>

<p align="center">
  <a href="README.zh-CN.md">简体中文</a>
</p>

`EUI-NEO-DX11` is a Windows-only fork of EUI-NEO focused on a native Win32 + DirectX 11 stack. This fork uses a Win32 window backend, a DX11 renderer built on Direct3D 11 / Direct2D / DirectWrite, and targets C++20 through the Visual Studio project in this repository.

## Positioning

This fork is not the original cross-platform distribution. Its scope is intentionally narrower:

- Windows only
- Win32 window lifecycle and input
- Direct3D 11 swap chain and device management
- Direct2D / DirectWrite for 2D UI and text rendering
- Visual Studio project-based workflow
- C++20 as the required language standard

If you are looking for the original multi-backend, multi-platform distribution, use the upstream project rather than this fork.


## Preview

|  |  |
| --- | --- |
| ![preview 1](docs/pic/1.jpg) | ![preview 2](docs/pic/2.jpg) |
| ![preview 3](docs/pic/3.jpg) | ![preview 4](docs/pic/4.jpg) |
| ![example 1](docs/pic/示例1.jpg) | ![example 2](docs/pic/示例2.jpg) |

## Tech Stack

- Language: C++20
- Platform: Windows
- Window backend: Win32
- Render backend: Direct3D 11
- 2D drawing: Direct2D
- Text: DirectWrite
- Project format: Visual Studio `.vcxproj` / `.slnx`

The current project file is [`EUI-NEO-DX11.vcxproj`](EUI-NEO-DX11.vcxproj). It builds `Debug` / `Release` for both `Win32` and `x64`, and sets `LanguageStandard` to `stdcpp20`.

## Repository Layout

```text
assets/       Runtime assets: fonts, PNG, SVG, and icons
components/   Reusable UI components built on top of the DSL
core/         Runtime, DSL, rendering, platform, and Win32 integration
core/app/     App bootstrap and Win32 main entry
core/render/dx11/
              DX11 backend implementation
apps/         Application sources included in the Visual Studio project
examples/     Example sources retained from the original project
include/      Public facade headers
3rd/          Third-party source dependencies vendored into the repo
```

## Build

### Visual Studio

Requirements:

- Windows 10/11
- Visual Studio 2022
- MSVC toolset with C++20 support
- Windows SDK

Open `EUI-NEO-DX11.slnx` in Visual Studio, select `Debug` or `Release`, choose `Win32` or `x64`, then build the project.

### Command Line

Developer Command Prompt or PowerShell example:

```powershell
msbuild .\EUI-NEO-DX11.vcxproj /p:Configuration=Release /p:Platform=x64
```

The executable is produced under the standard Visual Studio output directory, for example:

```text
x64/Release/
```

## Runtime Notes

- This fork expects Windows-native graphics APIs and does not provide alternate rendering backends.
- Text rendering is handled through DirectWrite.
- UI composition and runtime behavior remain centered on the EUI DSL and component system, but the graphics path is specific to DX11 on Windows.
- The current DX11 path prioritizes direct rendering correctness. Runtime-side render cache and retained layer interfaces are present, but the backend currently falls back to direct drawing instead of maintaining offscreen cache resources.
- The DX11 backend can optionally adopt an external D3D11 / D2D / DWrite context through `eui/dx11.h` when a host application already owns the graphics pipeline. This is currently an experimental integration path intended for advanced host applications.
- Assets under `assets/` are part of the runtime and should remain available next to the built executable when the app is packaged.

## Entry Point

The application entry path for this fork is the Win32 app bootstrap under [`core/app/win32_app_main.cpp`](core/app/win32_app_main.cpp). The render backend resolves to DX11 through [`core/render/render_backend.cpp`](core/render/render_backend.cpp) and the implementation under [`core/render/dx11/`](core/render/dx11). External-context adoption is documented in [`docs/external_dx11_context.md`](docs/external_dx11_context.md).

## Status

This repository should be treated as a Windows-focused fork with its own engineering direction. Some files and documents inherited from upstream may still reference older cross-platform backends; when that conflicts with the actual code, the code in this fork is authoritative.

The external DX11-context adoption path is currently experimental. It is meant for advanced host integration, not yet a general compatibility guarantee for arbitrary third-party render loops.

## License

Original EUI-NEO source code remains under the Apache License 2.0. Third-party code under `3rd/` and bundled assets under `assets/` follow their respective upstream licenses and notices.

