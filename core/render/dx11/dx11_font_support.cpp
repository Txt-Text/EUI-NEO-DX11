#include "core/render/dx11/dx11_font_support.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite_3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace core::render::dx11 {
namespace {

struct ResolvedFontEntry {
    std::wstring primaryFamily;
    FontSelection selection;
    bool needsRegistration = false;
    std::string registrationPath;
};

struct FontSupportState {
    // 这里统一维护字体路径、注册状态和 DirectWrite 字体集合缓存。
    std::mutex mutex;
    std::string defaultUiFontOverride;
    std::string defaultIconFontOverride;
    std::unordered_map<std::string, std::string> resolvedPathCache;
    std::unordered_map<std::string, bool> registeredFontStatus;
    std::unordered_map<std::string, ResolvedFontEntry> resolvedFontCache;
    std::map<std::string, Microsoft::WRL::ComPtr<IDWriteFontCollection>> fontCollectionCache;
};

FontSupportState& state() {
    static FontSupportState instance;
    return instance;
}

std::filesystem::path executableDirectory() {
    namespace fs = std::filesystem;
    char executablePath[MAX_PATH] = {};
    const DWORD size = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return {};
    }
    std::error_code error;
    return fs::absolute(fs::path(executablePath), error).parent_path();
}

std::string firstExistingPath(std::initializer_list<std::filesystem::path> candidates) {
    namespace fs = std::filesystem;
    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        ++core::render::currentRenderFrameStats().fontPathResolveAttempts;
        ++core::render::currentRenderFrameStats().fontFileExistsChecks;
        error.clear();
        if (fs::exists(candidate, error) && !error) {
            return fs::absolute(candidate, error).string();
        }
    }
    return {};
}

std::string resolveFontFilePathUncached(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const fs::path requested(path);
    const fs::path current = fs::current_path(error);
    const fs::path exeDir = executableDirectory();
    return firstExistingPath({
        requested,
        current / requested,
        current / "assets" / requested.filename(),
        exeDir / requested,
        exeDir / "assets" / requested.filename()
    });
}

std::string resolveFontFilePathWithState(const std::string& path) {
    FontSupportState& shared = state();
    std::scoped_lock lock(shared.mutex);
    const auto cached = shared.resolvedPathCache.find(path);
    if (cached != shared.resolvedPathCache.end()) {
        ++core::render::currentRenderFrameStats().fontPathCacheHits;
        return cached->second;
    }
    ++core::render::currentRenderFrameStats().fontPathCacheMisses;
    std::string resolved = resolveFontFilePathUncached(path);
    shared.resolvedPathCache.emplace(path, resolved);
    return resolved;
}

std::string currentDefaultUiOverride() {
    FontSupportState& shared = state();
    std::scoped_lock lock(shared.mutex);
    return shared.defaultUiFontOverride;
}

std::string currentDefaultIconOverride() {
    FontSupportState& shared = state();
    std::scoped_lock lock(shared.mutex);
    return shared.defaultIconFontOverride;
}

ResolvedFontEntry buildResolvedFontEntry(const std::string& family) {
    if (family == "monospace") {
        return {L"Consolas", {{L"Consolas", L"Cascadia Mono", L"Segoe UI"}, {}}, false, {}};
    }
    if (family == "Emoji") {
        return {L"Segoe UI Emoji", {{L"Segoe UI Emoji", L"Segoe UI Symbol", L"Segoe UI"}, {}}, false, {}};
    }
    if (family == "YouSheBiaoTiHei" || family == "YouShe") {
        const std::string path = resolveFontFilePathWithState("YouSheBiaoTiHei-2.ttf");
        return {L"YouSheBiaoTiHei", {{L"YouSheBiaoTiHei", L"Segoe UI"}, {}}, !path.empty(), path};
    }
    if (family == "JingNanJunJunTi" || family == "JingNan") {
        const std::string path = resolveFontFilePathWithState(currentDefaultUiOverride().empty()
            ? "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf"
            : currentDefaultUiOverride());
        return {L"JingNanJunJunTi", {{L"JingNanJunJunTi", L"Segoe UI"}, {}}, !path.empty(), path};
    }
    if (isIconFontFamily(family)) {
        const std::string path = resolveFontFilePathWithState(currentDefaultIconOverride().empty()
            ? "Font Awesome 7 Free-Solid-900.otf"
            : currentDefaultIconOverride());
        return {L"Font Awesome 7 Free Solid", {{L"Font Awesome 7 Free", L"Segoe UI Symbol"}, path}, !path.empty(), path};
    }
    if (family.empty()) {
        const std::string path = resolveFontFilePathWithState(currentDefaultUiOverride().empty()
            ? "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf"
            : currentDefaultUiOverride());
        return {L"JingNanJunJunTi", {{L"JingNanJunJunTi", L"Segoe UI"}, {}}, !path.empty(), path};
    }
    return {utf8ToWide(family), {{utf8ToWide(family), L"Segoe UI"}, {}}, false, {}};
}

ResolvedFontEntry resolveFontEntryCached(std::string_view familyView) {
    // family 名称到候选字体族/私有字体文件的解析成本较高，这里做一次缓存。
    const std::string family(familyView);
    FontSupportState& shared = state();
    {
        std::scoped_lock lock(shared.mutex);
        const auto cached = shared.resolvedFontCache.find(family);
        if (cached != shared.resolvedFontCache.end()) {
            ++core::render::currentRenderFrameStats().fontPathCacheHits;
            return cached->second;
        }
    }

    ResolvedFontEntry entry = buildResolvedFontEntry(family);
    {
        std::scoped_lock lock(shared.mutex);
        shared.resolvedFontCache.emplace(family, entry);
    }
    return entry;
}

} // namespace

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int sourceLength = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), sourceLength, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), sourceLength, result.data(), size);
    return result;
}

std::wstring utf8ToWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    return utf8ToWide(std::string_view(text, std::strlen(text)));
}

void setDefaultFontFileOverrides(const std::string& textFontFile, const std::string& iconFontFile) {
    FontSupportState& shared = state();
    std::scoped_lock lock(shared.mutex);
    shared.defaultUiFontOverride = textFontFile;
    shared.defaultIconFontOverride = iconFontFile;
    shared.resolvedFontCache.clear();
}

std::string resolveFontFilePathCached(const std::string& path) {
    return resolveFontFilePathWithState(path);
}

std::string resolveDefaultUiFontPathCached() {
    const std::string override = currentDefaultUiOverride();
    return resolveFontFilePathWithState(override.empty() ? "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf" : override);
}

std::string resolveDefaultIconFontPathCached() {
    const std::string override = currentDefaultIconOverride();
    return resolveFontFilePathWithState(override.empty() ? "Font Awesome 7 Free-Solid-900.otf" : override);
}

bool ensurePrivateFontRegisteredCached(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    FontSupportState& shared = state();
    {
        std::scoped_lock lock(shared.mutex);
        const auto cached = shared.registeredFontStatus.find(path);
        if (cached != shared.registeredFontStatus.end()) {
            ++core::render::currentRenderFrameStats().fontRegistrationCacheHits;
            return cached->second;
        }
    }

    const bool registered = AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr) > 0;
    {
        std::scoped_lock lock(shared.mutex);
        shared.registeredFontStatus.emplace(path, registered);
    }
    if (registered) {
        ++core::render::currentRenderFrameStats().fontRegistrations;
    }
    return registered;
}

bool isIconFontFamily(std::string_view family) {
    return family == "FontAwesome" || family == "Font Awesome" ||
           family == "Font Awesome 7 Free" || family == "Font Awesome 7 Free Solid" ||
           family == "Icon";
}

std::wstring resolvedFontFamily(const std::string& family) {
    const ResolvedFontEntry entry = resolveFontEntryCached(family);
    if (entry.needsRegistration) {
        ensurePrivateFontRegisteredCached(entry.registrationPath);
    }
    return entry.primaryFamily;
}

FontSelection candidateFontSelection(const char* family) {
    const ResolvedFontEntry entry = resolveFontEntryCached(family != nullptr ? std::string_view(family) : std::string_view());
    if (entry.needsRegistration) {
        ensurePrivateFontRegisteredCached(entry.registrationPath);
    }
    return entry.selection;
}

DWRITE_FONT_WEIGHT effectiveFontWeight(std::string_view family, int fontWeight) {
    const int weight = isIconFontFamily(family) ? std::max(fontWeight, 900) : fontWeight;
    return static_cast<DWRITE_FONT_WEIGHT>(std::clamp(weight, 1, 999));
}

void warmDefaultFontCaches() {
    const std::string uiPath = resolveDefaultUiFontPathCached();
    if (!uiPath.empty()) {
        ensurePrivateFontRegisteredCached(uiPath);
    }

    const std::string iconPath = resolveDefaultIconFontPathCached();
    if (!iconPath.empty()) {
        ensurePrivateFontRegisteredCached(iconPath);
    }

    (void)resolveFontEntryCached(std::string_view());
    (void)resolveFontEntryCached(std::string_view("FontAwesome"));
}

Microsoft::WRL::ComPtr<IDWriteFontCollection> cachedFontCollectionFromFile(IDWriteFactory* factory,
                                                                            const std::string& fontFilePath) {
    if (factory == nullptr || fontFilePath.empty()) {
        return {};
    }

    // icon/private 字体需要独立 font collection，这里按文件路径做缓存复用。
    FontSupportState& shared = state();
    {
        std::scoped_lock lock(shared.mutex);
        const auto cached = shared.fontCollectionCache.find(fontFilePath);
        if (cached != shared.fontCollectionCache.end()) {
            ++core::render::currentRenderFrameStats().fontCollectionCacheHits;
            return cached->second;
        }
    }

    ++core::render::currentRenderFrameStats().fontCollectionCacheMisses;
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory5;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) || factory5 == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> fontSetBuilder;
    if (FAILED(factory5->CreateFontSetBuilder(&fontSetBuilder)) || fontSetBuilder == nullptr) {
        return {};
    }

    const std::wstring widePath = utf8ToWide(fontFilePath);
    FILETIME lastWriteTime{};
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &attributes)) {
        lastWriteTime = attributes.ftLastWriteTime;
    }

    Microsoft::WRL::ComPtr<IDWriteFontFaceReference> faceReference;
    if (FAILED(factory5->CreateFontFaceReference(widePath.c_str(), &lastWriteTime, 0, DWRITE_FONT_SIMULATIONS_NONE, &faceReference)) ||
        faceReference == nullptr) {
        return {};
    }
    if (FAILED(fontSetBuilder->AddFontFaceReference(faceReference.Get()))) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(fontSetBuilder->CreateFontSet(&fontSet)) || fontSet == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection1;
    if (FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), &collection1)) || collection1 == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    collection1.As(&collection);

    std::scoped_lock lock(shared.mutex);
    shared.fontCollectionCache.emplace(fontFilePath, collection);
    return collection;
}

} // namespace core::render::dx11
