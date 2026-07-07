# EUI-NEO-DX11

<p align="center">
  <img src="assets/icon.svg" width="104" alt="EUI 图标">
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
  <a href="README.md">English</a>
</p>

`EUI-NEO-DX11` 是 EUI-NEO 的一个 Windows 单平台分支，定位为原生 Win32 + DirectX 11 UI 框架实现。这个 fork 使用 Win32 窗口后端、基于 Direct3D 11 / Direct2D / DirectWrite 的渲染链路，并以仓库内的 Visual Studio 工程作为主要构建入口，语言标准为 C++20。

## 定位

这个 fork 不再维持上游仓库的跨平台目标，而是明确聚焦于：

- 仅支持 Windows
- 使用 Win32 处理窗口生命周期与输入
- 使用 Direct3D 11 管理设备、交换链和呈现
- 使用 Direct2D / DirectWrite 进行 2D UI 与文本渲染
- 以 Visual Studio 工程为主的开发工作流
- 以 C++20 作为必需语言标准

如果你需要原始上游那套多平台、多后端发行版，应以 upstream 项目为准，而不是这个 fork。


## 预览

|  |  |
| --- | --- |
| ![preview 1](docs/pic/1.jpg) | ![preview 2](docs/pic/2.jpg) |
| ![preview 3](docs/pic/3.jpg) | ![preview 4](docs/pic/4.jpg) |
| ![示例 1](docs/pic/示例1.jpg) | ![示例 2](docs/pic/示例2.jpg) |

## 技术栈

- 语言标准：C++20
- 运行平台：Windows
- 窗口后端：Win32
- 渲染后端：Direct3D 11
- 2D 绘制：Direct2D
- 文本系统：DirectWrite
- 工程格式：Visual Studio `.vcxproj` / `.slnx`

当前工程文件是 [`EUI-NEO-DX11.vcxproj`](EUI-NEO-DX11.vcxproj)。它提供 `Debug` / `Release` 与 `Win32` / `x64` 配置，并在工程内将 `LanguageStandard` 设为 `stdcpp20`。

## 仓库结构

```text
assets/       字体、PNG、SVG、图标等运行资源
components/   基于 DSL 的可复用组件
core/         Runtime、DSL、渲染、平台与 Win32 集成代码
core/app/     应用启动和 Win32 入口
core/render/dx11/
              DX11 后端实现
apps/         已纳入 Visual Studio 工程的应用源码
examples/     从原项目保留的示例源码
include/      公共 facade 头文件
3rd/          仓库内置第三方源码依赖
```

## 构建

### Visual Studio

环境要求：

- Windows 10 或 Windows 11
- Visual Studio 2022
- 支持 C++20 的 MSVC 工具链
- Windows SDK

直接用 Visual Studio 打开 `EUI-NEO-DX11.slnx`，选择 `Debug` 或 `Release`，再选择 `Win32` 或 `x64`，然后构建即可。

### 命令行

Developer Command Prompt 或 PowerShell 示例：

```powershell
msbuild .\EUI-NEO-DX11.vcxproj /p:Configuration=Release /p:Platform=x64
```

生成产物会输出到标准的 Visual Studio 目录，例如：

```text
x64/Release/
```

## 运行时说明

- 这个 fork 依赖 Windows 原生图形 API，不提供其他渲染后端。
- 文本渲染通过 DirectWrite 完成。
- UI 组合和运行时行为仍然基于 EUI DSL 与组件系统，但底层图形路径是 Windows + DX11 专用实现。
- 当前 DX11 路径优先保证直接绘制正确性。Runtime 侧仍保留 render cache 与 retained layer 接口，但后端目前会回退到 direct drawing，而不是维护离屏缓存资源。
- 当宿主程序已经拥有 D3D11 / D2D / DWrite 管线时，可以通过 `eui/dx11.h` 注册外部上下文，让 EUI 复用宿主图形资源。该接入路径当前属于实验性能力，主要面向高级宿主集成场景。
- `assets/` 下的资源属于运行时的一部分，打包时应确保它们与可执行文件保持可用关系。

## 入口与后端

这个 fork 的应用入口在 [`core/app/win32_app_main.cpp`](core/app/win32_app_main.cpp)。渲染后端通过 [`core/render/render_backend.cpp`](core/render/render_backend.cpp) 固定解析到 DX11，具体实现位于 [`core/render/dx11/`](core/render/dx11)。外部上下文接管方式见 [`docs/external_dx11_context.md`](docs/external_dx11_context.md)。

## 状态说明

这个仓库应被视为一个独立演进的 Windows 定向 fork。部分历史文件和文档可能仍然保留上游跨平台时期的描述；当文档和当前实现不一致时，以本 fork 的实际代码为准。

外部 DX11 上下文接管当前属于实验性能力，定位是高级宿主集成接口，而不是对任意第三方渲染循环的通用兼容承诺。

## 许可

EUI-NEO 的原创源码仍遵循 Apache License 2.0。`3rd/` 下的第三方代码以及 `assets/` 下随仓库分发的资源，遵循各自上游许可证和版权声明。

