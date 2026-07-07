#include "core/render/dx11/dx11_external_context.h"

#include <unordered_map>

namespace core::render {

namespace {

std::unordered_map<HWND, ExternalDx11Context>& externalContextRegistry() {
    static std::unordered_map<HWND, ExternalDx11Context> registry;
    return registry;
}

} // namespace

void setExternalDx11Context(const ExternalDx11Context& context) {
    if (context.hwnd == nullptr) {
        return;
    }
    externalContextRegistry()[context.hwnd] = context;
}

bool hasExternalDx11Context() {
    return !externalContextRegistry().empty();
}

bool hasExternalDx11Context(HWND hwnd) {
    return hwnd != nullptr && externalContextRegistry().contains(hwnd);
}

const ExternalDx11Context* externalDx11Context() {
    if (externalContextRegistry().size() != 1u) {
        return nullptr;
    }
    return &externalContextRegistry().begin()->second;
}

const ExternalDx11Context* externalDx11Context(HWND hwnd) {
    if (hwnd == nullptr) {
        return nullptr;
    }
    const auto it = externalContextRegistry().find(hwnd);
    return it != externalContextRegistry().end() ? &it->second : nullptr;
}

void clearExternalDx11Context() {
    externalContextRegistry().clear();
}

void clearExternalDx11Context(HWND hwnd) {
    if (hwnd == nullptr) {
        return;
    }
    externalContextRegistry().erase(hwnd);
}

} // namespace core::render
