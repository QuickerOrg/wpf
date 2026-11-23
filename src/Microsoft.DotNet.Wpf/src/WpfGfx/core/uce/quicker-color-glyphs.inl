// EMOJI2：只在私有门面中编译；由 drawingcontext.cpp 的宏保护入口包含。
#if QUICKER_WPF_D3D11_FACADE
#include <dwrite_3.h>
#include <d2d1_3.h>
#include <list>
#include <unordered_map>
#include <new>
#include <cmath>

class QkColorGlyphCache
{
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
public:
    struct Entry
    {
        std::vector<BYTE> key;
        UINT64 hash;
        Ptr<IDWriteFontFace> face; // 防止字体面指针在逐出前被复用。
        Ptr<IWGXBitmap> bitmap;
        D2D1_COLOR_F foreground;
        bool usesForeground;
        float left, top;
        UINT width, height;
    };

    struct Prepared
    {
        Ptr<IWGXBitmap> bitmap;
        float x, y, left, top;
        UINT width, height;
        bool hit;
        LONGLONG lookupTicks;
    };

    QkColorGlyphCache()
    {
        m_keyBuffer.reserve(4096 * 14 + 128);
        m_origins.reserve(4096);
        m_prepared.reserve(4096);
    }

    HRESULT PrepareGlyphs(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
        float sx, float sy, float deviceX, float deviceY, const D2D1_COLOR_F &foreground,
        const std::vector<Prepared> **result)
    {
        m_prepared.clear();
        HRESULT hr = Initialize();
        if (FAILED(hr)) { Fallback("initialize", hr); return S_FALSE; }
        m_origins.resize(run.glyphCount);
        const DWRITE_MATRIX matrix = { sx, 0, 0, sy, deviceX, deviceY };
        // 由 DirectWrite 消费完整 advance/offset、bidi、sideways 和 measuring mode；不猜测 ZWJ 的字形结构。
        hr = m_dw4->ComputeGlyphOrigins(&run, mode, D2D1::Point2F(), &matrix, m_origins.data());
        if (FAILED(hr)) { Fallback("glyph-origins", hr); return S_FALSE; }
        const FLOAT zeroAdvance = 0;
        DWRITE_GLYPH_RUN glyph = run;
        glyph.glyphCount = 1; glyph.glyphAdvances = &zeroAdvance; glyph.glyphOffsets = nullptr;
        size_t preparedBytes = 0;
        for (UINT i = 0; i < run.glyphCount; ++i)
        {
            const float x = deviceX + m_origins[i].x * sx, y = deviceY + m_origins[i].y * sy;
            if (!std::isfinite(x) || !std::isfinite(y) || fabsf(x) > 1e7f || fabsf(y) > 1e7f)
            { Fallback("glyph-position"); return S_FALSE; }
            const float snappedX = floorf(x * 4 + 0.5f) / 4;
            const float baseX = floorf(snappedX), baseY = floorf(y + 0.5f);
            glyph.glyphIndices = run.glyphIndices + i;
            Entry *entry = nullptr;
            const LONGLONG start = QkFacadeClock();
            // advance/offset 已折入绝对位置；规范化后的单字形键可跨任意字符串复用。
            hr = Get(glyph, mode, sx, sy, snappedX - baseX, 0, foreground, &entry);
            if (hr != S_OK) return hr;
            const LONGLONG lookupTicks = QkFacadeClock() - start;
            preparedBytes += static_cast<size_t>(entry->width) * entry->height * 4;
            if (preparedBytes > 32 * 1024 * 1024) { Fallback("prepared-size-limit"); return S_FALSE; }
            Prepared tile = { entry->bitmap, baseX, baseY, entry->left, entry->top,
                entry->width, entry->height, m_lastHit, lookupTicks };
            m_prepared.push_back(std::move(tile));
        }
        // 全部成功才绘制；持 bitmap 引用，避免长 run 中 LRU 逐出使前面字形悬空。
        *result = &m_prepared;
        return S_OK;
    }

    void ReleasePrepared() { m_prepared.clear(); }

    void PreparedDrawTime(LONGLONG start, const Prepared &tile)
    {
        m_lastHit = tile.hit;
        DrawTime(start - tile.lookupTicks);
    }

    IDWriteFontFace *ColorFace(CGlyphRunResource *resource)
    {
        IDWriteFont *font = resource->GetColorGlyphFont();
        auto found = m_fonts.find(font);
        if (found != m_fonts.end()) return found->second.color ? found->second.face.Get() : nullptr;
        Font entry; entry.font = font;
        if (FAILED(resource->GetColorGlyphFace(&entry.face))) return nullptr;
        Ptr<IDWriteFontFace2> color;
        entry.color = SUCCEEDED(entry.face.As(&color)) && color->IsColorFont();
        // 字体键和字体面均持引用；清空时没有悬空地址身份可被误命中。
        if (m_fonts.size() >= 64) m_fonts.clear();
        auto inserted = m_fonts.emplace(font, std::move(entry));
        return inserted.first->second.color ? inserted.first->second.face.Get() : nullptr;
    }

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
        auto &key = m_keyBuffer;
        key.clear();
        Append(key, &run.glyphCount, sizeof(run.glyphCount));
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
        // FNV-1a 流式预计算 64 位哈希；相同哈希才比较完整键，碰撞不影响正确性。
        UINT64 hash = 14695981039346656037ull;
        for (BYTE value : key) { hash ^= value; hash *= 1099511628211ull; }
        const auto range = m_index.equal_range(hash);
        for (auto candidate = range.first; candidate != range.second; ++candidate)
        {
            auto it = candidate->second;
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
        const bool monochrome = hr == DWRITE_E_NOCOLOR;
        if (monochrome) hr = S_OK; // 彩色字体中的单色字形也光栅化，保持整条 run 的原始叠加顺序。
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
        if (monochrome)
        {
            usesForeground = true;
            formatsSeen = DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE;
            m_dc4->DrawGlyphRun(D2D1::Point2F(), &run, brush.Get(), mode);
        }
        while (!monochrome && SUCCEEDED(hr = layers->MoveNext(&next)) && next)
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
                usesForeground |= PaintUsesForeground(layer->glyphRun);
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
        // 空白等无墨迹单色字形也缓存为空条目，不让整条混排 run 回退。
        const bool empty = bounds.right <= bounds.left || bounds.bottom <= bounds.top;
        const float left = empty ? 0 : floorf(bounds.left) - 2, top = empty ? 0 : floorf(bounds.top) - 2;
        const float width = empty ? 0 : ceilf(bounds.right) + 2 - left, height = empty ? 0 : ceilf(bounds.bottom) + 2 - top;
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) || !std::isfinite(height) ||
            width < 0 || height < 0 || width > 1024 || height > 1024)
        { Fallback("size-limit"); return S_FALSE; }
        const UINT w = static_cast<UINT>(width), h = static_cast<UINT>(height);
        Entry entry = {};
        if (!empty)
        {
        m_dc4->BeginDraw();
        m_dc4->PushAxisAlignedClip(D2D1::RectF(0, 0, width, height), D2D1_ANTIALIAS_MODE_ALIASED);
        m_dc4->Clear(D2D1::ColorF(0, 0, 0, 0));
        m_dc4->SetTransform(D2D1::Matrix3x2F::Translation(-left, -top));
        m_dc4->DrawImage(commands.Get());
        m_dc4->PopAxisAlignedClip();
        hr = m_dc4->EndDraw();
        m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(hr)) { Fallback("rasterize", hr); return S_FALSE; }
        const WICRect crop = { 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
        Ptr<IWICBitmap> bitmap;
        hr = m_wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
        Ptr<IWICBitmapLock> pixels;
        if (SUCCEEDED(hr)) hr = bitmap->Lock(&crop, WICBitmapLockWrite, &pixels);
        UINT stride = 0, bytesAvailable = 0; BYTE *data = nullptr;
        if (SUCCEEDED(hr)) hr = pixels->GetStride(&stride);
        if (SUCCEEDED(hr)) hr = pixels->GetDataPointer(&bytesAvailable, &data);
        if (SUCCEEDED(hr)) hr = m_scratch->CopyPixels(&crop, stride, bytesAvailable, data);
        pixels.Reset(); // 直接写入最终 WIC 存储，省去中间 vector 和 CreateBitmapFromMemory 的复制。
        if (SUCCEEDED(hr)) hr = CWICWrapperBitmap::Create(bitmap.Get(), &entry.bitmap);
        if (FAILED(hr)) { Fallback("wic-wrap", hr); return S_FALSE; }
        }
        entry.key = key; entry.hash = hash; entry.face = run.fontFace;
        entry.foreground = foreground; entry.usesForeground = usesForeground;
        entry.left = left; entry.top = top; entry.width = w; entry.height = h;
        const size_t bytes = static_cast<size_t>(w) * h * 4;
        while (!m_entries.empty() && (m_entries.size() >= 256 || m_bytes + bytes > 32 * 1024 * 1024))
        {
            m_bytes -= static_cast<size_t>(m_entries.back().width) * m_entries.back().height * 4;
            const auto victim = --m_entries.end();
            const auto victims = m_index.equal_range(victim->hash);
            for (auto it = victims.first; it != victims.second; ++it)
                if (it->second == victim) { m_index.erase(it); break; }
            m_entries.pop_back(); ++m_evictions;
        }
        m_entries.push_front(std::move(entry));
        try { m_index.emplace(hash, m_entries.begin()); }
        catch (...) { m_entries.pop_front(); throw; }
        m_bytes += bytes; ++m_misses;
        m_rasterTicks += QkFacadeClock() - start;
        char line[240];
        StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI1 raster glyphs=%u formats=0x%x foregroundLayers=%u foregroundKey=%u size=%ux%u scale=%.3f,%.3f phase=%.5f,%.5f\n",
            run.glyphCount, formatsSeen, foregroundLayers, usesForeground ? 1u : 0u, w, h, sx, sy, phaseX, phaseY);
        Log(line);
        *result = &m_entries.front();
        return S_OK;
    }

private:
    static bool PaintUsesForeground(const DWRITE_GLYPH_RUN &run)
    {
        Ptr<IDWriteFontFace7> face;
        if (FAILED(run.fontFace->QueryInterface(IID_PPV_ARGS(&face)))) return true;
        Ptr<IDWritePaintReader> reader;
        if (FAILED(face->CreatePaintReader(DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE,
            DWRITE_PAINT_FEATURE_LEVEL_COLR_V1, &reader))) return true;
        for (UINT i = 0; i < run.glyphCount; ++i)
        {
            DWRITE_PAINT_ELEMENT paint = {};
            D2D_RECT_F clip;
            DWRITE_PAINT_ATTRIBUTES attributes = DWRITE_PAINT_ATTRIBUTES_NONE;
            // 官方字形属性覆盖整棵 paint tree，包含嵌套颜色；未知或失败时保守保留颜色键。
            if (FAILED(reader->SetCurrentGlyph(run.glyphIndices[i], &paint, &clip, &attributes)) ||
                paint.paintType == DWRITE_PAINT_TYPE_NONE ||
                (attributes & DWRITE_PAINT_ATTRIBUTES_USES_TEXT_COLOR) != 0) return true;
        }
        return false;
    }

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
    struct Font { Ptr<IDWriteFont> font; Ptr<IDWriteFontFace> face; bool color = false; };
    std::unordered_map<IDWriteFont *, Font> m_fonts;
    std::vector<BYTE> m_keyBuffer;
    std::vector<D2D1_POINT_2F> m_origins;
    std::vector<Prepared> m_prepared;
    std::list<Entry> m_entries;
    std::unordered_multimap<UINT64, std::list<Entry>::iterator> m_index;
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
    // 创建和字体映射可能分配；统一置于 bad_alloc 后备保护内。
    try
    {
        if (!m_pColorGlyphCache) m_pColorGlyphCache = new QkColorGlyphCache();
        IDWriteFontFace *face = m_pColorGlyphCache->ColorFace(resource);
        if (!face) return S_OK;
        resource->GetColorGlyphRun(face, &run, &mode);
        if (!run.glyphCount || run.glyphCount > 4096) return S_OK;
        ApplyRenderState();
        const auto &world = m_contextState.WorldToDevice;
        const float sx = world.GetM11(), sy = world.GetM22();
        const float dx = world.GetDx(), dy = world.GetDy();
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
        const std::vector<QkColorGlyphCache::Prepared> *tiles = nullptr;
        if (m_pColorGlyphCache->PrepareGlyphs(run, mode, sx, sy, deviceX, deviceY, foreground, &tiles) != S_OK)
        { m_pColorGlyphCache->ReleasePrepared(); return S_OK; }
        // 画刷透明度作用于整个 run；重叠字形先 src-over 合成，再统一乘透明度。
        // 世界矩阵的平移已复制，PushOpacity 的临时目标不会改变局部定位计算。
        struct OpacityScope
        {
            CDrawingContext *context;
            bool active = false;
            ~OpacityScope() { if (active) (void)context->Pop(); }
        } scope = { this };
        if (opacity < 1.0f)
        {
            const HRESULT hr = PushOpacity(opacity, nullptr);
            if (FAILED(hr)) { m_pColorGlyphCache->ReleasePrepared(); return hr; }
            scope.active = true;
        }
        *drawn = true; // 开始任何 DrawBitmap 后绝不再叠加整条单色 run。
        for (const auto &tile : *tiles)
        {
            const LONGLONG start = QkFacadeClock();
            if (!tile.bitmap) { m_pColorGlyphCache->PreparedDrawTime(start, tile); continue; }
            CMilRectF rect;
            rect.left = (tile.x + tile.left - dx) / sx;
            rect.top = (tile.y + tile.top - dy) / sy;
            rect.right = rect.left + tile.width / sx; rect.bottom = rect.top + tile.height / sy;
            const MilRectF source = { 0, 0, static_cast<float>(tile.width), static_cast<float>(tile.height) };
            const auto oldInterpolation = m_renderState.InterpolationMode;
            m_renderState.InterpolationMode = MilBitmapInterpolationMode::NearestNeighbor;
            const HRESULT hr = DrawBitmap(tile.bitmap.Get(), &source, &rect, 1.0f);
            m_renderState.InterpolationMode = oldInterpolation;
            m_pColorGlyphCache->PreparedDrawTime(start, tile);
            if (FAILED(hr)) { m_pColorGlyphCache->ReleasePrepared(); return hr; }
        }
        m_pColorGlyphCache->ReleasePrepared();
        if (scope.active)
        {
            scope.active = false;
            return Pop();
        }
        return S_OK;
    }
    catch (const std::bad_alloc &)
    {
        if (m_pColorGlyphCache) m_pColorGlyphCache->ReleasePrepared();
        return S_OK;
    }
}
#endif
