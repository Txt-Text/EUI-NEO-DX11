#pragma once

#include "core/dsl.h"
#include "core/platform/platform.h"
#include "core/input/input_state.h"
#include "core/render/image.h"
#include "core/render/primitive.h"
#include "core/render/render_backend.h"
#include "core/render/text.h"
#include "core/window/window_backend.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace core::dsl {

class Runtime {
    struct RenderTransform {
        bool active = false;
        TransformMatrix matrix;
        float opacity = 1.0f;
    };

    struct ElementSnapshot {
        std::string id;
        ElementKind kind = ElementKind::Stack;
        int zIndex = 0;
        bool clip = false;
        std::size_t childCount = 0;

        bool operator==(const ElementSnapshot& other) const {
            return id == other.id &&
                   kind == other.kind &&
                   zIndex == other.zIndex &&
                   clip == other.clip &&
                   childCount == other.childCount;
        }

        bool operator!=(const ElementSnapshot& other) const {
            return !(*this == other);
        }
    };

public:
    bool initialize() {
        return true;
    }

    bool initialize(core::window::Handle window) {
        installInputCallbacks(window);
        return true;
    }

    template <typename ComposeFn>
    void compose(const std::string& pageId, float logicalWidth, float logicalHeight, ComposeFn&& composeFn) {
        const std::vector<ElementSnapshot> previousStructure = elementStructure_;
        const Screen screen{logicalWidth, logicalHeight};
        ui_.begin(pageId);
        ui_.setFocusedId(focusedId_);
        composeFn(ui_, screen);
        ui_.end();
        ui_.layout(screen);
        elementStructure_ = collectElementStructure();
        syncScrollStateBindings();

        if (elementStructure_ != previousStructure) {
            needsRender_ = true;
            fullRedraw_ = true;
        }

        if (logicalWidth_ != logicalWidth || logicalHeight_ != logicalHeight) {
            needsRender_ = true;
            fullRedraw_ = true;
        }
        logicalWidth_ = logicalWidth;
        logicalHeight_ = logicalHeight;
    }

    bool update(core::window::Handle window, float deltaSeconds, float pointerScale, float dpiScale, bool inputEnabled = true) {
        PointerEvent event = readPointerEvent(window, pointerScale);
        const auto inputEvents = consumeInputEvents(window);
        KeyboardEvent keyboardEvent = inputEvents.first;
        ScrollEvent scrollEvent = inputEvents.second;
        if (!inputEnabled) {
            event.x = -1000000.0;
            event.y = -1000000.0;
            event.deltaX = 0.0;
            event.deltaY = 0.0;
            event.down = false;
            event.pressedThisFrame = false;
            event.releasedThisFrame = true;
            event.rightDown = false;
            event.rightPressedThisFrame = false;
            event.rightReleasedThisFrame = false;
            keyboardEvent = {};
            scrollEvent = {};
        }
        animating_ = false;
        needsCompose_ = false;
        wantsHandCursor_ = false;
        markInstancesUnseen();
        markTimersUnseen();
        if (ImagePrimitive::consumeRemoteImageReady()) {
            fullRedraw_ = true;
            needsRender_ = true;
        }

        syncScrollStateBindings();
        if (scrollEvent.active()) {
            updateScroll(scrollEvent, hitTestScrollable(event, dpiScale));
        }

        if (event.pressedThisFrame) {
            setFocusedId(hitTestFocusable(event, dpiScale));
        }

        const std::string capturedId = capturedInteractionId();
        const std::string hoverTargetId = !capturedId.empty() ? capturedId : hitTestInteractive(event, dpiScale);
        updateElementTree(event, deltaSeconds, dpiScale, hoverTargetId);
        updateDependentVisualDirtyRegions(dpiScale);

        if (keyboardEvent.hasInput()) {
            updateTextInput(keyboardEvent);
        }
        releaseUnseenTimers();
        updateImeCursorRect(window, dpiScale);
        applyCursor(window);

        promoteBackdropBlurDirtyRegions(dpiScale);
        releaseUnseenInstances();

        const bool result = needsRender_;
        needsRender_ = false;
        return result;
    }

    bool isAnimating() const {
        return animating_;
    }

    bool needsCompose() const {
        return needsCompose_;
    }

    void markFullRedraw() {
        fullRedraw_ = true;
        needsRender_ = true;
    }

    void render(int windowWidth, int windowHeight, float dpiScale, const Color& clearColor) {
        core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
        if (renderBackend == nullptr) {
            return;
        }

        if (!renderBackend->ensureRenderCache(windowWidth, windowHeight)) {
            renderBackend->clear(clearColor);
            renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
            dirtyRects_.clear();
            fullRedraw_ = false;
            return;
        }
        if (renderBackend->renderCacheWasRecreated()) {
            fullRedraw_ = true;
        }

        const std::vector<Rect> dirtyRects = resolveDirtyRects(windowWidth, windowHeight, dpiScale);
        if (dirtyRects.empty() && !fullRedraw_) {
            renderBackend->blitRenderCache(windowWidth, windowHeight);
            return;
        }

        renderBackend->beginRenderCacheFrame(windowWidth, windowHeight);

        if (fullRedraw_) {
            renderBackend->setScissor(false, {}, windowHeight);
            renderBackend->clear(clearColor);
            renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale);
        } else {
            for (const Rect& dirty : dirtyRects) {
                renderBackend->setScissor(true, dirty, windowHeight);
                renderBackend->clear(clearColor);
                renderDirect(*renderBackend, windowWidth, windowHeight, dpiScale, &dirty);
            }
            renderBackend->setScissor(false, {}, windowHeight);
        }

        renderBackend->endRenderCacheFrame();
        renderBackend->blitRenderCache(windowWidth, windowHeight);
        dirtyRects_.clear();
        fullRedraw_ = false;
    }

    void render(int windowWidth, int windowHeight, float dpiScale) {
        core::render::RenderBackend* renderBackend = core::render::activeRenderBackend();
        if (renderBackend == nullptr) {
            return;
        }

        const RenderTransform identity;
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (const Element* root : roots) {
            prepareTextElement(*root, windowWidth, windowHeight, dpiScale, identity);
        }
        for (const Element* root : roots) {
            renderElement(*renderBackend, *root, windowWidth, windowHeight, dpiScale, identity);
        }
    }

    void shutdown(bool releaseCachedImageTextures = true) {
        releaseGraphicsResources(releaseCachedImageTextures);
        rects_.clear();
        polygons_.clear();
        texts_.clear();
        images_.clear();
        interactions_.clear();
        dirtyKeys_.clear();
        layouts_.clear();
        scrollStates_.clear();
        sliderStates_.clear();
        timers_.clear();
        dependentVisualStates_.clear();
        frameTargets_.clear();
        elementStructure_.clear();
    }

    void releaseGraphicsResources(bool releaseCachedImageTextures = true) {
        for (auto& item : rects_) {
            if (item.second.initialized) {
                item.second.primitive->destroy();
                item.second.initialized = false;
            }
        }
        for (auto& item : polygons_) {
            if (item.second.initialized) {
                item.second.primitive->destroy();
                item.second.initialized = false;
            }
        }
        for (auto& item : texts_) {
            if (item.second.initialized) {
                item.second.primitive->destroy();
                item.second.initialized = false;
            }
        }
        for (auto& item : images_) {
            if (item.second.initialized) {
                item.second.primitive->destroy();
                item.second.initialized = false;
            }
        }
        if (releaseCachedImageTextures) {
            ImagePrimitive::releaseCachedTextures();
        }
        destroyCursors();
        fullRedraw_ = true;
        needsRender_ = true;
    }

private:
    struct RectInstance {
        std::unique_ptr<RoundedRectPrimitive> primitive = std::make_unique<RoundedRectPrimitive>();
        InteractionState interaction;
        bool initialized = false;
        bool seen = false;
        SmoothedValue<float> hoverBlend;
        SmoothedValue<float> pressBlend;
        AnimatedValue<LayoutRect> frame;
        AnimatedValue<Color> color;
        Gradient gradient;
        AnimatedValue<float> radius;
        AnimatedValue<float> blur;
        AnimatedValue<float> opacity;
        AnimatedValue<Border> border;
        AnimatedValue<Shadow> shadow;
        AnimatedValue<Transform> transform;
    };

    struct PolygonInstance {
        std::unique_ptr<PolygonPrimitive> primitive = std::make_unique<PolygonPrimitive>();
        InteractionState interaction;
        bool initialized = false;
        bool seen = false;
        SmoothedValue<float> hoverBlend;
        SmoothedValue<float> pressBlend;
        AnimatedValue<LayoutRect> frame;
        AnimatedValue<Color> color;
        AnimatedValue<float> opacity;
        AnimatedValue<Transform> transform;
        std::vector<Vec2> points;
    };

    struct TextInstance {
        std::unique_ptr<TextPrimitive> primitive = std::make_unique<TextPrimitive>();
        bool initialized = false;
        bool seen = false;
        AnimatedValue<LayoutRect> frame;
        AnimatedValue<Color> color;
        AnimatedValue<float> opacity;
        AnimatedValue<Transform> transform;
        std::string text;
        std::string fontFamily;
        float fontSize = 16.0f;
        int fontWeight = 400;
        float maxWidth = 0.0f;
        bool wrap = false;
        HorizontalAlign horizontalAlign = HorizontalAlign::Left;
        VerticalAlign verticalAlign = VerticalAlign::Top;
        float lineHeight = 0.0f;
        std::string contentDirtyKey;
    };

    struct ImageInstance {
        std::unique_ptr<ImagePrimitive> primitive = std::make_unique<ImagePrimitive>();
        bool initialized = false;
        bool seen = false;
        AnimatedValue<LayoutRect> frame;
        AnimatedValue<Color> tint;
        AnimatedValue<float> radius;
        AnimatedValue<float> opacity;
        AnimatedValue<Transform> transform;
        std::string source;
        std::string svgSource;
        bool flipVertically = false;
        ImageFit fit = ImageFit::Cover;
        bool hasCoverViewport = false;
        Vec2 coverViewportSize;
        Vec2 coverViewportOffset;
    };

    struct InteractionInstance {
        InteractionState state;
        bool seen = false;
    };

    struct DirtyKeyInstance {
        std::string key;
        Rect rect;
        bool initialized = false;
        bool seen = false;
    };

    struct LayoutInstance {
        AnimatedValue<Transform> transform;
        AnimatedValue<float> opacity;
        bool seen = false;
    };

    struct ScrollStateInstance {
        float offset = 0.0f;
        float maxOffset = 0.0f;
        float step = 48.0f;
        float dragStartOffset = 0.0f;
        Rect dirtyRect;
        bool hasDirtyRect = false;
        bool initialized = false;
        bool seen = false;
    };

    struct SliderStateInstance {
        float value = 0.0f;
        float width = 0.0f;
        float knobSize = 0.0f;
        Rect dirtyRect;
        bool hasDirtyRect = false;
        bool initialized = false;
        bool dragging = false;
        bool seen = false;
    };

    struct TimerInstance {
        float seconds = 0.0f;
        float elapsed = 0.0f;
        bool seen = false;
        bool active = false;
    };

    struct FrameTargetInstance {
        LayoutRect frame;
        bool initialized = false;
        bool seen = false;
    };

    struct DependentVisualState {
        Rect rect;
        float opacity = 1.0f;
        float scale = 1.0f;
        bool seen = false;
    };

    struct LogicalDirtyRect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    template <typename Fn>
    void forEachElement(Fn&& fn) const {
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (const Element* root : roots) {
            forEachElement(*root, fn);
        }
    }

    template <typename Fn>
    static void forEachElement(const Element& element, Fn&& fn) {
        fn(element);
        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            forEachElement(*child, fn);
        }
    }

    std::vector<ElementSnapshot> collectElementStructure() const {
        std::vector<ElementSnapshot> result;
        forEachElement([&](const Element& element) {
            result.push_back({
                element.id,
                element.kind,
                element.zIndex,
                element.clip,
                element.children.size()
            });
        });
        return result;
    }

    static std::vector<const Element*> orderedElements(const std::vector<std::unique_ptr<Element>>& elements) {
        std::vector<const Element*> ordered;
        ordered.reserve(elements.size());
        for (const auto& element : elements) {
            ordered.push_back(element.get());
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](const Element* a, const Element* b) {
            return a->zIndex < b->zIndex;
        });
        return ordered;
    }

    static float toPixels(float value, float dpiScale) {
        return value * dpiScale;
    }

    static Rect toPixelRect(const LayoutRect& frame, float dpiScale) {
        return {
            toPixels(frame.x, dpiScale),
            toPixels(frame.y, dpiScale),
            toPixels(frame.width, dpiScale),
            toPixels(frame.height, dpiScale)
        };
    }

    static Rect toPixelRect(const Rect& rect, float dpiScale) {
        return {
            toPixels(rect.x, dpiScale),
            toPixels(rect.y, dpiScale),
            toPixels(rect.width, dpiScale),
            toPixels(rect.height, dpiScale)
        };
    }

    static Rect toLogicalRect(const Rect& rect, float dpiScale) {
        const float scale = dpiScale > 0.0f ? 1.0f / dpiScale : 1.0f;
        return {
            rect.x * scale,
            rect.y * scale,
            rect.width * scale,
            rect.height * scale
        };
    }

    static bool intersects(const Rect& a, const Rect& b) {
        return a.x < b.x + b.width &&
               a.x + a.width > b.x &&
               a.y < b.y + b.height &&
               a.y + a.height > b.y;
    }

    static Rect unionRect(const Rect& a, const Rect& b) {
        const float left = std::min(a.x, b.x);
        const float top = std::min(a.y, b.y);
        const float right = std::max(a.x + a.width, b.x + b.width);
        const float bottom = std::max(a.y + a.height, b.y + b.height);
        return {left, top, right - left, bottom - top};
    }

    static bool intersectRect(const Rect& a, const Rect& b, Rect& out) {
        const float left = std::max(a.x, b.x);
        const float top = std::max(a.y, b.y);
        const float right = std::min(a.x + a.width, b.x + b.width);
        const float bottom = std::min(a.y + a.height, b.y + b.height);
        if (right <= left || bottom <= top) {
            out = {};
            return false;
        }
        out = {left, top, right - left, bottom - top};
        return true;
    }

    static Rect inflateRect(Rect rect, float amount) {
        rect.x -= amount;
        rect.y -= amount;
        rect.width += amount * 2.0f;
        rect.height += amount * 2.0f;
        return rect;
    }

    static constexpr float dependentVisualPadding() {
        return 4.0f;
    }

    static bool isIdentityTransform(const Transform& transform) {
        return closeEnough(transform.translate, Vec2{}) &&
               closeEnough(transform.translateZ, 0.0f) &&
               closeEnough(transform.scale, Vec2{1.0f, 1.0f}) &&
               closeEnough(transform.rotate, 0.0f) &&
               closeEnough(transform.rotateX, 0.0f) &&
               closeEnough(transform.rotateY, 0.0f) &&
               closeEnough(transform.perspective, 0.0f);
    }

    static bool isIdentityMatrix(const TransformMatrix& matrix) {
        return closeEnough(matrix.m00, 1.0f) &&
               closeEnough(matrix.m01, 0.0f) &&
               closeEnough(matrix.tx, 0.0f) &&
               closeEnough(matrix.m10, 0.0f) &&
               closeEnough(matrix.m11, 1.0f) &&
               closeEnough(matrix.ty, 0.0f) &&
               closeEnough(matrix.px, 0.0f) &&
               closeEnough(matrix.py, 0.0f) &&
               closeEnough(matrix.pw, 1.0f);
    }

    static TransformMatrix multiplyMatrix(const TransformMatrix& outer, const TransformMatrix& inner) {
        return {
            outer.m00 * inner.m00 + outer.m01 * inner.m10 + outer.tx * inner.px,
            outer.m00 * inner.m01 + outer.m01 * inner.m11 + outer.tx * inner.py,
            outer.m00 * inner.tx + outer.m01 * inner.ty + outer.tx * inner.pw,
            outer.m10 * inner.m00 + outer.m11 * inner.m10 + outer.ty * inner.px,
            outer.m10 * inner.m01 + outer.m11 * inner.m11 + outer.ty * inner.py,
            outer.m10 * inner.tx + outer.m11 * inner.ty + outer.ty * inner.pw,
            outer.px * inner.m00 + outer.py * inner.m10 + outer.pw * inner.px,
            outer.px * inner.m01 + outer.py * inner.m11 + outer.pw * inner.py,
            outer.px * inner.tx + outer.py * inner.ty + outer.pw * inner.pw
        };
    }

    static bool inverseMatrix(const TransformMatrix& matrix, TransformMatrix& inverse) {
        const float c00 = matrix.m11 * matrix.pw - matrix.ty * matrix.py;
        const float c01 = matrix.ty * matrix.px - matrix.m10 * matrix.pw;
        const float c02 = matrix.m10 * matrix.py - matrix.m11 * matrix.px;
        const float c10 = matrix.tx * matrix.py - matrix.m01 * matrix.pw;
        const float c11 = matrix.m00 * matrix.pw - matrix.tx * matrix.px;
        const float c12 = matrix.m01 * matrix.px - matrix.m00 * matrix.py;
        const float c20 = matrix.m01 * matrix.ty - matrix.tx * matrix.m11;
        const float c21 = matrix.tx * matrix.m10 - matrix.m00 * matrix.ty;
        const float c22 = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
        const float determinant = matrix.m00 * c00 + matrix.m01 * c01 + matrix.tx * c02;
        if (std::fabs(determinant) <= 0.000001f) {
            return false;
        }
        const float invDet = 1.0f / determinant;
        inverse = {
            c00 * invDet,
            c10 * invDet,
            c20 * invDet,
            c01 * invDet,
            c11 * invDet,
            c21 * invDet,
            c02 * invDet,
            c12 * invDet,
            c22 * invDet
        };
        return true;
    }

    static TransformMatrix matrixForTransform(const Rect& frame, const Transform& transform) {
        const Vec2 origin = {
            frame.x + frame.width * transform.origin.x,
            frame.y + frame.height * transform.origin.y
        };
        const float cosX = std::cos(transform.rotateX);
        const float sinX = std::sin(transform.rotateX);
        const float cosY = std::cos(transform.rotateY);
        const float sinY = std::sin(transform.rotateY);
        const float cosZ = std::cos(transform.rotate);
        const float sinZ = std::sin(transform.rotate);
        const float scaleX = transform.scale.x;
        const float scaleY = transform.scale.y;

        const float xFromX = scaleX * cosY;
        const float xFromY = scaleY * sinX * sinY;
        const float yFromY = scaleY * cosX;
        const float zFromX = -scaleX * sinY;
        const float zFromY = scaleY * sinX * cosY;

        const float ax = xFromX * cosZ;
        const float bx = xFromY * cosZ - yFromY * sinZ;
        const float ay = xFromX * sinZ;
        const float by = xFromY * sinZ + yFromY * cosZ;
        const float az = zFromX;
        const float bz = zFromY;

        if (transform.perspective <= 0.0001f) {
            return {
                ax,
                bx,
                origin.x + transform.translate.x - ax * origin.x - bx * origin.y,
                ay,
                by,
                origin.y + transform.translate.y - ay * origin.x - by * origin.y,
                0.0f,
                0.0f,
                1.0f
            };
        }

        const float perspective = std::max(1.0f, transform.perspective);
        const float tz = transform.translateZ;
        const float nxDx = perspective * ax - origin.x * az;
        const float nxDy = perspective * bx - origin.x * bz;
        const float nyDx = perspective * ay - origin.y * az;
        const float nyDy = perspective * by - origin.y * bz;
        const float denominator = perspective - tz + az * origin.x + bz * origin.y;

        return {
            nxDx + transform.translate.x * -az,
            nxDy + transform.translate.x * -bz,
            perspective * origin.x - origin.x * tz - nxDx * origin.x - nxDy * origin.y + transform.translate.x * denominator,
            nyDx + transform.translate.y * -az,
            nyDy + transform.translate.y * -bz,
            perspective * origin.y - origin.y * tz - nyDx * origin.x - nyDy * origin.y + transform.translate.y * denominator,
            -az,
            -bz,
            denominator
        };
    }

    static TransformMatrix matrixForScaleAround(const Rect& frame, float scale) {
        Transform transform;
        transform.scale = {scale, scale};
        transform.origin = {0.5f, 0.5f};
        return matrixForTransform(frame, transform);
    }

    static RenderTransform appendRenderMatrix(RenderTransform transform, const TransformMatrix& matrix) {
        transform.matrix = multiplyMatrix(transform.matrix, matrix);
        transform.active = transform.active || !isIdentityMatrix(matrix);
        return transform;
    }

    static Vec2 transformPoint(Vec2 point, const LayoutRect& frame, const Transform& transform) {
        return core::transformPoint(matrixForTransform({frame.x, frame.y, frame.width, frame.height}, transform), point.x, point.y);
    }

    static float shadowVisualPadding(const Shadow& shadow) {
        const float blur = std::max(shadow.blur, 1.0f);
        const float offsetMagnitude = std::max(std::fabs(shadow.offset.x), std::fabs(shadow.offset.y));
        return blur * 1.18f * 1.08f + offsetMagnitude * 0.20f + 1.0f;
    }

    static Rect transformRect(const Rect& rect, const LayoutRect& frame, const Transform& transform) {
        if (isIdentityTransform(transform)) {
            return rect;
        }

        const Vec2 p0 = transformPoint({rect.x, rect.y}, frame, transform);
        const Vec2 p1 = transformPoint({rect.x + rect.width, rect.y}, frame, transform);
        const Vec2 p2 = transformPoint({rect.x + rect.width, rect.y + rect.height}, frame, transform);
        const Vec2 p3 = transformPoint({rect.x, rect.y + rect.height}, frame, transform);
        const float left = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float top = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float right = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float bottom = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return {left, top, right - left, bottom - top};
    }

    static Rect visualRect(const LayoutRect& frame, const Shadow& shadow, float blur, const Transform& transform = {}) {
        Rect rect{frame.x, frame.y, frame.width, frame.height};
        if (shadow.enabled && !shadow.inset) {
            Rect shadowRect{
                frame.x + shadow.offset.x - shadow.spread,
                frame.y + shadow.offset.y - shadow.spread,
                frame.width + shadow.spread * 2.0f,
                frame.height + shadow.spread * 2.0f
            };
            shadowRect = inflateRect(shadowRect, shadowVisualPadding(shadow));
            rect = unionRect(rect, shadowRect);
        }
        if (blur > 0.0f) {
            rect = inflateRect(rect, blur + 2.0f);
        }
        return transformRect(rect, frame, transform);
    }

    static Rect backdropCaptureRect(const LayoutRect& frame, float blur, const Transform& transform = {}) {
        return visualRect(frame, Shadow{}, blur, transform);
    }

    static Rect imageVisualRect(const LayoutRect& frame, const Transform& transform = {}) {
        return transformRect({frame.x, frame.y, frame.width, frame.height}, frame, transform);
    }

    static bool containsRect(const Rect& outer, const Rect& inner) {
        return inner.x >= outer.x &&
               inner.y >= outer.y &&
               inner.x + inner.width <= outer.x + outer.width &&
               inner.y + inner.height <= outer.y + outer.height;
    }

    static bool sameGradient(const Gradient& left, const Gradient& right) {
        return left.enabled == right.enabled &&
               left.direction == right.direction &&
               closeEnough(left.start, right.start) &&
               closeEnough(left.end, right.end);
    }

    void applyCursor(core::window::Handle window) {
        if (!arrowCursor_) {
            arrowCursor_ = core::window::createStandardCursor(core::window::CursorType::Arrow);
        }
        if (!handCursor_) {
            handCursor_ = core::window::createStandardCursor(core::window::CursorType::Hand);
        }

        core::window::CursorHandle target = wantsHandCursor_ && handCursor_ ? handCursor_ : arrowCursor_;
        if (target != currentCursor_) {
            core::window::setCursor(window, target);
            currentCursor_ = target;
        }
    }

    void destroyCursors() {
        if (arrowCursor_) {
            core::window::destroyCursor(arrowCursor_);
            arrowCursor_ = nullptr;
        }
        if (handCursor_) {
            core::window::destroyCursor(handCursor_);
            handCursor_ = nullptr;
        }
        currentCursor_ = nullptr;
    }

    void addDirtyRect(const Rect& rect) {
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            return;
        }
        dirtyRects_.push_back({rect.x, rect.y, rect.width, rect.height});
        needsRender_ = true;
    }

    void addDirtyUnion(const Rect& before, const Rect& after) {
        addDirtyRect(unionRect(before, after));
    }

    void promoteBackdropBlurDirtyRegions(float dpiScale) {
        if (fullRedraw_ || dirtyRects_.empty()) {
            return;
        }

        Rect mergedDirty{};
        bool hasMergedDirty = false;
        for (const LogicalDirtyRect& dirty : dirtyRects_) {
            const Rect dirtyRect{dirty.x, dirty.y, dirty.width, dirty.height};
            mergedDirty = hasMergedDirty ? unionRect(mergedDirty, dirtyRect) : dirtyRect;
            hasMergedDirty = true;
        }
        if (!hasMergedDirty) {
            return;
        }

        bool expandedAny = false;
        bool expandedThisPass = false;
        const RenderTransform identity;
        do {
            expandedThisPass = false;
            const std::vector<const Element*> roots = orderedElements(ui_.roots());
            for (const Element* root : roots) {
                expandBackdropBlurDirtyRegions(*root, dpiScale, identity, mergedDirty, expandedThisPass);
            }
            expandedAny = expandedAny || expandedThisPass;
        } while (expandedThisPass);

        if (expandedAny) {
            dirtyRects_.clear();
            dirtyRects_.push_back({mergedDirty.x, mergedDirty.y, mergedDirty.width, mergedDirty.height});
            needsRender_ = true;
        }
    }

    void expandBackdropBlurDirtyRegions(const Element& element,
                                        float dpiScale,
                                        const RenderTransform& inheritedTransform,
                                        Rect& mergedDirty,
                                        bool& expanded) {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        if (element.kind == ElementKind::Rect) {
            const auto instance = rects_.find(element.id);
            const LayoutRect frame = instance != rects_.end() ? instance->second.frame.value() : element.frame;
            const Transform transform = instance != rects_.end() ? instance->second.transform.value() : element.transform;
            const float blur = std::max(element.blur, instance != rects_.end() ? instance->second.blur.value() : element.blur);
            if (blur > 0.0f) {
                const Rect captureRect = applyRenderTransformToLogicalRect(
                    backdropCaptureRect(frame, blur, transform),
                    dpiScale,
                    renderTransform);
                if (intersects(captureRect, mergedDirty) && !containsRect(mergedDirty, captureRect)) {
                    mergedDirty = unionRect(mergedDirty, captureRect);
                    expanded = true;
                }
            }
        }

        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            expandBackdropBlurDirtyRegions(*child, dpiScale, renderTransform, mergedDirty, expanded);
        }
    }

    static Vec2 applyRenderTransform(Vec2 point, const RenderTransform& transform) {
        if (!transform.active) {
            return point;
        }
        return core::transformPoint(transform.matrix, point.x, point.y);
    }

    static Rect applyRenderTransform(const Rect& rect, const RenderTransform& transform) {
        if (!transform.active) {
            return rect;
        }

        const Vec2 p0 = applyRenderTransform(Vec2{rect.x, rect.y}, transform);
        const Vec2 p1 = applyRenderTransform(Vec2{rect.x + rect.width, rect.y}, transform);
        const Vec2 p2 = applyRenderTransform(Vec2{rect.x + rect.width, rect.y + rect.height}, transform);
        const Vec2 p3 = applyRenderTransform(Vec2{rect.x, rect.y + rect.height}, transform);
        const float left = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float top = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float right = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float bottom = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return {left, top, right - left, bottom - top};
    }

    static Rect applyTransformMatrix(const Rect& rect, const TransformMatrix& matrix) {
        const Vec2 p0 = core::transformPoint(matrix, rect.x, rect.y);
        const Vec2 p1 = core::transformPoint(matrix, rect.x + rect.width, rect.y);
        const Vec2 p2 = core::transformPoint(matrix, rect.x + rect.width, rect.y + rect.height);
        const Vec2 p3 = core::transformPoint(matrix, rect.x, rect.y + rect.height);
        const float left = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
        const float top = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
        const float right = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
        const float bottom = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
        return {left, top, right - left, bottom - top};
    }

    static Rect applyRenderTransformToLogicalRect(const Rect& rect, float dpiScale, const RenderTransform& transform) {
        if (!transform.active) {
            return rect;
        }
        return toLogicalRect(applyRenderTransform(toPixelRect(rect, dpiScale), transform), dpiScale);
    }

    static Border scaleBorder(Border border, float dpiScale) {
        border.width = toPixels(border.width, dpiScale);
        return border;
    }

    static Shadow scaleShadow(Shadow shadow, float dpiScale) {
        shadow.offset.x = toPixels(shadow.offset.x, dpiScale);
        shadow.offset.y = toPixels(shadow.offset.y, dpiScale);
        shadow.blur = toPixels(shadow.blur, dpiScale);
        shadow.spread = toPixels(shadow.spread, dpiScale);
        return shadow;
    }

    static Transform scaleTransform(Transform transform, float dpiScale) {
        transform.translate.x = toPixels(transform.translate.x, dpiScale);
        transform.translate.y = toPixels(transform.translate.y, dpiScale);
        transform.translateZ = toPixels(transform.translateZ, dpiScale);
        transform.perspective = toPixels(transform.perspective, dpiScale);
        return transform;
    }

    Transform pointerRuntimeTransform(const Element& element,
                                      const PointerEvent& event,
                                      float dpiScale,
                                      const std::string& hoverTargetId) const {
        Transform result = element.transform;
        if (element.pointerRuntimeSourceId.empty() || element.pointerRuntimeAmount <= 0.0f) {
            return result;
        }

        const Element* source = ui_.find(element.pointerRuntimeSourceId);
        const bool hover = source != nullptr && hoverTargetId == source->id && !source->disabled;
        if (!hover || source == nullptr) {
            return result;
        }

        const Rect bounds = toPixelRect(source->frame, dpiScale);
        const float localX = static_cast<float>(event.x) - bounds.x;
        const float localY = static_cast<float>(event.y) - bounds.y;
        const float nx = std::clamp(localX / std::max(1.0f, bounds.width), 0.0f, 1.0f) - 0.5f;
        const float ny = std::clamp(localY / std::max(1.0f, bounds.height), 0.0f, 1.0f) - 0.5f;
        result.rotateY += nx * element.pointerRuntimeMaxRotateY;
        result.rotateX += -ny * element.pointerRuntimeMaxRotateX;
        result.translate.x += nx * element.pointerRuntimeTranslate.x;
        result.translate.y += ny * element.pointerRuntimeTranslate.y;
        result.scale = {
            result.scale.x * element.pointerRuntimeHoverScale,
            result.scale.y * element.pointerRuntimeHoverScale
        };
        return result;
    }

    Transform scrollTransformForElement(const Element& element, Transform transform = {}) const {
        if (!element.scrollContentSourceId.empty()) {
            const auto state = scrollStates_.find(element.scrollContentSourceId);
            if (state != scrollStates_.end()) {
                transform.translate.y -= state->second.offset;
            }
        }
        if (!element.scrollThumbSourceId.empty()) {
            const auto state = scrollStates_.find(element.scrollThumbSourceId);
            if (state != scrollStates_.end() && state->second.maxOffset > 0.0f) {
                const float normalized = std::clamp(state->second.offset / state->second.maxOffset, 0.0f, 1.0f);
                transform.translate.y += element.scrollThumbTravel * normalized;
            }
        }
        return transform;
    }

    Transform sliderTransformForElement(const Element& element, Transform transform = {}) const {
        if (!element.sliderKnobSourceId.empty()) {
            const auto state = sliderStates_.find(element.sliderKnobSourceId);
            if (state != sliderStates_.end()) {
                const float travel = std::max(0.0f, state->second.width - state->second.knobSize);
                const float centered = std::clamp(state->second.width * state->second.value - state->second.knobSize * 0.5f,
                                                  0.0f,
                                                  travel);
                transform.translate.x += centered;
            }
        }
        return transform;
    }

    Transform runtimeTransformForElement(const Element& element, const Transform& base) const {
        return sliderTransformForElement(element, scrollTransformForElement(element, base));
    }

    static bool usesRuntimeTransform(const Element& element) {
        return !element.scrollContentSourceId.empty() ||
               !element.scrollThumbSourceId.empty() ||
               !element.sliderKnobSourceId.empty();
    }

    static TransformMatrix combinedPrimitiveMatrix(const RenderTransform& renderTransform,
                                                   const Rect& frame,
                                                   const Transform& localTransform) {
        return multiplyMatrix(renderTransform.matrix, matrixForTransform(frame, localTransform));
    }

    static bool isRectAnimating(const Element& element, const RectInstance& instance) {
        const bool interactive = element.interactive && !element.disabled;
        const bool stateColorsVisible = element.hasStateColors &&
            (!closeEnough(element.color, element.hoverColor) || !closeEnough(element.color, element.pressedColor));
        return instance.hoverBlend.isMovingTo(interactive && stateColorsVisible && instance.interaction.hover ? 1.0f : 0.0f) ||
               instance.pressBlend.isMovingTo(interactive && stateColorsVisible && instance.interaction.pressed ? 1.0f : 0.0f) ||
               instance.frame.isActive() ||
               instance.color.isActive() ||
               instance.radius.isActive() ||
               instance.blur.isActive() ||
               instance.opacity.isActive() ||
               instance.border.isActive() ||
               instance.shadow.isActive() ||
               instance.transform.isActive();
    }

    static bool isPolygonAnimating(const Element& element, const PolygonInstance& instance) {
        const bool interactive = element.interactive && !element.disabled;
        const bool stateColorsVisible = element.hasStateColors &&
            (!closeEnough(element.color, element.hoverColor) || !closeEnough(element.color, element.pressedColor));
        return instance.hoverBlend.isMovingTo(interactive && stateColorsVisible && instance.interaction.hover ? 1.0f : 0.0f) ||
               instance.pressBlend.isMovingTo(interactive && stateColorsVisible && instance.interaction.pressed ? 1.0f : 0.0f) ||
               instance.frame.isActive() ||
               instance.color.isActive() ||
               instance.opacity.isActive() ||
               instance.transform.isActive();
    }

    static bool isTextAnimating(const TextInstance& instance) {
        return instance.frame.isActive() ||
               instance.color.isActive() ||
               instance.opacity.isActive() ||
               instance.transform.isActive();
    }

    static bool isImageAnimating(const ImageInstance& instance) {
        return instance.frame.isActive() ||
               instance.tint.isActive() ||
               instance.radius.isActive() ||
               instance.opacity.isActive() ||
               instance.transform.isActive() ||
               instance.primitive->isAnimating() ||
               instance.primitive->hasPendingLoad();
    }

    static bool isLayoutAnimating(const LayoutInstance& instance) {
        return instance.transform.isActive() ||
               instance.opacity.isActive();
    }

    static bool shouldAnimate(const Element& element, AnimProperty property) {
        return element.transition.enabled && hasAnimProperty(element.transition.properties, property);
    }

    static bool shouldAnimateFrame(const Element& element) {
        return element.transition.enabled &&
               hasAnimProperty(element.transition.properties, AnimProperty::Frame) &&
               element.explicitFrameAnimation;
    }

    static bool samePoints(const std::vector<Vec2>& left, const std::vector<Vec2>& right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (!closeEnough(left[index], right[index])) {
                return false;
            }
        }
        return true;
    }

    bool updateFrameTarget(const Element& element) {
        FrameTargetInstance& instance = frameTargets_.try_emplace(element.id).first->second;
        instance.seen = true;
        if (!instance.initialized) {
            instance.frame = element.frame;
            instance.initialized = true;
            return false;
        }

        const bool changed = !closeEnough(instance.frame, element.frame);
        instance.frame = element.frame;
        return changed;
    }

    Rect visualDirtyRectForElement(const Element& element,
                                   float dpiScale,
                                   const RenderTransform& inheritedTransform) const {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        Rect local{element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        if (element.kind == ElementKind::Rect) {
            const auto instance = rects_.find(element.id);
            if (instance != rects_.end()) {
                local = visualRect(instance->second.frame.value(),
                                   instance->second.shadow.value(),
                                   instance->second.blur.value(),
                                   instance->second.transform.value());
            } else {
                local = visualRect(element.frame, element.shadow, element.blur, element.transform);
            }
        } else if (element.kind == ElementKind::Polygon) {
            const auto instance = polygons_.find(element.id);
            const LayoutRect frame = instance != polygons_.end() ? instance->second.frame.value() : element.frame;
            const Transform transform = instance != polygons_.end() ? instance->second.transform.value() : element.transform;
            local = transformRect({frame.x, frame.y, frame.width, frame.height}, frame, transform);
        } else if (element.kind == ElementKind::Text) {
            const auto instance = texts_.find(element.id);
            const LayoutRect frame = instance != texts_.end() ? instance->second.frame.value() : element.frame;
            const Transform transform = instance != texts_.end() ? instance->second.transform.value() : element.transform;
            local = transformRect({frame.x, frame.y, frame.width, frame.height}, frame, transform);
        } else if (element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
            const auto instance = images_.find(element.id);
            const LayoutRect frame = instance != images_.end() ? instance->second.frame.value() : element.frame;
            const Transform transform = instance != images_.end() ? instance->second.transform.value() : element.transform;
            local = imageVisualRect(frame, transform);
        }
        return applyRenderTransformToLogicalRect(local, dpiScale, renderTransform);
    }

    void updateExplicitDirtyKey(const Element& element,
                                float dpiScale,
                                const RenderTransform& inheritedTransform) {
        if (element.dirtyKey.empty()) {
            return;
        }

        DirtyKeyInstance& instance = dirtyKeyInstance(element.id);
        const Rect current = visualDirtyRectForElement(element, dpiScale, inheritedTransform);
        if (!instance.initialized) {
            instance.key = element.dirtyKey;
            instance.rect = current;
            instance.initialized = true;
            return;
        }

        if (instance.key != element.dirtyKey) {
            addDirtyUnion(instance.rect, current);
            instance.key = element.dirtyKey;
        }
        instance.rect = current;
    }

    void updateElementTree(const PointerEvent& event,
                           float deltaSeconds,
                           float dpiScale,
                           const std::string& hoverTargetId) {
        const RenderTransform identity;
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (const Element* root : roots) {
            updateElementTree(*root, event, deltaSeconds, dpiScale, hoverTargetId, identity, false);
        }
    }

    void updateElementTree(const Element& element,
                           const PointerEvent& event,
                           float deltaSeconds,
                           float dpiScale,
                           const std::string& hoverTargetId,
                           const RenderTransform& inheritedTransform,
                           bool ancestorFrameChanged) {
        const bool frameTargetChanged = updateFrameTarget(element);
        updateExplicitDirtyKey(element, dpiScale, inheritedTransform);
        updateInteraction(element, event, dpiScale, hoverTargetId, inheritedTransform);
        updateTimer(element, deltaSeconds);
        updateFrameCallback(element, deltaSeconds);

        if (element.kind == ElementKind::Row ||
            element.kind == ElementKind::Column ||
            element.kind == ElementKind::Stack) {
            updateLayoutElement(element, deltaSeconds, dpiScale, inheritedTransform, event, hoverTargetId);
        } else if (element.kind == ElementKind::Rect) {
            updateRect(element, deltaSeconds, dpiScale, inheritedTransform, ancestorFrameChanged);
        } else if (element.kind == ElementKind::Polygon) {
            updatePolygon(element, deltaSeconds, dpiScale, inheritedTransform, ancestorFrameChanged);
        } else if (element.kind == ElementKind::Text) {
            updateText(element, deltaSeconds, dpiScale, inheritedTransform, ancestorFrameChanged);
        } else if (element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
            updateImage(element, deltaSeconds, dpiScale, inheritedTransform, ancestorFrameChanged);
        }

        const bool childAncestorFrameChanged = ancestorFrameChanged || frameTargetChanged;
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            updateElementTree(*child, event, deltaSeconds, dpiScale, hoverTargetId, renderTransform, childAncestorFrameChanged);
        }
    }

    RectInstance& rectInstance(const std::string& id) {
        RectInstance& instance = rects_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    PolygonInstance& polygonInstance(const std::string& id) {
        PolygonInstance& instance = polygons_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    TextInstance& textInstance(const std::string& id) {
        TextInstance& instance = texts_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    ImageInstance& imageInstance(const std::string& id) {
        ImageInstance& instance = images_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    InteractionInstance& interactionInstance(const std::string& id) {
        InteractionInstance& instance = interactions_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    DirtyKeyInstance& dirtyKeyInstance(const std::string& id) {
        DirtyKeyInstance& instance = dirtyKeys_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    LayoutInstance& layoutInstance(const std::string& id) {
        LayoutInstance& instance = layouts_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    ScrollStateInstance& scrollStateInstance(const std::string& id) {
        ScrollStateInstance& instance = scrollStates_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    SliderStateInstance& sliderStateInstance(const std::string& id) {
        SliderStateInstance& instance = sliderStates_.try_emplace(id).first->second;
        instance.seen = true;
        return instance;
    }

    TimerInstance& timerInstance(const std::string& id) {
        return timers_.try_emplace(id).first->second;
    }

    template <typename Map>
    void markEntriesUnseen(Map& entries) {
        for (auto& item : entries) {
            item.second.seen = false;
        }
    }

    template <typename Map, typename OnRemove>
    void releaseUnseenEntries(Map& entries, OnRemove&& onRemove) {
        for (auto item = entries.begin(); item != entries.end(); ) {
            if (!item->second.seen) {
                onRemove(item->second);
                item = entries.erase(item);
            } else {
                ++item;
            }
        }
    }

    void markInstancesUnseen() {
        markEntriesUnseen(rects_);
        markEntriesUnseen(polygons_);
        markEntriesUnseen(texts_);
        markEntriesUnseen(images_);
        markEntriesUnseen(interactions_);
        markEntriesUnseen(dirtyKeys_);
        markEntriesUnseen(layouts_);
        markEntriesUnseen(scrollStates_);
        markEntriesUnseen(sliderStates_);
        markEntriesUnseen(frameTargets_);
    }

    void releaseUnseenInstances() {
        auto releasePrimitive = [](auto& instance) {
            if (instance.initialized) {
                instance.primitive->destroy();
                instance.initialized = false;
            }
        };
        auto noop = [](auto&) {};

        releaseUnseenEntries(rects_, releasePrimitive);
        releaseUnseenEntries(polygons_, releasePrimitive);
        releaseUnseenEntries(texts_, releasePrimitive);
        releaseUnseenEntries(images_, releasePrimitive);
        releaseUnseenEntries(interactions_, noop);
        releaseUnseenEntries(dirtyKeys_, noop);
        releaseUnseenEntries(layouts_, noop);
        releaseUnseenEntries(scrollStates_, noop);
        releaseUnseenEntries(sliderStates_, noop);
        releaseUnseenEntries(frameTargets_, noop);
    }

    void markTimersUnseen() {
        for (auto& item : timers_) {
            item.second.seen = false;
        }
    }

    void releaseUnseenTimers() {
        for (auto item = timers_.begin(); item != timers_.end(); ) {
            if (!item->second.seen) {
                item = timers_.erase(item);
            } else {
                ++item;
            }
        }
    }

    std::string capturedInteractionId() const {
        for (const auto& item : interactions_) {
            if (item.second.state.active && ui_.find(item.first)) {
                return item.first;
            }
        }
        return {};
    }

    std::string hitTestInteractive(const PointerEvent& event, float dpiScale) const {
        return hitTest(event, dpiScale, [](const Element& element) {
            return element.interactive && !element.disabled;
        });
    }

    std::string hitTestFocusable(const PointerEvent& event, float dpiScale) const {
        std::string targetId;
        const RenderTransform identity;
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            if (hitTestFocusableElement(**it, event, dpiScale, identity, false, {}, targetId)) {
                break;
            }
        }
        return targetId;
    }

    std::string hitTestScrollable(const PointerEvent& event, float dpiScale) const {
        return hitTest(event, dpiScale, [](const Element& element) {
            return (!element.scrollStateId.empty() || static_cast<bool>(element.onScroll)) && !element.disabled;
        });
    }

    template <typename Predicate>
    std::string hitTest(const PointerEvent& event, float dpiScale, Predicate&& predicate) const {
        std::string targetId;
        const RenderTransform identity;
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            if (hitTestElement(**it, event, dpiScale, identity, predicate, false, {}, targetId)) {
                break;
            }
        }
        return targetId;
    }

    template <typename Predicate>
    bool hitTestElement(const Element& element,
                        const PointerEvent& event,
                        float dpiScale,
                        const RenderTransform& inheritedTransform,
                        Predicate& predicate,
                        bool hasClip,
                        const Rect& clipRect,
                        std::string& targetId) const {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        Rect effectiveClip = clipRect;
        bool effectiveHasClip = hasClip;
        const Rect bounds = toPixelRect(element.frame, dpiScale);
        if (element.clip) {
            const Rect clipBounds = applyRenderTransform(bounds, renderTransform);
            if (effectiveHasClip) {
                if (!intersectRect(effectiveClip, clipBounds, effectiveClip)) {
                    return false;
                }
            } else {
                effectiveClip = clipBounds;
                effectiveHasClip = true;
            }
        }

        if (effectiveHasClip && !effectiveClip.contains(event.x, event.y)) {
            return false;
        }

        const std::vector<const Element*> children = orderedElements(element.children);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (hitTestElement(**it, event, dpiScale, renderTransform, predicate, effectiveHasClip, effectiveClip, targetId)) {
                return true;
            }
        }

        if (predicate(element) && hitContains(element, event, dpiScale, bounds, renderTransform)) {
            targetId = element.id;
            return true;
        }
        return false;
    }

    bool hitTestFocusableElement(const Element& element,
                                 const PointerEvent& event,
                                 float dpiScale,
                                 const RenderTransform& inheritedTransform,
                                 bool hasClip,
                                 const Rect& clipRect,
                                 std::string& targetId) const {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        Rect effectiveClip = clipRect;
        bool effectiveHasClip = hasClip;
        const Rect bounds = toPixelRect(element.frame, dpiScale);
        if (element.clip) {
            const Rect clipBounds = applyRenderTransform(bounds, renderTransform);
            if (effectiveHasClip) {
                if (!intersectRect(effectiveClip, clipBounds, effectiveClip)) {
                    return false;
                }
            } else {
                effectiveClip = clipBounds;
                effectiveHasClip = true;
            }
        }

        if (effectiveHasClip && !effectiveClip.contains(event.x, event.y)) {
            return false;
        }

        const std::vector<const Element*> children = orderedElements(element.children);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            if (hitTestFocusableElement(**it, event, dpiScale, renderTransform, effectiveHasClip, effectiveClip, targetId)) {
                return true;
            }
        }

        if (!hitContains(element, event, dpiScale, bounds, renderTransform)) {
            return false;
        }
        if (element.focusable && !element.disabled) {
            targetId = element.id;
            return true;
        }
        if (element.interactive && !element.disabled) {
            targetId.clear();
            return true;
        }
        return false;
    }

    void setFocusedId(const std::string& id) {
        if (focusedId_ == id) {
            return;
        }

        const std::string oldId = focusedId_;
        focusedId_ = id;
        imeCursorRectValid_ = false;
        ui_.setFocusedId(focusedId_);

        if (const Element* oldElement = ui_.find(oldId)) {
            if (oldElement->onFocusChanged) {
                oldElement->onFocusChanged(false);
            }
        }
        if (const Element* newElement = ui_.find(focusedId_)) {
            if (newElement->onFocusChanged) {
                newElement->onFocusChanged(true);
            }
        }
        needsCompose_ = true;
        needsRender_ = true;
    }

    void updateScroll(const ScrollEvent& event, const std::string& targetId) {
        if (targetId.empty()) {
            return;
        }

        if (const Element* element = ui_.find(targetId)) {
            if (!element->scrollStateId.empty() && !element->disabled) {
                applyRuntimeScroll(*element, -static_cast<float>(event.y) * scrollStepFor(*element));
                return;
            }
            if (element->onScroll && !element->disabled) {
                element->onScroll(event);
                needsCompose_ = true;
                needsRender_ = true;
            }
        }
    }

    void updateTextInput(const KeyboardEvent& event) {
        if (focusedId_.empty()) {
            return;
        }

        if (const Element* element = ui_.find(focusedId_)) {
            if (element->onTextInput && !element->disabled) {
                element->onTextInput(event);
                needsCompose_ = true;
                needsRender_ = true;
            }
        }
    }

    void updateImeCursorRect(core::window::Handle window, float dpiScale) {
        if (window == nullptr) {
            imeCursorRectValid_ = false;
            return;
        }
        if (focusedId_.empty()) {
            imeCursorRectValid_ = false;
            return;
        }

        const Element* element = ui_.find(focusedId_);
        if (element == nullptr || !element->hasImeRect) {
            imeCursorRectValid_ = false;
            return;
        }

        const Rect logicalRect{
            element->frame.x + element->imeRect.x,
            element->frame.y + element->imeRect.y,
            element->imeRect.width,
            element->imeRect.height
        };
        const Rect pixelRect = toPixelRect(logicalRect, dpiScale);
        if (imeCursorRectValid_ &&
            imeCursorWindow_ == window &&
            closeEnough(imeCursorRect_, pixelRect)) {
            return;
        }
        imeCursorWindow_ = window;
        imeCursorRect_ = pixelRect;
        imeCursorRectValid_ = true;
        core::platform::setImeCursorRect(
            window,
            pixelRect.x,
            pixelRect.y,
            pixelRect.width,
            pixelRect.height);
    }

    void updateInteraction(const Element& element,
                           const PointerEvent& event,
                           float dpiScale,
                           const std::string& hoverTargetId,
                           const RenderTransform& inheritedTransform) {
        if (!element.interactive && interactions_.find(element.id) == interactions_.end()) {
            return;
        }

        InteractionInstance& instance = interactionInstance(element.id);
        const Rect bounds = toPixelRect(element.frame, dpiScale);
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        const bool enabled = element.interactive && !element.disabled;
        const bool topmostHover = enabled && element.id == hoverTargetId;
        const bool wasHover = instance.state.hover;
        const Rect interactionBounds = applyTransformMatrix(
            bounds,
            hitMatrixForElement(element, dpiScale, bounds, renderTransform));
        instance.state.update(interactionBounds, event, topmostHover, enabled);

        if (enabled && wasHover != instance.state.hover && element.onHoverChanged) {
            element.onHoverChanged(instance.state.hover);
            needsCompose_ = true;
            needsRender_ = true;
        }

        if (enabled && instance.state.hover && element.cursor == CursorShape::Hand) {
            wantsHandCursor_ = true;
        }

        if (enabled && topmostHover && element.onMove &&
            (event.deltaX != 0.0 || event.deltaY != 0.0 || wasHover != instance.state.hover)) {
            PointerEvent logicalEvent = event;
            logicalEvent.x /= dpiScale;
            logicalEvent.y /= dpiScale;
            logicalEvent.deltaX /= dpiScale;
            logicalEvent.deltaY /= dpiScale;
            const Rect logicalBounds{
                bounds.x / dpiScale,
                bounds.y / dpiScale,
                bounds.width / dpiScale,
                bounds.height / dpiScale
            };
            if (element.onMove(logicalEvent, logicalBounds)) {
                needsCompose_ = true;
                needsRender_ = true;
            }
        }

        if (enabled && topmostHover && event.rightPressedThisFrame && element.onContextMenu) {
            PointerEvent logicalEvent = event;
            logicalEvent.x /= dpiScale;
            logicalEvent.y /= dpiScale;
            logicalEvent.deltaX /= dpiScale;
            logicalEvent.deltaY /= dpiScale;
            const Rect logicalBounds{
                bounds.x / dpiScale,
                bounds.y / dpiScale,
                bounds.width / dpiScale,
                bounds.height / dpiScale
            };
            element.onContextMenu(logicalEvent, logicalBounds);
            needsCompose_ = true;
            needsRender_ = true;
        }

        if (enabled && instance.state.pressStarted && !element.scrollDragSourceId.empty()) {
            beginRuntimeScrollDrag(element);
            needsRender_ = true;
        }

        if (enabled && instance.state.pressStarted && !element.sliderInputSourceId.empty()) {
            updateRuntimeSlider(element, event.x, dpiScale, true);
            needsRender_ = true;
            return;
        }

        if (enabled && instance.state.pressStarted && element.onPress) {
            element.onPress(event, bounds);
            needsCompose_ = true;
            needsRender_ = true;
        }

        if (enabled && instance.state.clicked && element.onClick) {
            element.onClick();
            needsCompose_ = true;
        }

        if (enabled && instance.state.released && element.onRelease) {
            element.onRelease(event, bounds);
            needsCompose_ = true;
            needsRender_ = true;
        }

        if (enabled && instance.state.released && !element.sliderInputSourceId.empty()) {
            if (auto state = sliderStates_.find(element.sliderInputSourceId); state != sliderStates_.end()) {
                state->second.dragging = false;
            }
            needsCompose_ = true;
            needsRender_ = true;
            return;
        }

        if (enabled && instance.state.pressed && !element.scrollDragSourceId.empty() &&
            (event.deltaX != 0.0 || event.deltaY != 0.0 || instance.state.drag)) {
            updateRuntimeScrollDrag(element, instance.state.dragDeltaY, dpiScale);
            return;
        }

        if (enabled && instance.state.pressed && !element.sliderInputSourceId.empty() &&
            (event.deltaX != 0.0 || event.deltaY != 0.0 || instance.state.drag)) {
            updateRuntimeSlider(element, event.x, dpiScale, true);
            return;
        }

        if (enabled && instance.state.pressed && element.onDrag &&
            (event.deltaX != 0.0 || event.deltaY != 0.0 || instance.state.drag)) {
            element.onDrag({
                event.x,
                event.y,
                event.deltaX,
                event.deltaY,
                instance.state.dragDeltaX,
                instance.state.dragDeltaY
            });
            needsCompose_ = true;
            needsRender_ = true;
        }
    }

    static bool polygonContains(const Element& element, double pointX, double pointY, float dpiScale, const Rect& bounds) {
        if (element.polygonPoints.size() < 3 || !bounds.contains(pointX, pointY)) {
            return false;
        }

        bool inside = false;
        const double localX = pointX - bounds.x;
        const double localY = pointY - bounds.y;
        std::size_t previous = element.polygonPoints.size() - 1;
        for (std::size_t current = 0; current < element.polygonPoints.size(); ++current) {
            const Vec2& a = element.polygonPoints[current];
            const Vec2& b = element.polygonPoints[previous];
            const double ax = static_cast<double>(a.x) * dpiScale;
            const double ay = static_cast<double>(a.y) * dpiScale;
            const double bx = static_cast<double>(b.x) * dpiScale;
            const double by = static_cast<double>(b.y) * dpiScale;
            const double denominator = by - ay;
            const bool crosses = ((ay > localY) != (by > localY)) &&
                (localX < (bx - ax) * (localY - ay) / denominator + ax);
            if (crosses) {
                inside = !inside;
            }
            previous = current;
        }
        return inside;
    }

    Transform currentElementTransform(const Element& element) const {
        if (element.kind == ElementKind::Rect) {
            const auto instance = rects_.find(element.id);
            if (instance != rects_.end()) {
                return instance->second.transform.value();
            }
        } else if (element.kind == ElementKind::Polygon) {
            const auto instance = polygons_.find(element.id);
            if (instance != polygons_.end()) {
                return instance->second.transform.value();
            }
        } else if (element.kind == ElementKind::Text) {
            const auto instance = texts_.find(element.id);
            if (instance != texts_.end()) {
                return instance->second.transform.value();
            }
        } else if (element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
            const auto instance = images_.find(element.id);
            if (instance != images_.end()) {
                return instance->second.transform.value();
            }
        } else if (element.kind == ElementKind::Row ||
                   element.kind == ElementKind::Column ||
                   element.kind == ElementKind::Stack) {
            const auto instance = layouts_.find(element.id);
            if (instance != layouts_.end()) {
                return instance->second.transform.value();
            }
        }
        return element.transform;
    }

    TransformMatrix hitMatrixForElement(const Element& element, float dpiScale, const Rect& bounds, const RenderTransform& renderTransform) const {
        if (element.kind == ElementKind::Rect ||
            element.kind == ElementKind::Polygon ||
            element.kind == ElementKind::Text ||
            element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
            return combinedPrimitiveMatrix(renderTransform, bounds, scaleTransform(currentElementTransform(element), dpiScale));
        }
        return renderTransform.matrix;
    }

    bool hitContains(const Element& element,
                     const PointerEvent& event,
                     float dpiScale,
                     const Rect& bounds,
                     const RenderTransform& renderTransform) const {
        if (element.hitTestMode == HitTestMode::None) {
            return false;
        }
        if (element.hitTestMode == HitTestMode::Transformed || renderTransform.active) {
            TransformMatrix inverse;
            if (!inverseMatrix(hitMatrixForElement(element, dpiScale, bounds, renderTransform), inverse)) {
                return false;
            }
            PointerEvent localEvent = event;
            const Vec2 local = core::transformPoint(inverse, static_cast<float>(event.x), static_cast<float>(event.y));
            localEvent.x = local.x;
            localEvent.y = local.y;
            if (element.kind == ElementKind::Polygon) {
                return polygonContains(element, localEvent.x, localEvent.y, dpiScale, bounds);
            }
            return bounds.contains(localEvent.x, localEvent.y);
        }
        if (element.kind == ElementKind::Polygon) {
            return polygonContains(element, event.x, event.y, dpiScale, bounds);
        }
        return bounds.contains(event.x, event.y);
    }

    void updateTimer(const Element& element, float deltaSeconds) {
        if (!element.onTimer || element.timerSeconds <= 0.0f) {
            return;
        }

        TimerInstance& instance = timerInstance(element.id);
        instance.seen = true;
        if (!instance.active || !closeEnough(instance.seconds, element.timerSeconds)) {
            instance.seconds = element.timerSeconds;
            instance.elapsed = 0.0f;
            instance.active = true;
        }

        if (!instance.active) {
            return;
        }

        instance.elapsed += std::max(0.0f, deltaSeconds);
        if (instance.elapsed >= instance.seconds) {
            instance.active = false;
            element.onTimer();
            needsCompose_ = true;
            needsRender_ = true;
        } else {
            animating_ = true;
        }
    }

    void updateFrameCallback(const Element& element, float deltaSeconds) {
        if (!element.onFrame) {
            return;
        }
        element.onFrame(std::max(0.0f, deltaSeconds));
        needsCompose_ = true;
        needsRender_ = true;
        animating_ = true;
    }

    void updateLayoutElement(const Element& element,
                             float deltaSeconds,
                             float dpiScale,
                             const RenderTransform& inheritedTransform,
                             const PointerEvent& event,
                             const std::string& hoverTargetId) {
        LayoutInstance& instance = layoutInstance(element.id);
        const Rect beforeRect = applyRenderTransformToLogicalRect(inflateRect(
            transformRect({element.frame.x, element.frame.y, element.frame.width, element.frame.height},
                          element.frame,
                          instance.transform.value()),
            64.0f), dpiScale, inheritedTransform);

        bool changed = false;
        Transform targetTransform = pointerRuntimeTransform(element, event, dpiScale, hoverTargetId);
        targetTransform = runtimeTransformForElement(element, targetTransform);
        changed = instance.transform.setTarget(targetTransform, element.transition, shouldAnimate(element, AnimProperty::Transform)) || changed;
        changed = instance.opacity.setTarget(element.opacity, element.transition, shouldAnimate(element, AnimProperty::Opacity)) || changed;

        changed = instance.transform.tick(deltaSeconds) || changed;
        changed = instance.opacity.tick(deltaSeconds) || changed;

        if (changed) {
            const Rect afterRect = applyRenderTransformToLogicalRect(inflateRect(
                transformRect({element.frame.x, element.frame.y, element.frame.width, element.frame.height},
                              element.frame,
                              instance.transform.value()),
                64.0f), dpiScale, inheritedTransform);
            addDirtyUnion(beforeRect, afterRect);
        }
        animating_ = animating_ || isLayoutAnimating(instance);
    }

    void updateRect(const Element& element,
                    float deltaSeconds,
                    float dpiScale,
                    const RenderTransform& inheritedTransform,
                    bool snapFrame) {
        RectInstance& instance = rectInstance(element.id);
        instance.interaction = interactionInstance(element.id).state;
        const Rect beforeRect = applyRenderTransformToLogicalRect(
            visualRect(instance.frame.value(), instance.shadow.value(), instance.blur.value(), instance.transform.value()),
            dpiScale,
            inheritedTransform);

        const bool interactive = element.interactive && !element.disabled;
        const bool stateColorsVisible = element.hasStateColors &&
            (!closeEnough(element.color, element.hoverColor) || !closeEnough(element.color, element.pressedColor));
        const float hoverSpeed = element.smoothStateColors ? 12.0f : 0.0f;
        const float pressSpeed = element.smoothStateColors ? 20.0f : 0.0f;
        const bool hoverChanged = instance.hoverBlend.update(interactive && stateColorsVisible && instance.interaction.hover ? 1.0f : 0.0f,
                                                             hoverSpeed,
                                                             deltaSeconds);
        const bool pressChanged = instance.pressBlend.update(interactive && stateColorsVisible && instance.interaction.pressed ? 1.0f : 0.0f,
                                                             pressSpeed,
                                                             deltaSeconds);
        const float hover = instance.hoverBlend.value();
        const float press = instance.pressBlend.value();
        const Color hoverColor = stateColorsVisible ? mixColor(element.color, element.hoverColor, hover) : element.color;
        const Color currentColor = stateColorsVisible ? mixColor(hoverColor, element.pressedColor, press) : element.color;
        const bool gradientChanged = !sameGradient(instance.gradient, element.gradient);
        if (gradientChanged) {
            instance.gradient = element.gradient;
        }

        LayoutRect targetFrame = element.frame;
        if (!element.sliderFillSourceId.empty()) {
            const auto state = sliderStates_.find(element.sliderFillSourceId);
            if (state != sliderStates_.end()) {
                targetFrame.width = std::max(0.0f, state->second.width * state->second.value);
            }
        }

        bool changed = hoverChanged || pressChanged || gradientChanged;
        changed = instance.frame.setTarget(targetFrame, element.transition, !snapFrame && shouldAnimateFrame(element)) || changed;
        changed = instance.color.setTarget(currentColor, element.transition, shouldAnimate(element, AnimProperty::Color)) || changed;
        changed = instance.radius.setTarget(element.radius, element.transition, shouldAnimate(element, AnimProperty::Radius)) || changed;
        changed = instance.blur.setTarget(element.blur, element.transition, shouldAnimate(element, AnimProperty::Blur)) || changed;
        changed = instance.opacity.setTarget(element.opacity, element.transition, shouldAnimate(element, AnimProperty::Opacity)) || changed;
        changed = instance.border.setTarget(element.border, element.transition, shouldAnimate(element, AnimProperty::Border)) || changed;
        changed = instance.shadow.setTarget(element.shadow, element.transition, shouldAnimate(element, AnimProperty::Shadow)) || changed;
        changed = instance.transform.setTarget(runtimeTransformForElement(element, element.transform), element.transition, shouldAnimate(element, AnimProperty::Transform)) || changed;

        changed = instance.frame.tick(deltaSeconds) || changed;
        changed = instance.color.tick(deltaSeconds) || changed;
        changed = instance.radius.tick(deltaSeconds) || changed;
        changed = instance.blur.tick(deltaSeconds) || changed;
        changed = instance.opacity.tick(deltaSeconds) || changed;
        changed = instance.border.tick(deltaSeconds) || changed;
        changed = instance.shadow.tick(deltaSeconds) || changed;
        changed = instance.transform.tick(deltaSeconds) || changed;

        if (changed) {
            const Rect afterRect = applyRenderTransformToLogicalRect(
                visualRect(instance.frame.value(), instance.shadow.value(), instance.blur.value(), instance.transform.value()),
                dpiScale,
                inheritedTransform);
            addDirtyUnion(beforeRect, afterRect);
        }
        animating_ = animating_ || isRectAnimating(element, instance);
    }

    void updatePolygon(const Element& element,
                       float deltaSeconds,
                       float dpiScale,
                       const RenderTransform& inheritedTransform,
                       bool snapFrame) {
        PolygonInstance& instance = polygonInstance(element.id);
        instance.interaction = interactionInstance(element.id).state;
        const Rect beforeRect = applyRenderTransformToLogicalRect(
            transformRect({instance.frame.value().x, instance.frame.value().y, instance.frame.value().width, instance.frame.value().height},
                          instance.frame.value(),
                          instance.transform.value()),
            dpiScale,
            inheritedTransform);

        const bool interactive = element.interactive && !element.disabled;
        const bool stateColorsVisible = element.hasStateColors &&
            (!closeEnough(element.color, element.hoverColor) || !closeEnough(element.color, element.pressedColor));
        const float hoverSpeed = element.smoothStateColors ? 12.0f : 0.0f;
        const float pressSpeed = element.smoothStateColors ? 20.0f : 0.0f;
        const bool hoverChanged = instance.hoverBlend.update(interactive && stateColorsVisible && instance.interaction.hover ? 1.0f : 0.0f,
                                                             hoverSpeed,
                                                             deltaSeconds);
        const bool pressChanged = instance.pressBlend.update(interactive && stateColorsVisible && instance.interaction.pressed ? 1.0f : 0.0f,
                                                             pressSpeed,
                                                             deltaSeconds);
        const float hover = instance.hoverBlend.value();
        const float press = instance.pressBlend.value();
        const Color hoverColor = stateColorsVisible ? mixColor(element.color, element.hoverColor, hover) : element.color;
        const Color currentColor = stateColorsVisible ? mixColor(hoverColor, element.pressedColor, press) : element.color;
        const bool pointsChanged = !samePoints(instance.points, element.polygonPoints);
        if (pointsChanged) {
            instance.points = element.polygonPoints;
        }

        bool changed = hoverChanged || pressChanged || pointsChanged;
        changed = instance.frame.setTarget(element.frame, element.transition, !snapFrame && shouldAnimateFrame(element)) || changed;
        changed = instance.color.setTarget(currentColor, element.transition, shouldAnimate(element, AnimProperty::Color)) || changed;
        changed = instance.opacity.setTarget(element.opacity, element.transition, shouldAnimate(element, AnimProperty::Opacity)) || changed;
        changed = instance.transform.setTarget(runtimeTransformForElement(element, element.transform), element.transition, shouldAnimate(element, AnimProperty::Transform)) || changed;

        changed = instance.frame.tick(deltaSeconds) || changed;
        changed = instance.color.tick(deltaSeconds) || changed;
        changed = instance.opacity.tick(deltaSeconds) || changed;
        changed = instance.transform.tick(deltaSeconds) || changed;

        if (changed) {
            const Rect afterRect = applyRenderTransformToLogicalRect(
                transformRect({instance.frame.value().x, instance.frame.value().y, instance.frame.value().width, instance.frame.value().height},
                              instance.frame.value(),
                              instance.transform.value()),
                dpiScale,
                inheritedTransform);
            addDirtyUnion(beforeRect, afterRect);
        }
        animating_ = animating_ || isPolygonAnimating(element, instance);
    }

    void updateText(const Element& element,
                    float deltaSeconds,
                    float dpiScale,
                    const RenderTransform& inheritedTransform,
                    bool snapFrame) {
        TextInstance& instance = textInstance(element.id);
        const Rect beforeRect = applyRenderTransformToLogicalRect(
            transformRect({instance.frame.value().x,
                           instance.frame.value().y,
                           instance.frame.value().width,
                           instance.frame.value().height},
                          instance.frame.value(),
                          instance.transform.value()),
            dpiScale,
            inheritedTransform);

        const bool keyedContent = !element.dirtyKey.empty();
        const bool textChanged = keyedContent
            ? instance.contentDirtyKey != element.dirtyKey
            : instance.text != element.text;
        const bool contentChanged =
            textChanged ||
            instance.fontFamily != element.fontFamily ||
            instance.fontSize != element.fontSize ||
            instance.fontWeight != element.fontWeight ||
            instance.maxWidth != element.maxWidth ||
            instance.wrap != element.wrap ||
            instance.horizontalAlign != element.horizontalAlign ||
            instance.verticalAlign != element.verticalAlign ||
            instance.lineHeight != element.lineHeight;
        if (contentChanged) {
            instance.text = element.text;
            instance.contentDirtyKey = element.dirtyKey;
            instance.fontFamily = element.fontFamily;
            instance.fontSize = element.fontSize;
            instance.fontWeight = element.fontWeight;
            instance.maxWidth = element.maxWidth;
            instance.wrap = element.wrap;
            instance.horizontalAlign = element.horizontalAlign;
            instance.verticalAlign = element.verticalAlign;
            instance.lineHeight = element.lineHeight;
        }

        bool changed = false;
        changed = instance.frame.setTarget(element.frame, element.transition, !snapFrame && shouldAnimateFrame(element)) || changed;
        changed = instance.color.setTarget(element.textColor, element.transition, shouldAnimate(element, AnimProperty::TextColor)) || changed;
        changed = instance.opacity.setTarget(element.opacity, element.transition, shouldAnimate(element, AnimProperty::Opacity)) || changed;
        changed = instance.transform.setTarget(runtimeTransformForElement(element, element.transform), element.transition, shouldAnimate(element, AnimProperty::Transform)) || changed;

        changed = instance.frame.tick(deltaSeconds) || changed;
        changed = instance.color.tick(deltaSeconds) || changed;
        changed = instance.opacity.tick(deltaSeconds) || changed;
        changed = instance.transform.tick(deltaSeconds) || changed;

        if (changed || contentChanged) {
            const Rect afterRect = applyRenderTransformToLogicalRect(
                transformRect({instance.frame.value().x,
                               instance.frame.value().y,
                               instance.frame.value().width,
                               instance.frame.value().height},
                              instance.frame.value(),
                              instance.transform.value()),
                dpiScale,
                inheritedTransform);
            addDirtyUnion(beforeRect, afterRect);
        }
        animating_ = animating_ || isTextAnimating(instance);
    }

    void updateImage(const Element& element,
                     float deltaSeconds,
                     float dpiScale,
                     const RenderTransform& inheritedTransform,
                     bool snapFrame) {
        ImageInstance& instance = imageInstance(element.id);
        const Rect beforeRect = applyRenderTransformToLogicalRect(
            imageVisualRect(instance.frame.value(), instance.transform.value()),
            dpiScale,
            inheritedTransform);

        bool changed = false;
        changed = instance.frame.setTarget(element.frame, element.transition, !snapFrame && shouldAnimateFrame(element)) || changed;
        changed = instance.tint.setTarget(element.color, element.transition, shouldAnimate(element, AnimProperty::Color)) || changed;
        changed = instance.radius.setTarget(element.radius, element.transition, shouldAnimate(element, AnimProperty::Radius)) || changed;
        changed = instance.opacity.setTarget(element.opacity, element.transition, shouldAnimate(element, AnimProperty::Opacity)) || changed;
        changed = instance.transform.setTarget(runtimeTransformForElement(element, element.transform), element.transition, shouldAnimate(element, AnimProperty::Transform)) || changed;

        changed = instance.frame.tick(deltaSeconds) || changed;
        changed = instance.tint.tick(deltaSeconds) || changed;
        changed = instance.radius.tick(deltaSeconds) || changed;
        changed = instance.opacity.tick(deltaSeconds) || changed;
        changed = instance.transform.tick(deltaSeconds) || changed;

        if (instance.hasCoverViewport != element.imageHasCoverViewport ||
            !closeEnough(instance.coverViewportSize, element.imageCoverViewportSize) ||
            !closeEnough(instance.coverViewportOffset, element.imageCoverViewportOffset)) {
            instance.hasCoverViewport = element.imageHasCoverViewport;
            instance.coverViewportSize = element.imageCoverViewportSize;
            instance.coverViewportOffset = element.imageCoverViewportOffset;
            changed = true;
        }

        const bool sourceChanged = instance.source != element.imageSource ||
                                   instance.svgSource != element.svgSource ||
                                   instance.flipVertically != element.imageFlipVertically ||
                                   instance.fit != element.imageFit;
        if (sourceChanged) {
            instance.source = element.imageSource;
            instance.svgSource = element.svgSource;
            instance.flipVertically = element.imageFlipVertically;
            instance.fit = element.imageFit;
            if (element.kind == ElementKind::Svg) {
                instance.primitive->setSvgSource(element.id, instance.svgSource);
            } else {
                instance.primitive->setSource(instance.source);
            }
            instance.primitive->setFlipVertically(instance.flipVertically);
            instance.primitive->setFit(instance.fit);
            changed = true;
        }

        const LayoutRect frame = instance.frame.value();
        instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
        if (instance.primitive->updateTexture()) {
            changed = true;
        }

        if (changed) {
            const Rect afterRect = applyRenderTransformToLogicalRect(
                imageVisualRect(instance.frame.value(), instance.transform.value()),
                dpiScale,
                inheritedTransform);
            addDirtyUnion(beforeRect, afterRect);
        }
        animating_ = animating_ || isImageAnimating(instance);
    }

    DependentVisualState dependentVisualStateForElement(const Element& element,
                                                        float dpiScale,
                                                        const RenderTransform& inheritedTransform) const {
        DependentVisualState state;
        state.rect = inflateRect(visualDirtyRectForElement(element, dpiScale, inheritedTransform),
                                 dependentVisualPadding());

        if (!element.hoverOpacitySourceId.empty()) {
            float hover = 0.0f;
            if (hoverBlendForSource(element.hoverOpacitySourceId, hover)) {
                hover = std::clamp(hover, 0.0f, 1.0f);
                state.opacity *= lerpValue(element.hoverHiddenOpacity, element.hoverVisibleOpacity, hover);
            } else {
                state.opacity *= element.hoverHiddenOpacity;
            }
        }

        if (!element.visualStateSourceId.empty()) {
            float press = 0.0f;
            LayoutRect sourceFrame;
            if (pressBlendForSource(element.visualStateSourceId, press, sourceFrame)) {
                (void)sourceFrame;
                state.scale = 1.0f - (1.0f - element.pressedScale) * press;
            }
        }

        state.seen = true;
        return state;
    }

    void updateDependentVisualDirtyRegions(float dpiScale) {
        for (auto& item : dependentVisualStates_) {
            item.second.seen = false;
        }

        const RenderTransform identity;
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (const Element* root : roots) {
            updateDependentVisualDirtyRegions(*root, dpiScale, identity);
        }

        for (auto item = dependentVisualStates_.begin(); item != dependentVisualStates_.end(); ) {
            if (item->second.seen) {
                ++item;
                continue;
            }
            addDirtyRect(item->second.rect);
            item = dependentVisualStates_.erase(item);
        }
    }

    void updateDependentVisualDirtyRegions(const Element& element,
                                           float dpiScale,
                                           const RenderTransform& inheritedTransform) {
        if (!element.hoverOpacitySourceId.empty() || !element.visualStateSourceId.empty()) {
            const DependentVisualState current = dependentVisualStateForElement(element, dpiScale, inheritedTransform);
            auto item = dependentVisualStates_.find(element.id);
            if (item == dependentVisualStates_.end()) {
                dependentVisualStates_.emplace(element.id, current);
                if (!fullRedraw_ && current.opacity > 0.001f) {
                    addDirtyRect(current.rect);
                }
            } else {
                DependentVisualState& previous = item->second;
                previous.seen = true;
                const bool changed =
                    !closeEnough(previous.rect, current.rect) ||
                    !closeEnough(previous.opacity, current.opacity) ||
                    !closeEnough(previous.scale, current.scale);
                if (changed) {
                    addDirtyUnion(previous.rect, current.rect);
                    previous.rect = current.rect;
                    previous.opacity = current.opacity;
                    previous.scale = current.scale;
                }
            }
        }

        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            updateDependentVisualDirtyRegions(*child, dpiScale, renderTransform);
        }
    }

    bool hoverBlendForSource(const std::string& id, float& value) const {
        const auto rect = rects_.find(id);
        if (rect != rects_.end()) {
            value = rect->second.hoverBlend.value();
            return true;
        }
        const auto polygon = polygons_.find(id);
        if (polygon != polygons_.end()) {
            value = polygon->second.hoverBlend.value();
            return true;
        }
        return false;
    }

    bool pressBlendForSource(const std::string& id, float& value, LayoutRect& frame) const {
        const auto rect = rects_.find(id);
        if (rect != rects_.end()) {
            value = rect->second.pressBlend.value();
            frame = rect->second.frame.value();
            return true;
        }
        const auto polygon = polygons_.find(id);
        if (polygon != polygons_.end()) {
            value = polygon->second.pressBlend.value();
            frame = polygon->second.frame.value();
            return true;
        }
        return false;
    }

    RenderTransform resolveRenderTransform(const Element& element, float dpiScale, const RenderTransform& inherited) const {
        RenderTransform result = inherited;

        if (element.kind == ElementKind::Row ||
            element.kind == ElementKind::Column ||
            element.kind == ElementKind::Stack) {
            const auto layout = layouts_.find(element.id);
            if (layout != layouts_.end()) {
                const Transform local = layout->second.transform.value();
                const float opacity = layout->second.opacity.value();
                const bool hasTransform = !isIdentityTransform(local);
                const bool hasOpacity = !closeEnough(opacity, 1.0f);
                if (hasTransform || hasOpacity) {
                    Transform scaled = scaleTransform(local, dpiScale);
                    result = appendRenderMatrix(result, matrixForTransform(toPixelRect(element.frame, dpiScale), scaled));
                    result.opacity *= opacity;
                }
            }
        }

        if (!element.hoverOpacitySourceId.empty()) {
            float hover = 0.0f;
            if (hoverBlendForSource(element.hoverOpacitySourceId, hover)) {
                hover = std::clamp(hover, 0.0f, 1.0f);
                result.opacity *= lerpValue(element.hoverHiddenOpacity, element.hoverVisibleOpacity, hover);
            } else {
                result.opacity *= element.hoverHiddenOpacity;
            }
        }

        if (!element.visualStateSourceId.empty()) {
            float press = 0.0f;
            LayoutRect sourceFrame;
            if (pressBlendForSource(element.visualStateSourceId, press, sourceFrame)) {
                const float scale = 1.0f - (1.0f - element.pressedScale) * press;
                if (std::fabs(scale - 1.0f) > 0.0001f) {
                    result = appendRenderMatrix(result, matrixForScaleAround(toPixelRect(sourceFrame, dpiScale), scale));
                }
            }
        }
        return result;
    }

    static bool ownsScrollState(const Element& element) {
        return !element.scrollStateId.empty() && element.id == element.scrollStateId;
    }

    static bool ownsSliderState(const Element& element) {
        return !element.sliderStateId.empty() && element.id == element.sliderStateId;
    }

    void syncScrollStateElement(const Element& element) {
        if (element.scrollStateId.empty()) {
            return;
        }

        ScrollStateInstance& instance = scrollStateInstance(element.scrollStateId);
        if (!ownsScrollState(element)) {
            return;
        }

        instance.maxOffset = std::max(0.0f, element.scrollMaxOffset);
        instance.step = std::max(1.0f, element.scrollStep);
        instance.dirtyRect = {element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        instance.hasDirtyRect = true;
        if (!instance.initialized) {
            instance.offset = std::clamp(element.scrollOffset, 0.0f, instance.maxOffset);
            instance.initialized = true;
        } else {
            instance.offset = std::clamp(instance.offset, 0.0f, instance.maxOffset);
        }
    }

    void syncSliderStateElement(const Element& element) {
        if (element.sliderStateId.empty()) {
            return;
        }

        SliderStateInstance& instance = sliderStateInstance(element.sliderStateId);
        if (!ownsSliderState(element)) {
            return;
        }

        instance.width = std::max(0.0f, element.sliderWidth);
        instance.knobSize = std::max(0.0f, element.sliderKnobSize);
        instance.dirtyRect = {element.frame.x, element.frame.y, element.frame.width, element.frame.height};
        instance.hasDirtyRect = true;
        if (!instance.initialized || !instance.dragging) {
            instance.value = std::clamp(element.sliderValue, 0.0f, 1.0f);
            instance.initialized = true;
        }
    }

    void syncScrollStateBindings() {
        forEachElement([&](const Element& element) {
            syncScrollStateElement(element);
            syncSliderStateElement(element);
        });
    }

    float scrollStepFor(const Element& element) const {
        const auto state = scrollStates_.find(element.scrollStateId);
        if (state != scrollStates_.end()) {
            return state->second.step;
        }
        return std::max(1.0f, element.scrollStep);
    }

    void addScrollDirtyRect(const ScrollStateInstance& instance) {
        if (instance.hasDirtyRect) {
            addDirtyRect(instance.dirtyRect);
        }
    }

    void addSliderDirtyRect(const SliderStateInstance& instance) {
        if (instance.hasDirtyRect) {
            addDirtyRect(instance.dirtyRect);
        }
    }

    void setScrollOffset(const std::string& stateId, float offset) {
        auto state = scrollStates_.find(stateId);
        if (state == scrollStates_.end()) {
            return;
        }

        ScrollStateInstance& instance = state->second;
        const float next = std::clamp(offset, 0.0f, instance.maxOffset);
        if (closeEnough(next, instance.offset)) {
            return;
        }

        instance.offset = next;
        addScrollDirtyRect(instance);
        if (const Element* owner = ui_.find(stateId)) {
            if (owner->onScrollOffsetChanged && !owner->disabled) {
                owner->onScrollOffsetChanged(instance.offset);
            }
        }
    }

    void applyRuntimeScroll(const Element& element, float delta) {
        if (element.scrollStateId.empty()) {
            return;
        }
        const auto state = scrollStates_.find(element.scrollStateId);
        if (state == scrollStates_.end()) {
            return;
        }
        setScrollOffset(element.scrollStateId, state->second.offset + delta);
    }

    void beginRuntimeScrollDrag(const Element& element) {
        if (element.scrollDragSourceId.empty()) {
            return;
        }
        auto state = scrollStates_.find(element.scrollDragSourceId);
        if (state == scrollStates_.end()) {
            return;
        }
        state->second.dragStartOffset = state->second.offset;
    }

    void updateRuntimeScrollDrag(const Element& element, double dragDeltaY, float dpiScale) {
        if (element.scrollDragSourceId.empty() || element.scrollDragTravel <= 0.0f) {
            return;
        }
        const auto state = scrollStates_.find(element.scrollDragSourceId);
        if (state == scrollStates_.end() || state->second.maxOffset <= 0.0f) {
            return;
        }
        const float logicalDeltaY = static_cast<float>(dragDeltaY) / std::max(0.001f, dpiScale);
        const float next = state->second.dragStartOffset +
                           logicalDeltaY * (state->second.maxOffset / element.scrollDragTravel);
        setScrollOffset(element.scrollDragSourceId, next);
    }

    float sliderValueFromPointer(const Element& element, double pointerX, float dpiScale) const {
        if (element.sliderInputSourceId.empty()) {
            return 0.0f;
        }
        const auto state = sliderStates_.find(element.sliderInputSourceId);
        if (state == sliderStates_.end()) {
            return 0.0f;
        }

        const Element* owner = ui_.find(element.sliderInputSourceId);
        if (owner == nullptr) {
            return 0.0f;
        }

        const Rect bounds = toPixelRect(owner->frame, dpiScale);
        const float localX = static_cast<float>(pointerX - bounds.x);
        return std::clamp(localX / std::max(1.0f, bounds.width), 0.0f, 1.0f);
    }

    void setSliderValue(const std::string& stateId, float value, bool dragging) {
        auto state = sliderStates_.find(stateId);
        if (state == sliderStates_.end()) {
            return;
        }

        SliderStateInstance& instance = state->second;
        const float next = std::clamp(value, 0.0f, 1.0f);
        const bool changed = !closeEnough(next, instance.value);
        instance.dragging = dragging;
        if (!changed) {
            return;
        }

        instance.value = next;
        addSliderDirtyRect(instance);
        needsRender_ = true;
        if (const Element* owner = ui_.find(stateId)) {
            if (owner->onSliderValueChanged && !owner->disabled) {
                owner->onSliderValueChanged(instance.value);
            }
        }
    }

    void updateRuntimeSlider(const Element& element, double pointerX, float dpiScale, bool dragging) {
        if (element.sliderInputSourceId.empty()) {
            return;
        }
        setSliderValue(element.sliderInputSourceId, sliderValueFromPointer(element, pointerX, dpiScale), dragging);
    }

    std::vector<Rect> resolveDirtyRects(int windowWidth, int windowHeight, float dpiScale) const {
        if (fullRedraw_) {
            return {};
        }

        std::vector<Rect> rects;
        Rect merged;
        bool hasMerged = false;
        for (const LogicalDirtyRect& dirty : dirtyRects_) {
            const Rect logicalRect{dirty.x, dirty.y, dirty.width, dirty.height};
            Rect rect = toPixelRect(logicalRect, dpiScale);
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

    static void applyOptionalScissor(core::render::RenderBackend& renderBackend, bool enabled, const Rect& rect, int windowHeight) {
        renderBackend.setScissor(enabled, rect, windowHeight);
    }

    void renderDirect(core::render::RenderBackend& renderBackend, int windowWidth, int windowHeight, float dpiScale, const Rect* dirtyRect = nullptr) {
        const RenderTransform identity;
        const bool hasScissor = dirtyRect != nullptr;
        const Rect scissor = dirtyRect ? *dirtyRect : Rect{};
        const std::vector<const Element*> roots = orderedElements(ui_.roots());
        for (const Element* root : roots) {
            prepareTextElement(*root, windowWidth, windowHeight, dpiScale, identity, dirtyRect, hasScissor, scissor);
        }
        for (const Element* root : roots) {
            renderElement(renderBackend, *root, windowWidth, windowHeight, dpiScale, identity, dirtyRect, hasScissor, scissor);
        }
    }

    void prepareTextElement(const Element& element,
                            int windowWidth,
                            int windowHeight,
                            float dpiScale,
                            const RenderTransform& inheritedTransform,
                            const Rect* dirtyRect = nullptr,
                            bool hasScissor = false,
                            const Rect& scissorRect = {}) {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        if (renderTransform.opacity <= 0.001f) {
            return;
        }

        Rect effectiveScissor = scissorRect;
        bool effectiveHasScissor = hasScissor;
        if (element.clip) {
            Rect clipFrame = applyRenderTransform(toPixelRect(element.frame, dpiScale), renderTransform);
            if (effectiveHasScissor) {
                if (!intersectRect(effectiveScissor, clipFrame, effectiveScissor)) {
                    return;
                }
            } else {
                effectiveScissor = clipFrame;
                effectiveHasScissor = true;
            }
        }

        if (element.kind == ElementKind::Text) {
            TextInstance& instance = textInstance(element.id);
            Rect frame = toPixelRect(transformRect({instance.frame.value().x,
                                                    instance.frame.value().y,
                                                    instance.frame.value().width,
                                                    instance.frame.value().height},
                                                   instance.frame.value(),
                                                   instance.transform.value()), dpiScale);
            frame = applyRenderTransform(frame, renderTransform);
            if ((!dirtyRect || intersects(frame, *dirtyRect)) &&
                (!effectiveHasScissor || intersects(frame, effectiveScissor))) {
                prepareText(element, windowWidth, windowHeight, dpiScale, renderTransform);
            }
        }

        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            prepareTextElement(*child, windowWidth, windowHeight, dpiScale, renderTransform, dirtyRect, effectiveHasScissor, effectiveScissor);
        }
    }

    void renderElement(core::render::RenderBackend& renderBackend,
                       const Element& element,
                       int windowWidth,
                       int windowHeight,
                       float dpiScale,
                       const RenderTransform& inheritedTransform,
                       const Rect* dirtyRect = nullptr,
                       bool hasScissor = false,
                       const Rect& scissorRect = {}) {
        const RenderTransform renderTransform = resolveRenderTransform(element, dpiScale, inheritedTransform);
        if (renderTransform.opacity <= 0.001f) {
            return;
        }
        Rect effectiveScissor = scissorRect;
        bool effectiveHasScissor = hasScissor;
        if (element.clip) {
            Rect clipFrame = applyRenderTransform(toPixelRect(element.frame, dpiScale), renderTransform);
            if (effectiveHasScissor) {
                if (!intersectRect(effectiveScissor, clipFrame, effectiveScissor)) {
                    return;
                }
            } else {
                effectiveScissor = clipFrame;
                effectiveHasScissor = true;
            }
        }

        if (element.kind == ElementKind::Rect) {
            Rect visual = toPixelRect(visualRect(rectInstance(element.id).frame.value(),
                                                rectInstance(element.id).shadow.value(),
                                                rectInstance(element.id).blur.value(),
                                                rectInstance(element.id).transform.value()), dpiScale);
            visual = applyRenderTransform(visual, renderTransform);
            if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
                (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
                applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
                renderRect(element, windowWidth, windowHeight, dpiScale, renderTransform);
            }
        } else if (element.kind == ElementKind::Polygon) {
            Rect visual = toPixelRect(visualRect(polygonInstance(element.id).frame.value(),
                                                Shadow{},
                                                0.0f,
                                                polygonInstance(element.id).transform.value()), dpiScale);
            visual = applyRenderTransform(visual, renderTransform);
            if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
                (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
                applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
                renderPolygon(element, windowWidth, windowHeight, dpiScale, renderTransform);
            }
        } else if (element.kind == ElementKind::Text) {
            TextInstance& instance = textInstance(element.id);
            Rect frame = toPixelRect(transformRect({instance.frame.value().x,
                                                    instance.frame.value().y,
                                                    instance.frame.value().width,
                                                    instance.frame.value().height},
                                                   instance.frame.value(),
                                                   instance.transform.value()), dpiScale);
            frame = applyRenderTransform(frame, renderTransform);
            if ((!dirtyRect || intersects(frame, *dirtyRect)) &&
                (!effectiveHasScissor || intersects(frame, effectiveScissor))) {
                applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
                renderText(element, windowWidth, windowHeight, dpiScale, renderTransform);
            }
        } else if (element.kind == ElementKind::Image || element.kind == ElementKind::Svg) {
            Rect visual = toPixelRect(imageVisualRect(imageInstance(element.id).frame.value(),
                                                     imageInstance(element.id).transform.value()), dpiScale);
            visual = applyRenderTransform(visual, renderTransform);
            if ((!dirtyRect || intersects(visual, *dirtyRect)) &&
                (!effectiveHasScissor || intersects(visual, effectiveScissor))) {
                applyOptionalScissor(renderBackend, effectiveHasScissor, effectiveScissor, windowHeight);
                renderImage(element, windowWidth, windowHeight, dpiScale, renderTransform);
            }
        }

        const std::vector<const Element*> children = orderedElements(element.children);
        for (const Element* child : children) {
            renderElement(renderBackend, *child, windowWidth, windowHeight, dpiScale, renderTransform, dirtyRect, effectiveHasScissor, effectiveScissor);
        }
    }

    void renderRect(const Element& element,
                    int windowWidth,
                    int windowHeight,
                    float dpiScale,
                    const RenderTransform& renderTransform) {
        RectInstance& instance = rectInstance(element.id);
        if (!instance.initialized) {
            instance.initialized = instance.primitive->initialize();
            if (!instance.initialized) {
                return;
            }
        }

        const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
        const Color currentColor = instance.color.value();
        Transform transform = scaleTransform(instance.transform.value(), dpiScale);

        instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
        instance.primitive->setColor(currentColor);
        instance.primitive->setGradient(element.gradient);
        instance.primitive->setBorder(scaleBorder(instance.border.value(), dpiScale));
        instance.primitive->setShadow(scaleShadow(instance.shadow.value(), dpiScale));
        instance.primitive->setCornerRadius(toPixels(instance.radius.value(), dpiScale));
        instance.primitive->setBlur(toPixels(instance.blur.value(), dpiScale));
        instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
        instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
        instance.primitive->render(windowWidth, windowHeight);
    }

    std::vector<Vec2> scaledPolygonPoints(const std::vector<Vec2>& points, float dpiScale) const {
        std::vector<Vec2> result;
        result.reserve(points.size());
        for (const Vec2& point : points) {
            result.push_back({toPixels(point.x, dpiScale), toPixels(point.y, dpiScale)});
        }
        return result;
    }

    void renderPolygon(const Element& element,
                       int windowWidth,
                       int windowHeight,
                       float dpiScale,
                       const RenderTransform& renderTransform) {
        PolygonInstance& instance = polygonInstance(element.id);
        if (!instance.initialized) {
            instance.initialized = instance.primitive->initialize();
            if (!instance.initialized) {
                return;
            }
        }

        const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
        Transform transform = scaleTransform(instance.transform.value(), dpiScale);

        instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
        instance.primitive->setPoints(scaledPolygonPoints(instance.points, dpiScale));
        instance.primitive->setColor(instance.color.value());
        instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
        instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
        instance.primitive->render(windowWidth, windowHeight);
    }

    void prepareText(const Element& element,
                     int,
                     int,
                     float dpiScale,
                     const RenderTransform& renderTransform) {
        TextInstance& instance = textInstance(element.id);
        if (!instance.initialized) {
            instance.initialized = instance.primitive->initialize();
            if (!instance.initialized) {
                return;
            }
        }

        const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
        const float maxWidth = element.maxWidth > 0.0f ? toPixels(element.maxWidth, dpiScale) : frame.width;
        const float lineHeight = element.lineHeight > 0.0f ? toPixels(element.lineHeight, dpiScale) : 0.0f;
        Color textColor = instance.color.value();
        Transform transform = scaleTransform(instance.transform.value(), dpiScale);
        textColor.a *= instance.opacity.value();

        float x = frame.x;
        if (element.horizontalAlign == HorizontalAlign::Center) {
            x = frame.x + frame.width * 0.5f;
        } else if (element.horizontalAlign == HorizontalAlign::Right) {
            x = frame.x + frame.width;
        }

        float y = frame.y;
        if (element.verticalAlign == VerticalAlign::Center) {
            y = frame.y + frame.height * 0.5f;
        } else if (element.verticalAlign == VerticalAlign::Bottom) {
            y = frame.y + frame.height;
        }
        textColor.a *= renderTransform.opacity;

        instance.primitive->setPosition(x, y);
        instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
        instance.primitive->setColor(textColor);
        instance.primitive->setText(instance.text);
        instance.primitive->setFontFamily(instance.fontFamily);
        instance.primitive->setFontSize(toPixels(instance.fontSize, dpiScale));
        instance.primitive->setFontWeight(instance.fontWeight);
        instance.primitive->setMaxWidth(maxWidth);
        instance.primitive->setWrap(instance.wrap);
        instance.primitive->setHorizontalAlign(instance.horizontalAlign);
        instance.primitive->setVerticalAlign(instance.verticalAlign);
        instance.primitive->setLineHeight(lineHeight);
        instance.primitive->prepare();
    }

    void renderText(const Element& element,
                    int windowWidth,
                    int windowHeight,
                    float dpiScale,
                    const RenderTransform& renderTransform) {
        TextInstance& instance = textInstance(element.id);
        if (!instance.initialized) {
            instance.initialized = instance.primitive->initialize();
            if (!instance.initialized) {
                return;
            }
        }

        const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
        const float maxWidth = element.maxWidth > 0.0f ? toPixels(element.maxWidth, dpiScale) : frame.width;
        const float lineHeight = element.lineHeight > 0.0f ? toPixels(element.lineHeight, dpiScale) : 0.0f;
        Color textColor = instance.color.value();
        Transform transform = scaleTransform(instance.transform.value(), dpiScale);
        textColor.a *= instance.opacity.value();

        float x = frame.x;
        if (element.horizontalAlign == HorizontalAlign::Center) {
            x = frame.x + frame.width * 0.5f;
        } else if (element.horizontalAlign == HorizontalAlign::Right) {
            x = frame.x + frame.width;
        }

        float y = frame.y;
        if (element.verticalAlign == VerticalAlign::Center) {
            y = frame.y + frame.height * 0.5f;
        } else if (element.verticalAlign == VerticalAlign::Bottom) {
            y = frame.y + frame.height;
        }
        textColor.a *= renderTransform.opacity;

        instance.primitive->setPosition(x, y);
        instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
        instance.primitive->setColor(textColor);
        instance.primitive->setText(instance.text);
        instance.primitive->setFontFamily(instance.fontFamily);
        instance.primitive->setFontSize(toPixels(instance.fontSize, dpiScale));
        instance.primitive->setFontWeight(instance.fontWeight);
        instance.primitive->setMaxWidth(maxWidth);
        instance.primitive->setWrap(instance.wrap);
        instance.primitive->setHorizontalAlign(instance.horizontalAlign);
        instance.primitive->setVerticalAlign(instance.verticalAlign);
        instance.primitive->setLineHeight(lineHeight);
        instance.primitive->render(windowWidth, windowHeight);
    }

    void renderImage(const Element& element,
                     int windowWidth,
                     int windowHeight,
                     float dpiScale,
                     const RenderTransform& renderTransform) {
        ImageInstance& instance = imageInstance(element.id);
        if (!instance.initialized) {
            instance.initialized = instance.primitive->initialize();
            if (!instance.initialized) {
                return;
            }
        }

        const Rect frame = toPixelRect(instance.frame.value(), dpiScale);
        Transform transform = scaleTransform(instance.transform.value(), dpiScale);

        instance.primitive->setBounds(frame.x, frame.y, frame.width, frame.height);
        instance.primitive->setTint(instance.tint.value());
        instance.primitive->setCornerRadius(toPixels(instance.radius.value(), dpiScale));
        instance.primitive->setOpacity(instance.opacity.value() * renderTransform.opacity);
        instance.primitive->setTransformMatrix(combinedPrimitiveMatrix(renderTransform, frame, transform));
        instance.primitive->setFit(instance.fit);
        instance.primitive->setCoverViewport(instance.hasCoverViewport,
                                             {toPixels(instance.coverViewportSize.x, dpiScale),
                                              toPixels(instance.coverViewportSize.y, dpiScale)},
                                             {toPixels(instance.coverViewportOffset.x, dpiScale),
                                              toPixels(instance.coverViewportOffset.y, dpiScale)});
        instance.primitive->render(windowWidth, windowHeight);
    }

    Ui ui_;
    std::unordered_map<std::string, RectInstance> rects_;
    std::unordered_map<std::string, PolygonInstance> polygons_;
    std::unordered_map<std::string, TextInstance> texts_;
    std::unordered_map<std::string, ImageInstance> images_;
    std::unordered_map<std::string, InteractionInstance> interactions_;
    std::unordered_map<std::string, DirtyKeyInstance> dirtyKeys_;
    std::unordered_map<std::string, LayoutInstance> layouts_;
    std::unordered_map<std::string, ScrollStateInstance> scrollStates_;
    std::unordered_map<std::string, SliderStateInstance> sliderStates_;
    std::unordered_map<std::string, TimerInstance> timers_;
    std::unordered_map<std::string, DependentVisualState> dependentVisualStates_;
    std::unordered_map<std::string, FrameTargetInstance> frameTargets_;
    std::vector<ElementSnapshot> elementStructure_;
    std::vector<LogicalDirtyRect> dirtyRects_;
    bool needsRender_ = true;
    bool animating_ = false;
    bool needsCompose_ = false;
    bool fullRedraw_ = true;
    bool wantsHandCursor_ = false;
    std::string focusedId_;
    float logicalWidth_ = 0.0f;
    float logicalHeight_ = 0.0f;
    core::window::CursorHandle arrowCursor_ = nullptr;
    core::window::CursorHandle handCursor_ = nullptr;
    core::window::CursorHandle currentCursor_ = nullptr;
    core::window::Handle imeCursorWindow_ = nullptr;
    Rect imeCursorRect_;
    bool imeCursorRectValid_ = false;
};

} // namespace core::dsl
