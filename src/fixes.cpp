// TimePilot - ReXGlue Recompiled Project
// 项目专用补丁（干净版 2026-10-29）
//
// 说明：ReXGlue 运行时通过内部 guest 函数表派发，不查 exe 原生导出符号，
// 因此这里不需要（也不该有）针对 guest 地址的 dllexport 劫持桩。
// 本文件只保留两类确实需要的补丁：
//   1. 宿主编译器缺失的 CRT 数学函数
//   2. 游戏引用但宿主没有的 Xbox 360 USB 摄像头内核 API 空实现

#include <cmath>
#include <cstdint>

// MSVC/lld-link 没有 roundevenf，用 nearbyintf 近似替代
extern "C" float roundevenf(float x) {
    return std::nearbyintf(x);
}

// Xbox 360 USB 摄像头内核 API：TimePilot 未使用摄像头，全部返回空状态即可
extern "C" {
    __declspec(dllexport) int _XUsbcamCreate(...) { return 0; }
    __declspec(dllexport) int _XUsbcamDestroy(...) { return 0; }
    __declspec(dllexport) int _XUsbcamGetState(...) { return 0; }
    __declspec(dllexport) int _XUsbcamSetConfig(...) { return 0; }
    __declspec(dllexport) int _XUsbcamSetView(...) { return 0; }
    __declspec(dllexport) int _XUsbcamSetCaptureMode(...) { return 0; }
    __declspec(dllexport) int _XUsbcamReadFrame(...) { return 0; }
}
