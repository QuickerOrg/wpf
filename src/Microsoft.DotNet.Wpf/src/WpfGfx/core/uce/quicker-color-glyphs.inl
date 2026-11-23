// EMOJI3：只在私有门面中编译；由 drawingcontext.cpp 的宏保护入口包含。
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
    // EMOJI4：新增的 QPC 仅在日志开关下运行；无日志时不查询时钟。
    static bool TimingEnabled()
    {
        static const bool enabled = []() {
            WCHAR path[32768];
            const DWORD n = GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS_LOG", path, ARRAYSIZE(path));
            return n > 0 && n < ARRAYSIZE(path);
        }();
        return enabled;
    }
    static LONGLONG StepClock() { return TimingEnabled() ? QkFacadeClock() : 0; }
    enum Step { Init, Face, Translate, Paint, Record, Bounds, Raster, Copy, Insert, RasterDraw, RasterShared, StepCount };
    struct Steps
    {
        LONGLONG ticks[StepCount] = {};
        void Add(Step step, LONGLONG start) { if (TimingEnabled()) ticks[step] += StepClock() - start; }
    };
    struct Entry
    {
        std::vector<BYTE> key;
        UINT64 hash;
        std::vector<BYTE> identity;
        float sx, sy, phaseX, phaseY;
        Ptr<IDWriteFontFace> face; // 防止字体面指针在逐出前被复用。
        Ptr<IWGXBitmap> bitmap;
        D2D1_COLOR_F foreground;
        bool usesForeground;
        bool prewarmed = false, color = false;
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
        float scaleX = 1, scaleY = 1;
        bool interpolated = false, color = false;
    };

    // 仅保存平凡静态状态；无静态 COM 析构，也不从 DllMain 清理。
    struct ProcessState
    {
        SRWLOCK lock = SRWLOCK_INIT;
        QkColorGlyphCache *cache = nullptr;
        UINT contexts = 0;
        UINT64 hits = 0, misses = 0, fallbacks = 0, evictions = 0, draws = 0, hitDraws = 0;
        LONGLONG drawTicks = 0, rasterTicks = 0, hitTicks = 0;
    };
    static ProcessState &Process() { static ProcessState state; return state; }
    class Guard
    {
    public:
        explicit Guard(bool acquire = true) : held(acquire) { if (held) AcquireSRWLockExclusive(&Process().lock); }
        ~Guard() { if (held) ReleaseSRWLockExclusive(&Process().lock); }
        bool held;
        Guard(const Guard &) = delete;
        Guard &operator=(const Guard &) = delete;
    };
    static QkColorGlyphCache *Acquire()
    {
        Guard guard;
        auto &state = Process();
        if (!state.cache) state.cache = new QkColorGlyphCache();
        ++state.contexts;
        return state.cache;
    }
    static void Release(QkColorGlyphCache *cache)
    {
        if (!cache) return;
        bool last = false;
        {
            Guard guard;
            auto &state = Process();
            Assert(cache == state.cache && state.contexts > 0);
            last = --state.contexts == 0;
            cache->Trace();
            if (last)
            {
                state.cache = nullptr; // 下一代可以创建；旧后台仅能发布到仍是当前代的缓存。
                InterlockedExchange(&cache->m_stop, 1);
                if (cache->m_wake) SetEvent(cache->m_wake);
            }
        }
        if (last)
        {
            const ULONGLONG start = GetTickCount64();
            const DWORD result = cache->m_threads.empty() ? WAIT_OBJECT_0 :
                WaitForMultipleObjects(static_cast<DWORD>(cache->m_threads.size()), cache->m_threads.data(), TRUE, 2000);
            char line[180];
            StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI6-STOP waitMs=%llu result=%lu deferred=%u\n",
                GetTickCount64() - start, result, result == WAIT_OBJECT_0 ? 0u : 1u);
            Log(line);
            // 超时不能强杀系统光栅化或释放仍被使用的对象。工作引用+模块引用负责延迟安全收尾。
            cache->DropReference();
        }
    }

    explicit QkColorGlyphCache(bool worker = false) : m_worker(worker)
    {
        m_threads.reserve(4);
        m_keyBuffer.reserve(128);
        m_origins.reserve(4096);
    }
    static float XPhases()
    {
        static const float phases = []() {
            WCHAR value[3] = {};
            return GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS_XPHASE", value, 3) == 1 && value[0] == L'4' ? 4.0f : 1.0f;
        }();
        return phases;
    }
    static bool PrewarmEnabled()
    {
        static const bool enabled = []() {
            WCHAR value[3] = {};
            return !(GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS_PREWARM", value, 3) == 1 && value[0] == L'0');
        }();
        return enabled;
    }

    HRESULT PrepareGlyphs(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
        float sx, float sy, float deviceX, float deviceY, const D2D1_COLOR_F &foreground,
        std::vector<Prepared> &prepared, UINT64 sourceId, bool allowAnimation = true)
    {
        if (!m_worker && PrewarmEnabled()) InterlockedExchange64(&m_lastForeground, static_cast<LONG64>(GetTickCount64()));
        Guard guard(!m_worker);
        prepared.reserve(run.glyphCount);
        const bool initializing = !m_initialized;
        const LONGLONG initStart = StepClock();
        HRESULT hr = Initialize();
        if (initializing && TimingEnabled()) m_pendingInit += StepClock() - initStart;
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
        std::vector<Work> pending;
        std::unordered_multimap<UINT64, size_t> pendingIndex;
        for (UINT i = 0; i < run.glyphCount; ++i)
        {
            const float x = deviceX + m_origins[i].x * sx, y = deviceY + m_origins[i].y * sy;
            if (!std::isfinite(x) || !std::isfinite(y) || fabsf(x) > 1e7f || fabsf(y) > 1e7f)
            { Fallback("glyph-position"); return S_FALSE; }
            const float snappedX = floorf(x * XPhases() + 0.5f) / XPhases();
            if (!m_worker) m_maxXShift = max(m_maxXShift, fabsf(snappedX - floorf(x * 4 + 0.5f) / 4));
            const float baseX = floorf(snappedX), baseY = floorf(y + 0.5f);
            glyph.glyphIndices = run.glyphIndices + i;
            Entry *entry = nullptr;
            const LONGLONG start = QkFacadeClock();
            // advance/offset 已折入绝对位置；规范化后的单字形键可跨任意字符串复用。
            hr = Get(glyph, mode, sx, sy, snappedX - baseX, 0, foreground, &entry, pending, pendingIndex, prepared.size(), sourceId, allowAnimation);
            if (hr != S_OK) return hr;
            const LONGLONG lookupTicks = QkFacadeClock() - start;
            const Entry &dimensions = entry ? *entry : pending[m_pendingItem].entry;
            preparedBytes += static_cast<size_t>(dimensions.width) * dimensions.height * 4;
            if (preparedBytes > 32 * 1024 * 1024) { Fallback("prepared-size-limit"); return S_FALSE; }
            Prepared tile = { entry ? entry->bitmap : Ptr<IWGXBitmap>(), baseX, baseY, dimensions.left, dimensions.top,
                dimensions.width, dimensions.height, m_lastHit, lookupTicks };
            tile.color = dimensions.color;
            if (m_lastAnimated)
            {
                tile.scaleX = sx / entry->sx; tile.scaleY = sy / entry->sy;
                tile.left = (entry->left - entry->phaseX) * tile.scaleX + snappedX - baseX;
                tile.top = (entry->top - entry->phaseY) * tile.scaleY;
                tile.interpolated = true;
            }
            prepared.push_back(std::move(tile));
        }
        // 批次完成前不向 WPF 绘制；发布后各槽持有独立引用，逐出不影响锁外绘制。
        return Flush(pending, prepared);
    }

    void PreparedDrawTime(LONGLONG start, const Prepared &tile)
    {
        const LONGLONG ticks = QkFacadeClock() - start + tile.lookupTicks;
        Guard guard;
        Stats().drawTicks += ticks;
        if (tile.hit) { Stats().hitTicks += ticks; ++Stats().hitDraws; }
        if ((++Stats().draws & 255) == 0) Trace();
    }

    Ptr<IDWriteFontFace> ColorFace(CGlyphRunResource *resource)
    {
        Guard guard;
        IDWriteFont *font = resource->GetColorGlyphFont();
        auto found = m_fonts.find(font);
        if (found != m_fonts.end()) return found->second.color ? found->second.face : Ptr<IDWriteFontFace>();
        const LONGLONG faceStart = StepClock();
        Font entry; entry.font = font;
        if (FAILED(resource->GetColorGlyphFace(&entry.face))) return nullptr;
        Ptr<IDWriteFontFace2> color;
        entry.color = SUCCEEDED(entry.face.As(&color)) && color->IsColorFont();
        // 字体键和字体面均持引用；清空时没有悬空地址身份可被误命中。
        if (m_fonts.size() >= 64) m_fonts.clear();
        auto inserted = m_fonts.emplace(font, std::move(entry));
        if (inserted.first->second.color && TimingEnabled()) m_pendingFace += StepClock() - faceStart;
        return inserted.first->second.color ? inserted.first->second.face : Ptr<IDWriteFontFace>();
    }

    static bool Enabled()
    {
        static const bool enabled = []() {
            WCHAR value[2] = {};
            return GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS", value, 2) == 1 && value[0] == L'1';
        }();
        return enabled;
    }

    ~QkColorGlyphCache()
    {
        for (HANDLE thread : m_threads) CloseHandle(thread);
        if (m_wake) CloseHandle(m_wake);
    }

    void Fallback(const char *reason, HRESULT hr = S_OK)
    {
        if (m_worker) return;
        ++Stats().fallbacks;
        if (Stats().fallbacks <= 8)
        {
            char line[200];
            StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI1 fallback=%s hr=0x%08lx\n", reason, hr);
            Log(line);
        }
    }

private:
    struct Layer
    {
        Ptr<IDWriteFontFace> face;
        DWRITE_GLYPH_RUN run;
        std::vector<UINT16> indices;
        std::vector<FLOAT> advances;
        std::vector<DWRITE_GLYPH_OFFSET> offsets;
        D2D1_POINT_2F origin;
        DWRITE_MEASURING_MODE mode;
        DWRITE_GLYPH_IMAGE_FORMATS format;
        D2D1_COLOR_F color;
        UINT16 palette;
        Layer(const DWRITE_GLYPH_RUN &value, D2D1_POINT_2F point, DWRITE_MEASURING_MODE measuring,
            DWRITE_GLYPH_IMAGE_FORMATS imageFormat, D2D1_COLOR_F ink, UINT16 paletteIndex)
            : face(value.fontFace), run(value), indices(value.glyphIndices, value.glyphIndices + value.glyphCount),
              origin(point), mode(measuring), format(imageFormat), color(ink), palette(paletteIndex)
        {
            if (value.glyphAdvances) advances.assign(value.glyphAdvances, value.glyphAdvances + value.glyphCount);
            if (value.glyphOffsets) offsets.assign(value.glyphOffsets, value.glyphOffsets + value.glyphCount);
        }
        DWRITE_GLYPH_RUN Run() const
        {
            auto value = run; value.fontFace = face.Get(); value.glyphIndices = indices.data();
            value.glyphAdvances = advances.empty() ? nullptr : advances.data();
            value.glyphOffsets = offsets.empty() ? nullptr : offsets.data();
            return value;
        }
    };
    struct Work
    {
        Entry entry;
        std::vector<Layer> layers;
        Ptr<ID2D1CommandList> commands;
        Ptr<ID2D1SolidColorBrush> brush;
        std::vector<size_t> slots;
        Steps steps;
        UINT16 glyph;
        float em, sx, sy, phaseX, phaseY;
        UINT formats, foregroundLayers;
        UINT x = 0, y = 0;
    };
    UINT64 MakeKey(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
        float sx, float sy, float phaseX, float phaseY, size_t &placementOffset)
    {
        auto &key = m_keyBuffer;
        key.clear();
        Append(key, &run.glyphCount, sizeof(run.glyphCount));
        Append(key, &run.fontFace, sizeof(run.fontFace));
        Append(key, &run.fontEmSize, sizeof(run.fontEmSize));
        Append(key, &run.bidiLevel, sizeof(run.bidiLevel));
        Append(key, &run.isSideways, sizeof(run.isSideways));
        Append(key, &mode, sizeof(mode));
        placementOffset = key.size();
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
        return hash;
    }
    HRESULT Get(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
                float sx, float sy, float phaseX, float phaseY,
                const D2D1_COLOR_F &foreground, Entry **result, std::vector<Work> &pending,
                std::unordered_multimap<UINT64, size_t> &pendingIndex, size_t slot, UINT64 sourceId, bool allowAnimation)
    {
        m_lastHit = false; m_lastAnimated = false;
        size_t placementOffset = 0;
        const UINT64 hash = MakeKey(run, mode, sx, sy, phaseX, phaseY, placementOffset);
        auto &key = m_keyBuffer;
        const auto range = m_index.equal_range(hash);
        for (auto candidate = range.first; candidate != range.second; ++candidate)
        {
            auto it = candidate->second;
            if (it->key == key && (!it->usesForeground || memcmp(&it->foreground, &foreground, sizeof(foreground)) == 0))
            {
                if (it->prewarmed)
                {
                    --m_warmEntries; m_warmBytes -= static_cast<size_t>(it->width) * it->height * 4;
                    it->prewarmed = false;
                }
                m_entries.splice(m_entries.begin(), m_entries, it);
                ++Stats().hits; m_lastHit = true;
                *result = &m_entries.front();
                return S_OK;
            }
        }

        // 动画身份省略设备缩放/像素相位；位图仍保留原始精确键。资源序号仅隔离动画检测，
        // 不限制不同文本/窗口之间共享位图，也不在共享缓存中持有 WPF 资源指针。
        auto identity = key;
        memset(identity.data() + placementOffset, 0, 4 * sizeof(float));
        if (allowAnimation)
        {
            Entry *closest = AnimationBitmap(identity, sourceId, sx, sy, foreground);
            if (closest)
            {
                ++Stats().hits; m_lastHit = true; m_lastAnimated = true;
                *result = closest;
                return S_OK;
            }
        }
        const auto waiting = pendingIndex.equal_range(hash);
        for (auto it = waiting.first; it != waiting.second; ++it)
        {
            auto &item = pending[it->second];
            if (item.entry.key == key && (!item.entry.usesForeground || memcmp(&item.entry.foreground, &foreground, sizeof(foreground)) == 0))
            {
                item.slots.push_back(slot); m_pendingItem = it->second;
                ++Stats().hits; m_lastHit = true; *result = nullptr;
                return S_OK;
            }
        }
        const LONGLONG start = QkFacadeClock();
        Steps steps;
        steps.ticks[Init] = m_pendingInit; m_pendingInit = 0;
        steps.ticks[Face] = m_pendingFace; m_pendingFace = 0;
        HRESULT hr = Initialize();
        LONGLONG stepStart = StepClock();
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
        steps.Add(Translate, stepStart);
        const bool monochrome = hr == DWRITE_E_NOCOLOR;
        if (monochrome) hr = S_OK; // 彩色字体中的单色字形也光栅化，保持整条 run 的原始叠加顺序。
        if (FAILED(hr)) { Fallback("translate", hr); return S_FALSE; }

        stepStart = StepClock();
        std::vector<Layer> drawLayers;
        bool usesForeground = monochrome;
        UINT formatsSeen = monochrome ? DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE : 0, foregroundLayers = 0;
        if (monochrome) drawLayers.emplace_back(run, D2D1::Point2F(), mode,
            DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE, foreground, 0xFFFF);
        BOOL next = FALSE;
        while (!monochrome && SUCCEEDED(hr = layers->MoveNext(&next)) && next)
        {
            const DWRITE_COLOR_GLYPH_RUN1 *layer = nullptr;
            hr = layers->GetCurrentRun(&layer);
            if (FAILED(hr)) break;
            drawLayers.emplace_back(layer->glyphRun, D2D1::Point2F(layer->baselineOriginX, layer->baselineOriginY),
                layer->measuringMode, layer->glyphImageFormat, layer->runColor, layer->paletteIndex);
            formatsSeen |= static_cast<UINT>(layer->glyphImageFormat);
        }
        if (FAILED(hr)) { Fallback("enumerate-layers", hr); return S_FALSE; }
        D2D1_RECT_F bounds = { FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool reliableBounds = true;
        const auto transform = D2D1::Matrix3x2F(sx, 0, 0, sy, phaseX, phaseY);
        for (auto &layer : drawLayers)
        {
            D2D1_RECT_F box = {};
            const auto layerRun = layer.Run();
            if (layer.format == DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE)
            {
                const LONGLONG paintStart = StepClock();
                PaintInfo info = ReadPaint(layerRun);
                steps.Add(Paint, paintStart);
                usesForeground |= info.foreground;
                reliableBounds &= info.hasClip && !layerRun.isSideways && !(layerRun.bidiLevel & 1);
                // DirectWrite 的 ClipBox 为 em 单位、基线坐标且 Y 向下；其外像素按字体规范必须剪裁。
                box = D2D1::RectF((layer.origin.x + info.clip.left * layerRun.fontEmSize) * sx + phaseX,
                    (layer.origin.y + info.clip.top * layerRun.fontEmSize) * sy + phaseY,
                    (layer.origin.x + info.clip.right * layerRun.fontEmSize) * sx + phaseX,
                    (layer.origin.y + info.clip.bottom * layerRun.fontEmSize) * sy + phaseY);
            }
            else if (layer.format == DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE || layer.format == DWRITE_GLYPH_IMAGE_FORMATS_CFF ||
                     layer.format == DWRITE_GLYPH_IMAGE_FORMATS_COLR)
            {
                usesForeground |= layer.palette == 0xFFFF;
                if (layer.palette == 0xFFFF) ++foregroundLayers;
                // COLRv0 的每个图层均为真实轮廓；用 DWrite 原始定位展开轮廓，再合并几何黑框。
                Ptr<ID2D1PathGeometry> geometry;
                Ptr<ID2D1GeometrySink> sink;
                hr = m_factory->CreatePathGeometry(&geometry);
                if (SUCCEEDED(hr)) hr = geometry->Open(&sink);
                if (SUCCEEDED(hr)) hr = layerRun.fontFace->GetGlyphRunOutline(layerRun.fontEmSize, layerRun.glyphIndices,
                    layerRun.glyphAdvances, layerRun.glyphOffsets, layerRun.glyphCount, layerRun.isSideways,
                    (layerRun.bidiLevel & 1) != 0, sink.Get());
                if (SUCCEEDED(hr)) hr = sink->Close();
                const auto placement = D2D1::Matrix3x2F::Translation(layer.origin.x, layer.origin.y) * transform;
                if (SUCCEEDED(hr)) hr = geometry->GetBounds(&placement, &box);
                reliableBounds &= SUCCEEDED(hr);
            }
            else
            {
                reliableBounds = false; // 位图/SVG 保持命令列表紧边界；未知格式由 DrawLayers 后备。
                usesForeground |= layer.format == DWRITE_GLYPH_IMAGE_FORMATS_SVG;
            }
            bounds.left = min(bounds.left, box.left); bounds.top = min(bounds.top, box.top);
            bounds.right = max(bounds.right, box.right); bounds.bottom = max(bounds.bottom, box.bottom);
        }
        steps.Add(Bounds, stepStart);
        steps.ticks[Bounds] -= steps.ticks[Paint];
        Ptr<ID2D1SolidColorBrush> brush;
        hr = m_dc4->CreateSolidColorBrush(foreground, &brush);
        if (FAILED(hr)) return S_FALSE;
        Ptr<ID2D1CommandList> commands;
        if (!reliableBounds)
        {
            stepStart = StepClock();
            hr = m_dc4->CreateCommandList(&commands);
            if (FAILED(hr)) return S_FALSE;
            m_dc4->SetTarget(commands.Get());
            m_dc4->BeginDraw(); m_dc4->SetTransform(transform);
            m_dc4->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            hr = DrawLayers(drawLayers, brush.Get(), foreground);
            const HRESULT drawHr = m_dc4->EndDraw();
            m_dc4->SetTarget(m_target.Get()); m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
            if (SUCCEEDED(hr)) hr = drawHr;
            if (SUCCEEDED(hr)) hr = commands->Close();
            steps.Add(Record, stepStart);
            if (FAILED(hr)) { Fallback("record-layers", hr); return S_FALSE; }
            stepStart = StepClock();
            hr = m_dc4->GetImageLocalBounds(commands.Get(), &bounds);
            steps.Add(Bounds, stepStart);
            if (FAILED(hr)) { Fallback("ink-bounds", hr); return S_FALSE; }
        }
        // 空白等无墨迹单色字形也缓存为空条目，不让整条混排 run 回退。
        const bool empty = bounds.right <= bounds.left || bounds.bottom <= bounds.top;
        const float left = empty ? 0 : floorf(bounds.left) - 2, top = empty ? 0 : floorf(bounds.top) - 2;
        const float width = empty ? 0 : ceilf(bounds.right) + 2 - left, height = empty ? 0 : ceilf(bounds.bottom) + 2 - top;
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(width) || !std::isfinite(height) ||
            width < 0 || height < 0 || width > 1024 || height > 1024)
        { Fallback("size-limit"); return S_FALSE; }
        const UINT w = static_cast<UINT>(width), h = static_cast<UINT>(height);
        Work item = {};
        item.entry.key = key; item.entry.hash = hash; item.entry.face = run.fontFace;
        item.entry.identity = std::move(identity); item.entry.sx = sx; item.entry.sy = sy;
        item.entry.phaseX = phaseX; item.entry.phaseY = phaseY;
        item.entry.foreground = foreground; item.entry.usesForeground = usesForeground;
        item.entry.color = (formatsSeen & ~(DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE | DWRITE_GLYPH_IMAGE_FORMATS_CFF)) != 0;
        item.entry.left = left; item.entry.top = top; item.entry.width = w; item.entry.height = h;
        item.layers = std::move(drawLayers); item.commands = std::move(commands); item.brush = std::move(brush);
        item.glyph = run.glyphIndices[0]; item.em = run.fontEmSize;
        item.sx = sx; item.sy = sy; item.phaseX = phaseX; item.phaseY = phaseY;
        item.formats = formatsSeen; item.foregroundLayers = foregroundLayers;
        item.steps = steps; item.slots.push_back(slot);
        m_pendingItem = pending.size();
        pending.push_back(std::move(item));
        pendingIndex.emplace(hash, m_pendingItem);
        Stats().rasterTicks += QkFacadeClock() - start;
        *result = nullptr;
        return S_OK;
    }

    struct Animation
    {
        std::vector<BYTE> identity;
        Ptr<IDWriteFontFace> face;
        D2D1_COLOR_F foreground;
        UINT64 sourceId;
        float sx, sy;
        ULONGLONG changedAt;
        UINT changes = 1;
        bool active = false;
    };
    Entry *AnimationBitmap(const std::vector<BYTE> &identity, UINT64 sourceId, float sx, float sy,
        const D2D1_COLOR_F &foreground)
    {
        const ULONGLONG now = GetTickCount64();
        auto state = m_animations.begin();
        for (; state != m_animations.end(); ++state)
            if (state->sourceId == sourceId && state->identity == identity &&
                memcmp(&state->foreground, &foreground, sizeof(foreground)) == 0) break;
        if (state == m_animations.end())
        {
            if (m_animations.size() >= 256) m_animations.pop_back();
            Animation value = {}; value.identity = identity; value.sourceId = sourceId;
            // identity 已包含持引用的字体面；从当前键的字体指针取出并保活，防止地址重用。
            IDWriteFontFace *fontFace = nullptr;
            memcpy(&fontFace, identity.data() + sizeof(UINT), sizeof(fontFace));
            value.face = fontFace; value.foreground = foreground;
            value.sx = sx; value.sy = sy; value.changedAt = now;
            m_animations.push_front(std::move(value));
            return nullptr;
        }
        if (sx != state->sx || sy != state->sy)
        {
            state->changes = now - state->changedAt <= 250 ? min(state->changes + 1, 2u) : 1;
            state->active = state->changes >= 2;
            state->sx = sx; state->sy = sy; state->changedAt = now;
        }
        else if (now - state->changedAt >= 150)
        {
            state->active = false; state->changes = 1;
        }
        const bool active = state->active;
        m_animations.splice(m_animations.begin(), m_animations, state);
        if (!active) return nullptr;
        auto closest = m_entries.end();
        float best = FLT_MAX;
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            if (it->identity != identity || !it->bitmap ||
                (it->usesForeground && memcmp(&it->foreground, &foreground, sizeof(foreground)) != 0)) continue;
            // 复用也必须遵守精确生成的画布限制，避免极端缩放在目标矩形计算中溢出。
            const float rx = sx / it->sx, ry = sy / it->sy;
            if (!std::isfinite(rx) || !std::isfinite(ry) || rx <= 0 || ry <= 0 ||
                it->width * rx > 1024 || it->height * ry > 1024 ||
                !std::isfinite((it->left - it->phaseX) * rx) || !std::isfinite((it->top - it->phaseY) * ry)) continue;
            const float score = fabsf(logf(it->sx / sx)) + fabsf(logf(it->sy / sy)) +
                (it->sx < sx || it->sy < sy ? 0.05f : 0.0f);
            if (score < best) { best = score; closest = it; }
        }
        if (closest == m_entries.end()) return nullptr;
        if (closest->prewarmed)
        {
            --m_warmEntries; m_warmBytes -= static_cast<size_t>(closest->width) * closest->height * 4;
            closest->prewarmed = false;
        }
        m_entries.splice(m_entries.begin(), m_entries, closest);
        if (TimingEnabled())
        {
            char line[240];
            StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI4-ANIMATION source=%llu target=%.6f,%.6f bitmap=%.6f,%.6f stableMs=%llu\n",
                sourceId, sx, sy, m_entries.front().sx, m_entries.front().sy, now - m_animations.front().changedAt);
            Log(line);
        }
        return &m_entries.front();
    }

    HRESULT Flush(std::vector<Work> &pending, std::vector<Prepared> &prepared)
    {
        size_t first = 0;
        while (first < pending.size())
        {
            // 单个 DrawGlyphRun 的新字形按行打包；超出 1024 方形 scratch 时顺序提交多页。
            UINT x = 0, y = 0, rowHeight = 0;
            size_t last = first;
            for (; last < pending.size(); ++last)
            {
                auto &item = pending[last];
                if (x + item.entry.width > 1024) { x = 0; y += rowHeight; rowHeight = 0; }
                if (y + item.entry.height > 1024) break;
                item.x = x; item.y = y;
                x += item.entry.width; rowHeight = max(rowHeight, item.entry.height);
            }
            const LONGLONG rasterStart = QkFacadeClock();
            const LONGLONG beginStart = StepClock();
            m_dc4->BeginDraw();
            m_dc4->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            const LONGLONG beginTicks = StepClock() - beginStart;
            HRESULT hr = S_OK;
            for (size_t i = first; i < last; ++i)
            {
                auto &item = pending[i];
                if (!item.entry.width || !item.entry.height) continue;
                const LONGLONG drawStart = StepClock();
                m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
                m_dc4->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(item.x), static_cast<float>(item.y),
                    static_cast<float>(item.x + item.entry.width), static_cast<float>(item.y + item.entry.height)), D2D1_ANTIALIAS_MODE_ALIASED);
                m_dc4->Clear(D2D1::ColorF(0, 0, 0, 0));
                const auto shift = D2D1::Matrix3x2F::Translation(item.x - item.entry.left, item.y - item.entry.top);
                if (item.commands)
                {
                    m_dc4->SetTransform(shift); m_dc4->DrawImage(item.commands.Get());
                }
                else
                {
                    m_dc4->SetTransform(D2D1::Matrix3x2F(item.sx, 0, 0, item.sy, item.phaseX, item.phaseY) * shift);
                    hr = DrawLayers(item.layers, item.brush.Get(), item.entry.foreground);
                }
                m_dc4->PopAxisAlignedClip(); item.steps.Add(Raster, drawStart);
                if (FAILED(hr)) break;
            }
            const LONGLONG endStart = StepClock();
            const HRESULT endHr = m_dc4->EndDraw();
            const LONGLONG endTicks = StepClock() - endStart;
            m_dc4->SetTransform(D2D1::Matrix3x2F::Identity());
            Stats().rasterTicks += QkFacadeClock() - rasterStart;
            if (SUCCEEDED(hr)) hr = endHr;
            if (FAILED(hr)) { Fallback("batch-rasterize", hr); return S_FALSE; }
            if (!m_worker && TimingEnabled())
            {
                LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
                char line[200];
                StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI4-BATCH {\"count\":%zu,\"beginMs\":%.6f,\"endMs\":%.6f}\n",
                    last - first, beginTicks * 1000.0 / frequency.QuadPart, endTicks * 1000.0 / frequency.QuadPart);
                Log(line);
            }
            // Begin/End 的共同成本等额归到本批每个字形；原始共同成本另记批次行，避免重复相加。
            const LONGLONG shared = beginTicks + endTicks;
            for (size_t i = first; i < last; ++i)
            {
                auto &item = pending[i];
                item.steps.ticks[RasterDraw] = item.steps.ticks[Raster];
                item.steps.ticks[RasterShared] = shared / static_cast<LONGLONG>(last - first) +
                    (i == first ? shared % static_cast<LONGLONG>(last - first) : 0);
                item.steps.ticks[Raster] += item.steps.ticks[RasterShared];
                const LONGLONG copyStart = StepClock(), publishStart = QkFacadeClock();
                const UINT w = item.entry.width, h = item.entry.height;
                if (w && h)
                {
                    const WICRect output = { 0, 0, static_cast<INT>(w), static_cast<INT>(h) };
                    const WICRect crop = { static_cast<INT>(item.x), static_cast<INT>(item.y), static_cast<INT>(w), static_cast<INT>(h) };
                    Ptr<IWICBitmap> bitmap;
                    hr = m_wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
                    Ptr<IWICBitmapLock> pixels;
                    if (SUCCEEDED(hr)) hr = bitmap->Lock(&output, WICBitmapLockWrite, &pixels);
                    UINT stride = 0, bytesAvailable = 0; BYTE *data = nullptr;
                    if (SUCCEEDED(hr)) hr = pixels->GetStride(&stride);
                    if (SUCCEEDED(hr)) hr = pixels->GetDataPointer(&bytesAvailable, &data);
                    if (SUCCEEDED(hr)) hr = m_scratch->CopyPixels(&crop, stride, bytesAvailable, data);
                    pixels.Reset();
                    if (SUCCEEDED(hr)) hr = CWICWrapperBitmap::Create(bitmap.Get(), &item.entry.bitmap);
                    if (FAILED(hr)) { Fallback("wic-wrap", hr); return S_FALSE; }
                }
                item.steps.Add(Copy, copyStart);
                const LONGLONG insertStart = StepClock();
                const size_t bytes = static_cast<size_t>(w) * h * 4;
                while (!m_entries.empty() && (m_entries.size() >= 256 || m_bytes + bytes > 32 * 1024 * 1024))
                {
                    m_bytes -= static_cast<size_t>(m_entries.back().width) * m_entries.back().height * 4;
                    const auto victim = --m_entries.end();
                    const auto victims = m_index.equal_range(victim->hash);
                    for (auto it = victims.first; it != victims.second; ++it)
                        if (it->second == victim) { m_index.erase(it); break; }
                    if (victim->prewarmed) { --m_warmEntries; m_warmBytes -= static_cast<size_t>(victim->width) * victim->height * 4; }
                    m_entries.pop_back(); ++Stats().evictions;
                }
                m_entries.push_front(std::move(item.entry));
                try { m_index.emplace(m_entries.front().hash, m_entries.begin()); }
                catch (...) { m_entries.pop_front(); throw; }
                m_bytes += bytes; ++Stats().misses;
                const auto &entry = m_entries.front();
                for (size_t slot : item.slots) prepared[slot].bitmap = entry.bitmap;
                item.steps.Add(Insert, insertStart);
                Stats().rasterTicks += QkFacadeClock() - publishStart;
                DWRITE_GLYPH_RUN run = {}; run.glyphIndices = &item.glyph; run.fontFace = entry.face.Get(); run.fontEmSize = item.em;
                if (!m_worker) TraceSteps(run, item.sx, item.sy, item.phaseX, item.formats, w, h, item.steps);
                char line[240];
                StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI1 raster glyphs=1 formats=0x%x foregroundLayers=%u foregroundKey=%u size=%ux%u scale=%.3f,%.3f phase=%.5f,%.5f\n",
                    item.formats, item.foregroundLayers, entry.usesForeground ? 1u : 0u, w, h, item.sx, item.sy, item.phaseX, item.phaseY);
                if (!m_worker) Log(line);
            }
            first = last;
        }
        return S_OK;
    }

public:
    void RegisterTarget(const DWRITE_GLYPH_RUN &run, DWRITE_MEASURING_MODE mode,
        float sx, float sy, const D2D1_COLOR_F &foreground)
    {
        if (!PrewarmEnabled()) return;
        Guard guard;
        for (const auto &target : m_targets)
            if (target.face.Get() == run.fontFace && target.em == run.fontEmSize && target.sx == sx && target.sy == sy &&
                target.mode == mode && target.bidi == run.bidiLevel && target.sideways == run.isSideways &&
                memcmp(&target.foreground, &foreground, sizeof(foreground)) == 0) return;
        if (m_targets.size() >= 4) return;
        // 前景色保守区分目标；发布时仍由实际 usesForeground 决定是否忽略颜色键。
        WarmTarget target; target.face = run.fontFace; target.em = run.fontEmSize;
        target.sx = sx; target.sy = sy; target.mode = mode; target.bidi = run.bidiLevel;
        target.sideways = run.isSideways; target.foreground = foreground;
        m_targets.push_back(std::move(target));
        if (!m_running)
        {
            if (!m_wake) m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!m_wake || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&WorkerEntry), &m_workerModule)) return;
            InterlockedIncrement(&m_lifetime);
            const HANDLE thread = CreateThread(nullptr, 0, WorkerEntry, this, 0, nullptr);
            if (!thread)
            {
                InterlockedDecrement(&m_lifetime); FreeLibrary(m_workerModule); m_workerModule = nullptr;
                return;
            }
            m_threads.push_back(thread); // 构造时已预留4个句柄；最多4个尺寸目标，不会在启动后分配。
            m_running = true;
        }
        SetEvent(m_wake);
    }
private:
    struct WarmTarget
    {
        Ptr<IDWriteFontFace> face;
        float em = 0, sx = 1, sy = 1;
        DWRITE_MEASURING_MODE mode = DWRITE_MEASURING_MODE_NATURAL;
        UINT32 bidi = 0;
        BOOL sideways = FALSE;
        D2D1_COLOR_F foreground = {};
        size_t next = 0;
    };
    static const UINT32 *WarmCodepoints(size_t &count)
    {
        // 100 个单码点，不拆分肤色/ZWJ，不为未映射的码点猜测后备字体。
        static const UINT32 codes[] = {
            0x1F600,0x1F603,0x1F604,0x1F601,0x1F606,0x1F605,0x1F602,0x1F923,0x1F60A,0x1F60D,
            0x1F970,0x1F618,0x1F60E,0x1F914,0x1F62D,0x1F622,0x1F621,0x1F973,0x1F634,0x1F929,
            0x1F44D,0x1F44B,0x1F64F,0x1F4AA,0x1F389,0x1F382,0x1F381,0x1F525,0x1F4AF,0x2764,
            0x1F496,0x1F308,0x2600,0x1F680,0x1F44F,0x1F44C,0x1F64C,0x1F91D,0x1F4A1,0x1F4A5,
            0x1F60B,0x1F61C,0x1F61D,0x1F92A,0x1F607,0x1F917,0x1F92D,0x1F92B,0x1F644,0x1F60F,
            0x1F612,0x1F614,0x1F61E,0x1F61F,0x1F615,0x1F641,0x1F633,0x1F631,0x1F630,0x1F613,
            0x1F625,0x1F62C,0x1F910,0x1F92E,0x1F922,0x1F637,0x1F912,0x1F915,0x1F608,0x1F47B,
            0x1F480,0x1F916,0x1F4A9,0x1F63A,0x1F638,0x1F639,0x1F63B,0x1F63C,0x1F63D,0x1F640,
            0x1F436,0x1F431,0x1F98A,0x1F43C,0x1F981,0x1F984,0x1F338,0x1F339,0x1F33B,0x1F340,
            0x1F31F,0x2B50,0x1F319,0x1F30D,0x1F354,0x1F355,0x1F370,0x2615,0x1F37B,0x1F3C6
        };
        count = ARRAYSIZE(codes); return codes;
    }
    bool Stopping() { return InterlockedCompareExchange(&m_stop, 0, 0) != 0; }
    bool Idle()
    {
        return GetTickCount64() - static_cast<ULONGLONG>(InterlockedCompareExchange64(&m_lastForeground, 0, 0)) >= 300;
    }
    bool Contains(const Entry &entry)
    {
        const auto range = m_index.equal_range(entry.hash);
        for (auto it = range.first; it != range.second; ++it)
            if (it->second->key == entry.key && (!it->second->usesForeground ||
                memcmp(&it->second->foreground, &entry.foreground, sizeof(entry.foreground)) == 0)) return true;
        return false;
    }
    // 后台永不排队等待前台持有的 SRW；也没有任何 D2D 工作在共享锁内。
    class TryGuard
    {
    public:
        bool held;
        TryGuard() : held(TryAcquireSRWLockExclusive(&Process().lock) != FALSE) {}
        ~TryGuard() { if (held) ReleaseSRWLockExclusive(&Process().lock); }
    };
    static DWORD WINAPI WorkerEntry(void *parameter)
    {
        auto *owner = static_cast<QkColorGlyphCache *>(parameter);
        const HMODULE module = owner->m_workerModule;
        SetThreadDescription(GetCurrentThread(), L"QkEmojiPrewarm");
        const BOOL background = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        if (!background) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        char line[200];
        StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI6-THREAD tid=%lu background=%u priority=%d com=0x%08lx\n",
            GetCurrentThreadId(), background ? 1u : 0u, GetThreadPriority(GetCurrentThread()), com);
        Log(line);
        if (SUCCEEDED(com))
        {
            try { owner->PrewarmLoop(); }
            catch (...) { Log("QK-EMOJI6-ERROR worker-exception\n"); }
            CoUninitialize(); // 私有生成器和全部 D2D/WIC 对象已在工作线程正常释放。
        }
        Log("QK-EMOJI6-THREAD exited\n");
        if (background) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        owner->DropReference();
        FreeLibraryAndExitThread(module, 0); // 固定模块直到最后一条线程指令，DLL 卸载无需等待或释放 COM。
    }
    void PrewarmLoop()
    {
        QkColorGlyphCache generator(true); // 自己的 single-thread factory/context/WIC、reader 和计时。
        UINT64 batches = 0, generated = 0, skipped = 0, pauses = 0;
        LONGLONG totalTicks = 0;
        bool paused = false, exhausted = false;
        size_t count = 0; const UINT32 *codes = WarmCodepoints(count);
        while (!Stopping())
        {
            if (!Idle())
            {
                if (!paused) { ++pauses; paused = true; }
                WaitForSingleObject(m_wake, 50); continue;
            }
            paused = false;
            UINT attempted = 0, made = 0, omitted = 0;
            const LONGLONG start = QkFacadeClock();
            for (; attempted < 8 && !Stopping() && Idle(); ++attempted)
            {
                WarmTarget target; UINT32 code = 0;
                {
                    TryGuard guard;
                    if (!guard.held || Process().cache != this || Stopping()) break;
                    auto it = m_targets.begin();
                    for (; it != m_targets.end() && it->next >= count; ++it) {}
                    if (it == m_targets.end())
                    {
                        // 原子交接：后续登记在同一锁下看到 false，创建新的工作代。
                        // 当前代之后只析构自己的生成器；旧句柄保留，最终释放时一并有界等待。
                        m_running = false; exhausted = true; break;
                    }
                    target = *it; code = codes[it->next++];
                }
                UINT16 glyph = 0;
                if (FAILED(target.face->GetGlyphIndices(&code, 1, &glyph)) || !glyph) { ++omitted; continue; }
                const FLOAT advance = 0;
                DWRITE_GLYPH_RUN run = {};
                run.fontFace = target.face.Get(); run.fontEmSize = target.em; run.glyphCount = 1;
                run.glyphIndices = &glyph; run.glyphAdvances = &advance; run.bidiLevel = target.bidi; run.isSideways = target.sideways;
                size_t offset = 0;
                Entry lookup = {};
                lookup.hash = generator.MakeKey(run, target.mode, target.sx, target.sy, 0, 0, offset);
                lookup.key = generator.m_keyBuffer; lookup.foreground = target.foreground;
                {
                    TryGuard guard;
                    if (!guard.held) { ++omitted; continue; }
                    if (Process().cache != this || Stopping()) break;
                    if (Contains(lookup) || m_entries.size() >= 256 || m_warmEntries >= 64 ||
                        m_bytes >= 32 * 1024 * 1024 || m_warmBytes >= 8 * 1024 * 1024) { ++omitted; continue; }
                }
                std::vector<Prepared> prepared;
                if (generator.PrepareGlyphs(run, target.mode, target.sx, target.sy, 0, 0, target.foreground, prepared, 0, false) != S_OK ||
                    generator.m_entries.empty()) { ++omitted; continue; }
                auto &entry = generator.m_entries.front();
                const size_t bytes = static_cast<size_t>(entry.width) * entry.height * 4;
                {
                    TryGuard guard;
                    if (guard.held && Process().cache == this && !Stopping() && !Contains(entry) &&
                        m_entries.size() < 256 && m_warmEntries < 64 && m_bytes + bytes <= 32 * 1024 * 1024 &&
                        m_warmBytes + bytes <= 8 * 1024 * 1024)
                    {
                        entry.prewarmed = true;
                        m_entries.push_back(std::move(entry));
                        auto inserted = --m_entries.end();
                        try { m_index.emplace(inserted->hash, inserted); }
                        catch (...) { m_entries.pop_back(); throw; }
                        m_bytes += bytes; m_warmBytes += bytes; ++m_warmEntries; ++made;
                    }
                    else ++omitted;
                }
                generator.m_index.clear(); generator.m_entries.clear(); generator.m_bytes = 0;
            }
            if (attempted || made || omitted)
            {
                ++batches; generated += made; skipped += omitted;
                const LONGLONG ticks = QkFacadeClock() - start; totalTicks += ticks;
                LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
                char line[240];
                StringCchPrintfA(line, ARRAYSIZE(line),
                    "QK-EMOJI6-BATCH batches=%llu generated=%llu skipped=%llu pauses=%llu count=%u batchMs=%.6f elapsedMs=%.6f\n",
                    batches, generated, skipped, pauses, attempted, ticks * 1000.0 / frequency.QuadPart,
                    totalTicks * 1000.0 / frequency.QuadPart);
                Log(line);
            }
            if (exhausted) break;
            WaitForSingleObject(m_wake, 25);
        }
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        char line[240];
        StringCchPrintfA(line, ARRAYSIZE(line),
            "QK-EMOJI6-SUMMARY batches=%llu generated=%llu skipped=%llu pauses=%llu elapsedMs=%.6f\n",
            batches, generated, skipped, pauses, totalTicks * 1000.0 / frequency.QuadPart);
        Log(line);
    }

private:
    static void TraceSteps(const DWRITE_GLYPH_RUN &run, float sx, float sy, float phase,
        UINT formats, UINT w, UINT h, const Steps &steps)
    {
        if (!TimingEnabled()) return;
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        const double ms = 1000.0 / frequency.QuadPart;
        char line[1200];
        StringCchPrintfA(line, ARRAYSIZE(line),
            "QK-EMOJI4 {\"glyph\":%u,\"face\":\"%p\",\"em\":%.6f,\"sx\":%.6f,\"sy\":%.6f,\"phase\":%.2f,\"formats\":%u,\"width\":%u,\"height\":%u,"
            "\"init\":%.6f,\"faceMs\":%.6f,\"translate\":%.6f,\"paint\":%.6f,\"record\":%.6f,\"bounds\":%.6f,\"raster\":%.6f,\"copy\":%.6f,\"insert\":%.6f,\"rasterDraw\":%.6f,\"rasterShared\":%.6f}\n",
            run.glyphIndices[0], run.fontFace, run.fontEmSize, sx, sy, phase, formats, w, h,
            steps.ticks[Init] * ms, steps.ticks[Face] * ms, steps.ticks[Translate] * ms, steps.ticks[Paint] * ms,
            steps.ticks[Record] * ms, steps.ticks[Bounds] * ms, steps.ticks[Raster] * ms, steps.ticks[Copy] * ms, steps.ticks[Insert] * ms,
            steps.ticks[RasterDraw] * ms, steps.ticks[RasterShared] * ms);
        Log(line);
    }
    HRESULT DrawLayers(const std::vector<Layer> &layers, ID2D1SolidColorBrush *brush, const D2D1_COLOR_F &foreground)
    {
        for (const auto &layer : layers)
        {
            const auto run = layer.Run();
            switch (layer.format)
            {
            case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
            case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
                brush->SetColor(layer.palette == 0xFFFF ? foreground : layer.color);
                m_dc4->DrawGlyphRun(layer.origin, &run, brush, layer.mode); break;
            case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
            case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
            case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
                m_dc4->DrawColorBitmapGlyphRun(layer.format, layer.origin, &run, layer.mode, D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DISABLE); break;
            case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
                brush->SetColor(foreground); m_dc4->DrawSvgGlyphRun(layer.origin, &run, brush, nullptr, 0, layer.mode); break;
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE:
                if (!m_dc7) return E_NOINTERFACE;
                brush->SetColor(foreground); m_dc7->DrawPaintGlyphRun(layer.origin, &run, brush, 0, layer.mode); break;
            default: return E_NOTIMPL;
            }
        }
        return S_OK;
    }
    struct PaintInfo { bool foreground = true, hasClip = false; D2D_RECT_F clip = {}; };
    struct PaintFace
    {
        Ptr<IDWriteFontFace> face;
        Ptr<IDWritePaintReader> reader;
        std::unordered_map<UINT16, PaintInfo> glyphs;
    };
    PaintInfo ReadPaint(const DWRITE_GLYPH_RUN &run)
    {
        if (run.glyphCount != 1) return PaintInfo{};
        auto found = m_paint.find(run.fontFace);
        if (found == m_paint.end())
        {
            if (m_paint.size() >= 64) m_paint.clear();
            PaintFace font; font.face = run.fontFace;
            Ptr<IDWriteFontFace7> face7;
            if (SUCCEEDED(font.face.As(&face7)))
                (void)face7->CreatePaintReader(DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE,
                    DWRITE_PAINT_FEATURE_LEVEL_COLR_V1, &font.reader);
            found = m_paint.emplace(run.fontFace, std::move(font)).first;
        }
        auto &glyphs = found->second.glyphs;
        auto glyph = glyphs.find(run.glyphIndices[0]);
        if (glyph != glyphs.end()) return glyph->second;
        PaintInfo info = ReadPaintUncached(run, found->second.reader.Get());
        // 指针身份持引用；字体/字形属性与字号、相位和缩放无关。边界及保守失败均复用。
        if (glyphs.size() >= 4096) glyphs.clear();
        glyphs.emplace(run.glyphIndices[0], info);
        return info;
    }
    static PaintInfo ReadPaintUncached(const DWRITE_GLYPH_RUN &run, IDWritePaintReader *reader)
    {
        PaintInfo info;
        if (!reader) return info;
        if (run.glyphCount != 1) return info;
        DWRITE_PAINT_ELEMENT paint = {};
        DWRITE_PAINT_ATTRIBUTES attributes = DWRITE_PAINT_ATTRIBUTES_NONE;
        if (FAILED(reader->SetCurrentGlyph(run.glyphIndices[0], &paint, &info.clip, &attributes)) ||
            paint.paintType == DWRITE_PAINT_TYPE_NONE) return info;
        info.foreground = (attributes & DWRITE_PAINT_ATTRIBUTES_USES_TEXT_COLOR) != 0;
        info.hasClip = std::isfinite(info.clip.left) && std::isfinite(info.clip.top) &&
            std::isfinite(info.clip.right) && std::isfinite(info.clip.bottom) &&
            info.clip.right > info.clip.left && info.clip.bottom > info.clip.top;
        return info;
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
            "QK-EMOJI1 cache draws=%llu hits=%llu misses=%llu fallback=%llu evictions=%llu entries=%zu bytes=%zu drawMs=%.6f rasterMs=%.6f paint=%u hitDraws=%llu hitMs=%.6f contexts=%u scope=process\n",
            Stats().draws, Stats().hits, Stats().misses, Stats().fallbacks, Stats().evictions, m_entries.size(), m_bytes,
            Stats().drawTicks * 1000.0 / frequency.QuadPart, Stats().rasterTicks * 1000.0 / frequency.QuadPart,
            m_dc7 ? static_cast<UINT>(m_dc7->GetPaintFeatureLevel()) : 0, Stats().hitDraws,
            Stats().hitTicks * 1000.0 / frequency.QuadPart, Process().contexts);
        Log(line);
        StringCchPrintfA(line, ARRAYSIZE(line), "QK-EMOJI6-CAPACITY warmEntries=%zu warmBytes=%zu maxXShift=%.6f phases=%.0f\n",
            m_warmEntries, m_warmBytes, m_maxXShift, XPhases());
        Log(line);
    }
    struct Module { HMODULE handle = nullptr; ~Module() { if (handle) FreeLibrary(handle); } };
    Module m_d2d, m_dw;
    bool m_worker = false;
    ProcessState m_workerStats;
    ProcessState &Stats() { return m_worker ? m_workerStats : Process(); }
    volatile LONG m_lifetime = 1, m_stop = 0;
    volatile LONG64 m_lastForeground = 0;
    HANDLE m_wake = nullptr;
    std::vector<HANDLE> m_threads;
    bool m_running = false; // 仅共享SRW锁下访问；已完成队列不常驻轮询线程。
    HMODULE m_workerModule = nullptr;
    std::vector<WarmTarget> m_targets;
    size_t m_warmEntries = 0, m_warmBytes = 0;
    float m_maxXShift = 0;
    void DropReference() { if (InterlockedDecrement(&m_lifetime) == 0) delete this; }
    LONGLONG m_pendingInit = 0, m_pendingFace = 0;
    size_t m_pendingItem = 0;
    bool m_lastAnimated = false;
    std::list<Animation> m_animations;
    bool m_lastHit = false;
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
    std::unordered_map<IDWriteFontFace *, PaintFace> m_paint;
    std::vector<BYTE> m_keyBuffer;
    std::vector<D2D1_POINT_2F> m_origins;
    std::list<Entry> m_entries;
    std::unordered_multimap<UINT64, std::list<Entry>::iterator> m_index;
    size_t m_bytes = 0;
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
        if (!m_pColorGlyphCache) m_pColorGlyphCache = QkColorGlyphCache::Acquire();
        auto face = m_pColorGlyphCache->ColorFace(resource);
        if (!face) return S_OK;
        resource->GetColorGlyphRun(face.Get(), &run, &mode);
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
        std::vector<QkColorGlyphCache::Prepared> tiles; // 局部引用独立于共享 LRU，锁外绘制安全。
        if (m_pColorGlyphCache->PrepareGlyphs(run, mode, sx, sy, deviceX, deviceY, foreground, tiles, resource->GetColorGlyphId()) != S_OK)
        { return S_OK; }
        bool refine = false;
        for (const auto &tile : tiles) refine |= tile.interpolated;
        if (refine && FAILED(resource->RequestColorGlyphPass()))
        {
            // 无法安排收尾时立即生成精确位图，不能留下永久插值结果。
            tiles.clear();
            if (m_pColorGlyphCache->PrepareGlyphs(run, mode, sx, sy, deviceX, deviceY, foreground, tiles,
                resource->GetColorGlyphId(), false) != S_OK) return S_OK;
        }
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
            if (FAILED(hr)) { return hr; }
            scope.active = true;
        }
        *drawn = true; // 开始任何 DrawBitmap 后绝不再叠加整条单色 run。
        for (const auto &tile : tiles)
        {
            const LONGLONG start = QkFacadeClock();
            if (!tile.bitmap) { m_pColorGlyphCache->PreparedDrawTime(start, tile); continue; }
            CMilRectF rect;
            rect.left = (tile.x + tile.left - dx) / sx;
            rect.top = (tile.y + tile.top - dy) / sy;
            rect.right = rect.left + tile.width * tile.scaleX / sx; rect.bottom = rect.top + tile.height * tile.scaleY / sy;
            const MilRectF source = { 0, 0, static_cast<float>(tile.width), static_cast<float>(tile.height) };
            const auto oldInterpolation = m_renderState.InterpolationMode;
            m_renderState.InterpolationMode = tile.interpolated ? MilBitmapInterpolationMode::Linear : MilBitmapInterpolationMode::NearestNeighbor;
            const HRESULT hr = DrawBitmap(tile.bitmap.Get(), &source, &rect, 1.0f);
            m_renderState.InterpolationMode = oldInterpolation;
            m_pColorGlyphCache->PreparedDrawTime(start, tile);
            if (FAILED(hr)) { return hr; }
        }
        if (scope.active)
        {
            scope.active = false;
            const HRESULT hr = Pop();
            if (FAILED(hr)) return hr;
        }
        // 只用已经成功绘制的真实彩色图登记；单色后备/动画临时插值不占用尺寸组合名额。
        for (const auto &tile : tiles)
            if (tile.color && !tile.interpolated)
            {
                m_pColorGlyphCache->RegisterTarget(run, mode, sx, sy, foreground);
                break;
            }
        return S_OK;
    }
    catch (const std::bad_alloc &)
    {
        return S_OK;
    }
}
#endif
