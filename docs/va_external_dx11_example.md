# VA 最小接入示例

本文给出一个最小的 `VAGraphicsDevice` / `VAGraphicsWindow` 接入示例，用于让宿主程序复用已有 `DX11 + D2D + DWrite` 管线，并把 EUI 作为宿主渲染流程中的一个 UI 阶段接入。

> 当前状态：实验性示例。
>
> 目标是说明最小接线路径，而不是提供一个覆盖所有宿主生命周期模型的通用模板。

## 思路

接入分成三步：

1. 宿主先初始化 `VAGraphicsDevice` 和 `VAGraphicsWindow`
2. 把宿主暴露的原生指针填入 `eui::dx11::ExternalDx11Context`
3. 在创建 EUI 渲染后端之前注册这个上下文

推荐配置：

- `hostManagesPresent = true`
- `hostManagesResize = true`
- `hostManagesBeginEndDraw = true`

这意味着：

- 宿主负责 `BeginDraw/EndDraw`
- 宿主负责 `Present`
- 宿主负责窗口 resize 和 swap chain resize
- EUI 只复用上下文并发出绘制命令

## 适配函数

下面这个函数只做一件事：把 `VA` 的设备与窗口对象转换为 `ExternalDx11Context`。

```cpp docs/va_external_dx11_example.md
#include "eui/dx11.h"
#include <VADX11Engine/VAGraphicsDevice.hpp>
#include <VADX11Engine/VAGraphicsWindow.hpp>

inline eui::dx11::ExternalDx11Context makeEuiExternalContext(
    VectorAura::Engine::VAGraphicsDevice& device,
    VectorAura::Engine::VAGraphicsWindow& window) {
    eui::dx11::ExternalDx11Context context;
    context.hwnd = window.GetWindowHandle();

    context.d3dDevice = device.GetD3DDevice();
    context.d3dContext = device.GetD3DContext();
    context.dxgiFactory = device.GetDXGIFactory();

    context.d2dDevice = device.GetD2DDevice();
    context.d2dContext = window.GetD2DContext();
    context.dwriteFactory = device.GetDWriteFactory();

    context.swapChain = window.GetSwapChain();
    context.d3dRenderTargetView = window.GetD3DRenderTarget();

    context.hostManagesPresent = true;
    context.hostManagesResize = true;
    context.hostManagesBeginEndDraw = true;
    return context;
}
```

如果宿主还持有 `ID2D1Bitmap1*` 形式的当前 target bitmap，也可以继续填 `context.d2dTargetBitmap`。当前示例不要求这一步。

## 注册时机

关键点是：

- 先创建和初始化 `VA` 图形对象
- 再注册 `ExternalDx11Context`
- 最后再创建 EUI 渲染后端或进入 EUI app 初始化流程

最小示意：

```cpp docs/va_external_dx11_example.md
#include "eui_neo.h"

bool initializeHostAndEui(HWND hwnd) {
    using VectorAura::Engine::VAGraphicsDevice;
    using VectorAura::Engine::VAGraphicsWindow;

    auto& device = VAGraphicsDevice::GetInstance();
    if (!device.Initialize()) {
        return false;
    }

    static VAGraphicsWindow graphicsWindow(&device, hwnd);
    if (!graphicsWindow.Initialize()) {
        return false;
    }

    eui::dx11::setExternalDx11Context(makeEuiExternalContext(device, graphicsWindow));
    return true;
}
```

如果宿主在运行期销毁并重建窗口，应该在旧窗口失效前后及时调用：

```cpp docs/va_external_dx11_example.md
eui::dx11::clearExternalDx11Context(hwnd);
```

如果要一次性清空所有已注册窗口，也可以继续使用：

```cpp docs/va_external_dx11_example.md
eui::dx11::clearExternalDx11Context();
```

然后在新窗口与新图形上下文准备完成后重新注册。

## 宿主渲染循环

当 `hostManagesBeginEndDraw = true` 时，推荐的宿主渲染顺序是：

1. 宿主开始 D2D draw batch
2. 宿主绘制自己的背景或其他内容
3. EUI 进入自己的渲染阶段
4. 宿主结束 D2D draw batch
5. 宿主执行 `Present`

最小示意：

```cpp docs/va_external_dx11_example.md
void renderFrame(VectorAura::Engine::VAGraphicsWindow& graphicsWindow) {
    ID2D1DeviceContext* d2d = graphicsWindow.GetD2DContext();
    if (d2d == nullptr) {
        return;
    }

    d2d->BeginDraw();

    // host scene draw here
    // eui render here

    d2d->EndDraw();
    graphicsWindow.Present();
}
```

这里的 `eui render here` 不是直接调用 `eui::dx11` API，而是让你的 EUI runtime 照常走自己的渲染路径。接管模式只是让 backend 复用宿主图形资源，不改变上层 DSL/runtime 的调用方式。

## resize 处理

当 `hostManagesResize = true` 时：

- 宿主窗口尺寸变化时，先调用 `VAGraphicsWindow::Resize(...)`
- 然后继续沿用同一个或重新注册后的 `ExternalDx11Context`

如果宿主的 `Resize(...)` 会重建底层 render target，并导致 `swapChain`、`RTV`、`D2D target` 等对象变化，建议在 resize 之后重新调用一次：

```cpp docs/va_external_dx11_example.md
eui::dx11::setExternalDx11Context(makeEuiExternalContext(device, graphicsWindow));
```

## 当前建议

更稳妥的接法是：

- 保持 EUI 只作为 UI 绘制层使用
- 保持宿主管理图形生命周期
- 保持外部上下文注册点集中在窗口初始化和 resize 之后
- 对每个宿主窗口单独注册对应的 `HWND -> ExternalDx11Context`
- 不在多个宿主窗口之间复用同一份窗口级上下文

如果你后面要把这个示例真正落到另一个项目里，下一步最值得做的是补一个针对你宿主主循环的专用 adapter，把注册、清理和 resize 后重注册封装成几条固定入口。
