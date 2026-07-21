#pragma once

#include "core/render/render_backend.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <vector>

namespace core::render::dx11 {

struct FontSelection {
    // families 按优先级排列，fontFilePath 仅在需要私有字体集合时使用。
    std::vector<std::wstring> families;
    std::string fontFilePath;
};

std::wstring utf8ToWide(std::string_view text);
std::wstring utf8ToWide(const char* text);

void setDefaultFontFileOverrides(const std::string& textFontFile, const std::string& iconFontFile);
std::string resolveFontFilePathCached(const std::string& path);
std::string resolveDefaultUiFontPathCached();
std::string resolveDefaultIconFontPathCached();
bool ensurePrivateFontRegisteredCached(const std::string& path);
bool isIconFontFamily(std::string_view family);
std::wstring resolvedFontFamily(const std::string& family);
FontSelection candidateFontSelection(const char* family);
DWRITE_FONT_WEIGHT effectiveFontWeight(std::string_view family, int fontWeight);
void warmDefaultFontCaches();
Microsoft::WRL::ComPtr<IDWriteFontCollection> cachedFontCollectionFromFile(IDWriteFactory* factory,
                                                                            const std::string& fontFilePath);

} // namespace core::render::dx11
