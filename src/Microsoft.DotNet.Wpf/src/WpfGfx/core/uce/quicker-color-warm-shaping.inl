// 仅由 facade 的 QkColorGlyphCache 类体包含；不选择后备字体。
    class WarmAnalysis final : public IDWriteTextAnalysisSource, public IDWriteTextAnalysisSink
    {
        LONG refs = 1;
    public:
        const WCHAR *text;
        UINT32 length;
        bool rtl;
        struct Script { UINT32 start, length; DWRITE_SCRIPT_ANALYSIS analysis; };
        std::vector<Script> scripts;
        WarmAnalysis(const WCHAR *value, bool rightToLeft) : text(value), length(static_cast<UINT32>(wcslen(value))), rtl(rightToLeft) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
            if (!out) return E_POINTER;
            *out = nullptr;
            if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextAnalysisSource)) *out = static_cast<IDWriteTextAnalysisSource *>(this);
            else if (iid == __uuidof(IDWriteTextAnalysisSink)) *out = static_cast<IDWriteTextAnalysisSink *>(this);
            else return E_NOINTERFACE;
            AddRef(); return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
        ULONG STDMETHODCALLTYPE Release() override { LONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
        HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, const WCHAR **out, UINT32 *count) override {
            *out = pos < length ? text + pos : nullptr; *count = pos < length ? length - pos : 0; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 pos, const WCHAR **out, UINT32 *count) override {
            *out = pos && pos <= length ? text : nullptr; *count = pos <= length ? pos : 0; return S_OK;
        }
        DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
            return rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        }
        HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 pos, UINT32 *count, const WCHAR **out) override {
            *count = pos < length ? length - pos : 0; *out = L"en-US"; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 pos, UINT32 *count, IDWriteNumberSubstitution **out) override {
            *count = pos < length ? length - pos : 0; *out = nullptr; return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32 pos, UINT32 count, const DWRITE_SCRIPT_ANALYSIS *analysis) override {
            try { scripts.push_back({pos, count, *analysis}); } catch (...) { return E_OUTOFMEMORY; }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32, UINT32, const DWRITE_LINE_BREAKPOINT *) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution *) override { return S_OK; }
    };
    static const WCHAR *const *WarmSequences(size_t &count)
    {
        // 20 个肤色手势（四种手势×五色）+20 个常用 ZWJ；按字形去重后仍受原容量约束。
        static const WCHAR *const sequences[] = {
            L"\U0001F469\u200D\u2764\uFE0F\u200D\U0001F468", // 情侣：WPF 本字体可能拆成多个 glyph。
            L"\U0001F9D1\U0001F3FD\u200D\U0001F4BB", L"\U0001F9D1\u200D\U0001F4BB",
            L"\U0001F44B\U0001F3FE", L"\U0001F44D\U0001F3FD", L"\U0001F44D\U0001F3FB",
            L"\U0001F44D\U0001F3FC", L"\U0001F44D\U0001F3FE", L"\U0001F44D\U0001F3FF",
            L"\U0001F44B\U0001F3FB", L"\U0001F44B\U0001F3FC", L"\U0001F44B\U0001F3FD", L"\U0001F44B\U0001F3FF",
            L"\U0001F64F\U0001F3FB", L"\U0001F64F\U0001F3FC", L"\U0001F64F\U0001F3FD", L"\U0001F64F\U0001F3FE", L"\U0001F64F\U0001F3FF",
            L"\U0001F44F\U0001F3FB", L"\U0001F44F\U0001F3FC", L"\U0001F44F\U0001F3FD", L"\U0001F44F\U0001F3FE", L"\U0001F44F\U0001F3FF",
            L"\U0001F469\u200D\U0001F4BB", L"\U0001F468\u200D\U0001F4BB", L"\U0001F469\u200D\u2695\uFE0F", L"\U0001F468\u200D\u2695\uFE0F",
            L"\U0001F9D1\u200D\U0001F680", L"\U0001F469\u200D\U0001F680", L"\U0001F468\u200D\U0001F680",
            L"\U0001F468\u200D\U0001F469\u200D\U0001F467", L"\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466",
            L"\U0001F469\u200D\U0001F469\u200D\U0001F467", L"\U0001F468\u200D\U0001F468\u200D\U0001F466",
            L"\U0001F3F3\uFE0F\u200D\U0001F308", L"\U0001F3F3\uFE0F\u200D\u26A7\uFE0F",
            L"\U0001F469\u200D\u2764\uFE0F\u200D\U0001F469", L"\U0001F468\u200D\u2764\uFE0F\u200D\U0001F468",
            L"\u2764\uFE0F\u200D\U0001F525", L"\U0001F636\u200D\U0001F32B\uFE0F"
        };
        count = ARRAYSIZE(sequences); return sequences;
    }
    void BuildWarmGlyphs(WarmTarget &target)
    {
        QK_RT_SCOPE("prewarm.shape");
        auto add = [&](UINT16 glyph) {
            if (glyph && target.glyphs.size() < 512 && std::find(target.glyphs.begin(), target.glyphs.end(), glyph) == target.glyphs.end())
                target.glyphs.push_back(glyph);
        };
        size_t count = 0; const UINT32 *codes = WarmCodepoints(count);
        // 前40个高频单字形之后插入复杂序列；后60个低频单码点仍保留。
        for (size_t i = 0; i < min(count, size_t(40)); ++i) {
            UINT16 glyph = 0; if (SUCCEEDED(target.face->GetGlyphIndices(codes + i, 1, &glyph))) add(glyph);
        }
        Ptr<IDWriteTextAnalyzer> analyzer;
        if (SUCCEEDED(m_dw4->CreateTextAnalyzer(&analyzer))) {
            size_t sequencesCount = 0; const WCHAR *const *sequences = WarmSequences(sequencesCount);
            for (size_t i = 0; i < sequencesCount; ++i) {
                Ptr<WarmAnalysis> analysis; analysis.Attach(new WarmAnalysis(sequences[i], (target.bidi & 1) != 0));
                if (FAILED(analyzer->AnalyzeScript(analysis.Get(), 0, analysis->length, analysis.Get()))) continue;
                for (const auto &script : analysis->scripts) {
                    if (script.length > 64) continue;
                    UINT16 clusters[64] = {}, glyphs[128] = {};
                    DWRITE_SHAPING_TEXT_PROPERTIES textProps[64] = {};
                    DWRITE_SHAPING_GLYPH_PROPERTIES glyphProps[128] = {};
                    UINT32 actual = 0;
                    if (SUCCEEDED(analyzer->GetGlyphs(analysis->text + script.start, script.length, target.face.Get(),
                        target.sideways, (target.bidi & 1) != 0, &script.analysis, L"en-US", nullptr, nullptr, nullptr, 0,
                        ARRAYSIZE(glyphs), clusters, textProps, glyphs, glyphProps, &actual))) {
                        for (UINT32 n = 0; n < actual; ++n) {
                            add(glyphs[n]);
                            if (QkRtEnabled()) {
                                char info[120]; StringCchPrintfA(info, ARRAYSIZE(info), "{\"sequence\":%zu,\"glyph\":%u,\"face\":\"%p\"}", i, glyphs[n], target.face.Get());
                                QkRtPoint("prewarm.shaped", info);
                            }
                        }
                    }
                }
            }
        }
        for (size_t i = 40; i < count; ++i) {
            UINT16 glyph = 0; if (SUCCEEDED(target.face->GetGlyphIndices(codes + i, 1, &glyph))) add(glyph);
        }
        target.shaped = true;
    }
