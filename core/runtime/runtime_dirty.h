#pragma once

#include "core/runtime/runtime_geometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace core::dsl {

namespace runtime {

struct LogicalDirtyRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

} // namespace runtime

inline std::vector<Rect> resolveDirtyRects(const std::vector<runtime::LogicalDirtyRect>& dirtyRects,
                                           bool fullPaintRequested,
                                           int windowWidth,
                                           int windowHeight,
                                           float dpiScale) {
    if (fullPaintRequested) {
        return {};
    }

    std::vector<Rect> rects;
    Rect merged;
    bool hasMerged = false;
    for (const runtime::LogicalDirtyRect& dirty : dirtyRects) {
        Rect rect = toPixelRect(Rect{dirty.x, dirty.y, dirty.width, dirty.height}, dpiScale);
        const float left = std::clamp(std::floor(rect.x), 0.0f, static_cast<float>(windowWidth));
        const float top = std::clamp(std::floor(rect.y), 0.0f, static_cast<float>(windowHeight));
        const float right = std::clamp(std::ceil(rect.x + rect.width), 0.0f, static_cast<float>(windowWidth));
        const float bottom = std::clamp(std::ceil(rect.y + rect.height), 0.0f, static_cast<float>(windowHeight));
        if (right <= left || bottom <= top) {
            continue;
        }
        rect = {left, top, right - left, bottom - top};
        merged = hasMerged ? unionRect(merged, rect) : rect;
        hasMerged = true;
    }

    if (hasMerged) {
        rects.push_back(merged);
    }
    return rects;
}

} // namespace core::dsl
