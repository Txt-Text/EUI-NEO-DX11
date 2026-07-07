# 外部 DX11 上下文接管

这个 fork 默认仍然使用自带的 Win32 + DX11 backend。普通使用者不需要做任何额外配置，直接创建窗口并运行即可。

当宿主程序已经拥有自己的 D3D11 / D2D / DWrite 管线时，可以在创建 EUI 渲染后端之前注册一个外部上下文，让 EUI 复用宿主资源，而不是再创建一套设备和交换链。

> 当前状态：实验性能力。
>
> 这条路径主要面向高级宿主集成场景，用于复用已有图形管线。它目前不是对任意第三方渲染循环、任意多窗口宿主结构或任意生命周期模型的通用兼容承诺。

## 入口

公共头文件：`include/eui/dx11.h`

可用 API：

- `eui::dx11::setExternalDx11Context(...)`
- `eui::dx11::hasExternalDx11Context()`
- `eui::dx11::hasExternalDx11Context(hwnd)`
- `eui::dx11::externalDx11Context()`
- `eui::dx11::externalDx11Context(hwnd)`
- `eui::dx11::clearExternalDx11Context()`
- `eui::dx11::clearExternalDx11Context(hwnd)`

## 上下文结构

`ExternalDx11Context` 只描述原生 DX11 / D2D / DWrite 资源，不依赖任何宿主框架类型。

必填成员：

- `hwnd`
- `d3dDevice`
- `d3dContext`
- `d2dContext`
- `dwriteFactory`

可选成员：

- `dxgiFactory`
- `d2dFactory`
- `d2dDevice`
- `swapChain`
- `d3dRenderTargetView`
- `d2dTargetBitmap`

行为标志：

- `hostManagesPresent`
- `hostManagesResize`
- `hostManagesBeginEndDraw`

## 推荐用法

对于已有宿主渲染循环的项目，建议：

- `hostManagesPresent = true`
- `hostManagesResize = true`
- `hostManagesBeginEndDraw = true`

这样 EUI 只负责发出绘制命令，不接管宿主窗口的 `Present`、`ResizeBuffers` 和 draw batch 生命周期。

从当前实现开始，外部上下文会按 `HWND` 注册和查找。对于多窗口宿主，每个窗口都应注册自己的 `ExternalDx11Context`，并在该窗口 resize 或 target 重建后用同一个 `HWND` 重新注册最新的原生指针。

## 当前边界

当前实现更适合下面这类场景：

- 单个宿主窗口已经拥有稳定的 DX11 / D2D / DWrite 管线
- 宿主愿意显式控制 `BeginDraw/EndDraw`、`Present` 和 resize
- EUI 作为宿主渲染流程中的一个 UI 绘制阶段接入
- 宿主在 resize 或 render target 重建后会重新注册该窗口的外部上下文

在公开发布时，建议不要把这条路径描述成：

- 任意宿主引擎即插即用
- 任意多窗口场景完全验证
- 任意生命周期模型下都可直接兼容

## VA 适配方向

如果宿主使用 `VAGraphicsDevice` / `VAGraphicsWindow`，只需要把它们暴露的原生指针填进 `ExternalDx11Context` 即可，不需要让 EUI 依赖 `VA` 头文件或类型。

可参考最小示例：[`docs/va_external_dx11_example.md`](docs/va_external_dx11_example.md)
