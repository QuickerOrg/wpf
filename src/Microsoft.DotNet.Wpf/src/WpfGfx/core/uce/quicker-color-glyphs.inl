// EMOJI1：只在私有门面中编译；由 drawingcontext.cpp 的宏保护入口包含。
#if QUICKER_WPF_D3D11_FACADE
#include <dwrite_3.h>
#include <d2d1_3.h>
#include <list>
#include <new>
#include <cmath>

class QkColorGlyphCache
{
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
public:
    struct Entry
    {
        std::vector<BYTE> key;
        Ptr<IDWriteFontFace> face; // 防止字体面指针在逐出前被复用。
        Ptr<IWGXBitmap> bitmap;
        D2D1_COLOR_F foreground;
        bool usesForeground;
        float left, top;
        UINT width, height;
    };

    static bool Enabled()
    {
        static const bool enabled = []() {
            WCHAR value[2] = {};
            return GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS", value, 2) == 1 && value[0] == L'1';
        }();
        return enabled;
    }

    ~QkColorGlyphCache() { Trace(); }

    void Fallback(const char *reason, HRESULT hr = S_OK)
    {
        ++m_fallbacks;
        if (m_fallbacks <= 8)
        {
            char line[200];
            StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI1 fallback=%s hr=0x%08lx\n", reason, hr);
            Log(line);
        }
    }

    void DrawTime(LONGLONG start)
    {
        const LONGLONG ticks = QkFacadeClock() - start;
        m_drawTicks += ticks;
        if (m_lastHit) { m_hitTicks += ticks; ++m_hitDraws; }
        if ((++m_draws & 255) == 0) Trace();
    }

    HRESULT Get(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
                float sx, float sy, float phaseX, float phaseY,
                const D2D1_COLOR_F &foreground, Entry **result)
    {
        m_lastHit = false;
        std::vector<BYTE> key;
        Append(key, &run.fontFace, sizeof(run.fontFace));
        Append(key, &run.fontEmSize, sizeof(run.fontEmSize));
        Append(key, &run.bidiLevel, sizeof(run.bidiLevel));
        Append(key, &run.isSideways, sizeof(run.isSideways));
        Append(key, &mode, sizeof(mode));
        const float placement[] = { sx, sy, phaseX, phaseY };
        Append(key, placement, sizeof(placement));
        Append(key, run.glyphIndices, run.glyphCount * sizeof(UINT16));
        Append(key, run.glyphAdvances, run.glyphCount * sizeof(float));
        // WPF 无 offset 时以零规范化，不能以指针或资源标识代替完整定位。
        for (UINT i = 0; i < run.glyphCount; ++i)
        {
            const DWRITE_GLYPH_OFFSET offset = run.glyphOffsets ? run.glyphOffsets[i] : DWRITE_GLYPH_OFFSET{};
            Append(key, &offset, sizeof(offset));
        }
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            if (it->key == key && (!it->usesForeground || memcmp(&it->foreground, &foreground, sizeof(foreground)) == 0))
            {
                m_entries.splice(m_entries.begin(), m_entries, it);
                ++m_hits; m_lastHit = true;
                *result = &m_entries.front();
                return S_OK;
            }
        }

        const LONGLONG start = QkFacadeClock();
        HRESULT hr = Initialize();
        if (FAILED(hr)) { Fallback("initialize", hr); return S_FALSE; }
        const DWRITE_MATRIX matrix = { sx, 0, 0, sy, phaseX, phaseY };
        Ptr<IDWriteColorGlyphRunEnumerator1> layers;
        auto formats = static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE |
            DWRITE_GLYPH_IMAGE_FORMATS_CFF | DWRITE_GLYPH_IMAGE_FORMATS_COLR |
            DWRITE_GLYPH_IMAGE_FORMATS_PNG | DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
            DWRITE_GLYPH_IMAGE_FORMATS_TIFF | DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8 |
            DWRITE_GLYPH_IMAGE_FORMATS_SVG);
        if (m_dw8 && m_dc7 && m_dc7->GetPaintFeatureLevel() >= DWRITE_PAINT_FEATURE_LEVEL_COLR_V1)
        {
            formats = static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(formats | DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE);
            hr = m_dw8->TranslateColorGlyphRun(D2D1::Point2F(), &run, nullptr, formats,
                m_dc7->GetPaintFeatureLevel(), mode, &matrix, 0, &layers);
        }
        else
        {
            hr = m_dw4->TranslateColorGlyphRun(D2D1::Point2F(), &run, nullptr, formats, mode, &matrix, 0, &layers);
        }
        if (hr == DWRITE_E_NOCOLOR) { Fallback("no-color"); return S_FALSE; }
        if (FAILED(hr)) { Fallback("translate", hr); return S_FALSE; }

        Ptr<ID2D1SolidColorBrush> brush;
        hr = m_dc4->CreateSolidColorBrush(foreground, &brush);
        if (FAILED(hr)) return S_FALSE;
        Ptr<ID2D1CommandList> commands;
        hr = m_dc4->CreateCommandList(&commands);
        if (FAILED(hr)) return S_FALSE;
        m_dc4->SetTarget(commands.Get());
        m_dc4->BeginDraw();
        m_dc4->SetTransform(D2D1::Matrix3x2F(sx, 0, 0, sy, phaseX, phaseY));
        m_dc4->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        bool usesForeground = false;
        UINT formatsSeen = 0, foregroundLayers = 0;
        BOOL next = FALSE;
        while (SUCCEEDED(hr = layers->MoveNext(&next)) && next)
        {
            const DWRITE_COLOR_GLYPH_RUN1 *layer = nullptr;
            hr = layers->GetCurrentRun(&layer);
            if (FAILED(hr)) break;
            formatsSeen |= static_cast<UINT>(layer->glyphImageFormat);
            const auto origin = D2D1::Point2F(layer->baselineOriginX, layer->baselineOriginY);
            switch (layer->glyphImageFormat)
            {
            case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
            case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
                usesForeground |= layer->paletteIndex == 0xFFFF;
                if (layer->paletteIndex == 0xFFFF) ++foregroundLayers;
                brush->SetColor(layer->paletteIndex == 0xFFFF ? foreground : layer->runColor);
                m_dc4->DrawGlyphRun(origin, &layer->glyphRun, brush.Get(), layer->measuringMode);
                break;
            case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
            case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
            case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
                m_dc4->DrawColorBitmapGlyphRun(layer->glyphImageFormat, origin, &layer->glyphRun,
                    layer->measuringMode, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DISABLE);
                break;
            case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
                usesForeground = true; // SVG 的 currentColor 可引用前景色。
                brush->SetColor(foreground);
                m_dc4->DrawSvgGlyphRun(origin, &layer->glyphRun, brush.Get(), nullptr, 0, layer->measuringMode);
                break;
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE:
                if (!m_dc7) { hr = E_NOINTERFACE; break; }
                usesForeground = true; // paint tree 可引用前景色；保守地进入键。
                brush->SetColor(foreground);
                m_dc7->DrawPaintGlyphRun(origin, &layer->glyphRun, brush.Get(), 0, layer->measuringMode);
                break;
            default: hr = E_NOTIMPL; break;
            }
            if (FAILED(hr)) break;
        }
        HRESULT drawHr = m_dc4->EndDraw();
        m_dc4->SetTarget(m_target.Get()); // 任意失败均恢复 CPU WIC 目标。
        m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
        if (SUCCEEDED(hr)) hr = drawHr;
        if (SUCCEEDED(hr)) hr = commands->Close();
        if (FAILED(hr)) { Fallback("record-layers", hr); return S_FALSE; }

        // 命令列表包含所有图层和原始定位，D2D 直接给出真实墨迹边界，避免用单色轮廓估算 SVG/paint。
        D2D1_RECT_F bounds;
        hr = m_dc4->GetImageLocalBounds(commands.Get(), &bounds);
        if (FAILED(hr)) { Fallback("ink-bounds", hr); return S_FALSE; }
        const float left = floorf(bounds.left) - 2, top = floorf(bounds.top) - 2;
        const float width = ceilf(bounds.right) + 2 - left, height = ceilf(bounds.bottom) + 2 - top;
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) || !std::isfinite(height) ||
            width <= 0 || height <= 0 || width > 1024 || height > 1024)
        { Fallback("size-limit"); return S_FALSE; }
        const UINT w = static_cast<UINT>(width), h = static_cast<UINT>(height);
        m_dc4->BeginDraw();
        m_dc4->Clear(D2D1::ColorF(0, 0, 0, 0));
        m_dc4->SetTransform(D2D1::Matrix3x2F::Translation(-left, -top));
        m_dc4->DrawImage(commands.Get());
        hr = m_dc4->EndDraw();
        m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(hr)) { Fallback("rasterize", hr); return S_FALSE; }
        std::vector<BYTE> pixels(w * h * 4);
        const WICRect crop = { 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
        hr = m_scratch->CopyPixels(&crop, w * 4, static_cast<UINT>(pixels.size()), pixels.data());
        Ptr<IWICBitmap> bitmap;
        if (SUCCEEDED(hr)) hr = m_wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA,
            w * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap);
        Entry entry = {};
        if (SUCCEEDED(hr)) hr = CWICWrapperBitmap::Create(bitmap.Get(), &entry.bitmap);
        if (FAILED(hr)) { Fallback("wic-wrap", hr); return S_FALSE; }
        entry.key = std::move(key); entry.face = run.fontFace;
        entry.foreground = foreground; entry.usesForeground = usesForeground;
        entry.left = left; entry.top = top; entry.width = w; entry.height = h;
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        while (!m_entries.empty() && (m_entries.size() >= 256 || m_bytes + bytes > 32 * 1024 * 1024))
        {
            m_bytes -= static_cast<size_t>(m_entries.back().width) * m_entries.back().height * 4;
            m_entries.pop_back(); ++m_evictions;
        }
        m_entries.push_front(std::move(entry)); m_bytes += bytes; ++m_misses;
        m_rasterTicks += QkFacadeClock() - start;
        char line[240];
        StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI1 raster glyphs=%u formats=0x%x foregroundLayers=%u foregroundKey=%u size=%ux%u scale=%.3f,%.3f phase=%.5f,%.5f\n",
            run.glyphCount, formatsSeen, foregroundLayers, usesForeground ? 1u : 0u, w, h, sx, sy, phaseX, phaseY);
        Log(line);
        *result = &m_entries.front();
        return S_OK;
    }

private:
    static void Log(const char *line)
    {
        WCHAR path[32768];
        const DWORD count = GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS_LOG", path, ARRAYSIZE(path));
        if (!count || count >= ARRAYSIZE(path)) { QkFacadeTrace(line); return; }
        const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written; WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr); CloseHandle(file);
    }
    static void Append(std::vector<BYTE> &key, const void *data, size_t bytes)
    {
        const BYTE *p = static_cast<const BYTE *>(data);
        key.insert(key.end(), p, p + bytes);
    }
    HRESULT Initialize()
    {
        if (m_initialized) return m_initResult;
        m_initialized = true;
        // 系统库仅在开关打开且实际遇到彩色字体时加载，不增加关闭时的导入依赖。
        const HMODULE d2d = m_d2d.handle = LoadLibraryExW(L"d2d1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const HMODULE dw = m_dw.handle = LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!d2d || !dw) return m_initResult;
        using CreateD2D = HRESULT (WINAPI *)(D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS *, void **);
        using CreateDW = HRESULT (WINAPI *)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);
        const auto createD2D = reinterpret_cast<CreateD2D>(GetProcAddress(d2d, "D2D1CreateFactory"));
        const auto createDW = reinterpret_cast<CreateDW>(GetProcAddress(dw, "DWriteCreateFactory"));
        HRESULT hr = createD2D && createDW ? S_OK : E_NOINTERFACE;
        if (SUCCEEDED(hr)) hr = createD2D(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
            reinterpret_cast<void **>(m_factory.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = createDW(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory4),
            reinterpret_cast<IUnknown **>(m_dw4.GetAddressOf()));
        // 模块成员最后析构，晚于其创建的全部 COM 对象。
        if (SUCCEEDED(hr)) { (void)m_dw4.As(&m_dw8); hr = WICCreateImagingFactory_Proxy(WINCODEC_SDK_VERSION_WPF, &m_wic); }
        if (SUCCEEDED(hr)) hr = m_wic->CreateBitmap(1024, 1024, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &m_scratch);
        Ptr<ID2D1RenderTarget> target;
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (SUCCEEDED(hr)) hr = m_factory->CreateWicBitmapRenderTarget(m_scratch.Get(), props, &target);
        if (SUCCEEDED(hr)) hr = target.As(&m_dc4);
        if (SUCCEEDED(hr)) { (void)m_dc4.As(&m_dc7); m_dc4->GetTarget(&m_target); }
        return m_initResult = hr;
    }
    void Trace()
    {
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        char line[400];
        StringCchPrintfA(line, ARRAYSIZE(line),
            "QK-EMOJI1 cache draws=%llu hits=%llu misses=%llu fallback=%llu evictions=%llu entries=%zu bytes=%zu drawMs=%.6f rasterMs=%.6f paint=%u hitDraws=%llu hitMs=%.6f\n",
            m_draws, m_hits, m_misses, m_fallbacks, m_evictions, m_entries.size(), m_bytes,
            m_drawTicks * 1000.0 / frequency.QuadPart, m_rasterTicks * 1000.0 / frequency.QuadPart,
            m_dc7 ? static_cast<UINT>(m_dc7->GetPaintFeatureLevel()) : 0, m_hitDraws,
            m_hitTicks * 1000.0 / frequency.QuadPart);
        Log(line);
    }
    struct Module { HMODULE handle = nullptr; ~Module() { if (handle) FreeLibrary(handle); } };
    Module m_d2d, m_dw;
    bool m_lastHit = false;
    UINT64 m_hitDraws = 0;
    LONGLONG m_hitTicks = 0;
    bool m_initialized = false;
    HRESULT m_initResult = E_FAIL;
    Ptr<ID2D1Factory> m_factory;
    Ptr<IDWriteFactory4> m_dw4;
    Ptr<IDWriteFactory8> m_dw8;
    Ptr<IWICImagingFactory> m_wic;
    Ptr<IWICBitmap> m_scratch;
    Ptr<ID2D1DeviceContext4> m_dc4;
    Ptr<ID2D1DeviceContext7> m_dc7;
    Ptr<ID2D1Image> m_target;
    std::list<Entry> m_entries;
    size_t m_bytes = 0;
    UINT64 m_hits = 0, m_misses = 0, m_fallbacks = 0, m_evictions = 0, m_draws = 0;
    LONGLONG m_drawTicks = 0, m_rasterTicks = 0;
};

HRESULT CDrawingContext::TryDrawColorGlyphRun(CMilBrushDuce *brush, CGlyphRunResource *resource, bool *drawn)
{
    *drawn = false;
    if (!brush || !brush->IsOfType(TYPE_SOLIDCOLORBRUSH)) return S_OK;

    DWRITE_GLYPH_RUN run = {};
    DWRITE_MEASURING_MODE mode;
    if (FAILED(resource->GetColorGlyphRun(&run, &mode))) return S_OK;
    Microsoft::WRL::ComPtr<IDWriteFontFace> face; face.Attach(run.fontFace);
    Microsoft::WRL::ComPtr<IDWriteFontFace2> colorFace;
    if (FAILED(face.As(&colorFace)) || !colorFace->IsColorFont()) return S_OK;
    if (!run.glyphCount || run.glyphCount > 4096) return S_OK;
    ApplyRenderState();
    const auto &world = m_contextState.WorldToDevice;
    const float sx = world.GetM11(), sy = world.GetM22();
    if (world.GetM12() != 0 || world.GetM21() != 0 || !std::isfinite(sx) || !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return S_OK;
    MilColorF color;
    FLOAT opacity;
    if (FAILED(static_cast<CMilSolidColorBrushDuce *>(brush)->GetColorGlyphBrush(&color, &opacity))) return S_OK;
    opacity *= static_cast<float>(ClampAlpha(color.a));
    color = Convert_MilColorF_scRGB_To_MilColorF_sRGB(&color);
    const D2D1_COLOR_F foreground = { color.r, color.g, color.b, 1.0f };
    const auto origin = resource->GetOrigin();
    const float deviceX = origin.X * sx + world.GetDx(), deviceY = origin.Y * sy + world.GetDy();
    if (!std::isfinite(deviceX) || !std::isfinite(deviceY) || fabsf(deviceX) > 1e7f || fabsf(deviceY) > 1e7f) return S_OK;
    // 1/64 设备像素量化；整数平移不进入缓存键。位图最后仍以整数设备位置绘制。
    const float baseX = floorf(deviceX), baseY = floorf(deviceY);
    const float phaseX = floorf((deviceX - baseX) * 64 + 0.5f) / 64;
    const float phaseY = floorf((deviceY - baseY) * 64 + 0.5f) / 64;
    try
    {
        if (!m_pColorGlyphCache) m_pColorGlyphCache = new QkColorGlyphCache();
        const LONGLONG start = QkFacadeClock();
        QkColorGlyphCache::Entry *entry = nullptr;
        if (m_pColorGlyphCache->Get(run, mode, sx, sy, phaseX, phaseY, foreground, &entry) != S_OK) return S_OK;
        CMatrix<CoordinateSpace::BaseSampling, CoordinateSpace::LocalRendering> textureToLocal(true);
        textureToLocal.SetM11(1 / sx); textureToLocal.SetM22(1 / sy);
        textureToLocal.SetDx((baseX + entry->left - world.GetDx()) / sx);
        textureToLocal.SetDy((baseY + entry->top - world.GetDy()) / sy);
        CMilRectF rect;
        rect.left = textureToLocal.GetDx(); rect.top = textureToLocal.GetDy();
        rect.right = rect.left + entry->width / sx; rect.bottom = rect.top + entry->height / sy;
        const MilRectF source = { 0, 0, static_cast<float>(entry->width), static_cast<float>(entry->height) };
        const auto oldInterpolation = m_renderState.InterpolationMode;
        m_renderState.InterpolationMode = MilBitmapInterpolationMode::NearestNeighbor;
        const HRESULT hr = DrawBitmap(entry->bitmap.Get(), &source, &rect, opacity);
        m_renderState.InterpolationMode = oldInterpolation;
        *drawn = true; // DrawPath 开始后绝不再叠加单色后备。
        m_pColorGlyphCache->DrawTime(start);
        return hr;
    }
    catch (const std::bad_alloc &) { return S_OK; }
}
#endif
