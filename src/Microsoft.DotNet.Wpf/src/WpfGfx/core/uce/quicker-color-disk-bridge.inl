// QkColorGlyphCache 类体内；仅在显式磁盘开关下构建跨进程精确键。
    struct DiskFont { Ptr<IDWriteFontFace> face; std::vector<BYTE> identity; };
    std::vector<BYTE> DiskKey(const Entry &entry, bool usesForeground) {
        if (!m_disk) return {};
        auto found = m_diskFonts.find(entry.face.Get());
        if (found == m_diskFonts.end()) {
            if (m_diskFonts.size() >= 64) m_diskFonts.clear();
            DiskFont font; font.face = entry.face;
            UINT32 count = 0;
            if (FAILED(entry.face->GetFiles(&count, nullptr)) || !count || count > 16) return {};
            std::vector<Ptr<IDWriteFontFile>> files(count);
            std::vector<IDWriteFontFile *> raw(count, nullptr);
            const HRESULT filesResult = entry.face->GetFiles(&count, raw.data());
            for (size_t i = 0; i < raw.size(); ++i) files[i].Attach(raw[i]);
            if (FAILED(filesResult)) return {};
            bool valid = true;
            Append(font.identity, &count, sizeof(count));
            for (auto &file : files) {
                const void *key = nullptr; UINT32 size = 0, length = 0;
                Ptr<IDWriteFontFileLoader> loader; Ptr<IDWriteLocalFontFileLoader> local;
                if (FAILED(file->GetReferenceKey(&key, &size)) || FAILED(file->GetLoader(&loader)) ||
                    FAILED(loader.As(&local)) || FAILED(local->GetFilePathLengthFromKey(key, size, &length)) || length >= 16000) { valid = false; break; }
                std::vector<WCHAR> path(size_t(length) + 1);
                WIN32_FILE_ATTRIBUTE_DATA attributes = {};
                if (FAILED(local->GetFilePathFromKey(key, size, path.data(), length + 1)) ||
                    !GetFileAttributesExW(path.data(), GetFileExInfoStandard, &attributes)) { valid = false; break; }
                Append(font.identity, &length, sizeof(length));
                Append(font.identity, path.data(), size_t(length) * sizeof(WCHAR));
                Append(font.identity, &attributes.nFileSizeHigh, sizeof(attributes.nFileSizeHigh));
                Append(font.identity, &attributes.nFileSizeLow, sizeof(attributes.nFileSizeLow));
                Append(font.identity, &attributes.ftLastWriteTime, sizeof(attributes.ftLastWriteTime));
            }
            if (!valid) font.identity.clear();
            else {
                const UINT32 index = entry.face->GetIndex(), simulations = entry.face->GetSimulations(), type = entry.face->GetType();
                Append(font.identity, &index, sizeof(index)); Append(font.identity, &simulations, sizeof(simulations)); Append(font.identity, &type, sizeof(type));
                // OS 渲染组件更换也失效，不复用不同 DWrite/D2D 栅格器生成的位图。
                for (HMODULE module : {m_dw.handle, m_d2d.handle}) {
                    WCHAR path[32768]; WIN32_FILE_ATTRIBUTE_DATA attributes = {};
                    const DWORD length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
                    if (!length || length >= ARRAYSIZE(path) || !GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) { font.identity.clear(); break; }
                    Append(font.identity, &attributes.nFileSizeHigh, sizeof(attributes.nFileSizeHigh));
                    Append(font.identity, &attributes.nFileSizeLow, sizeof(attributes.nFileSizeLow));
                    Append(font.identity, &attributes.ftLastWriteTime, sizeof(attributes.ftLastWriteTime));
                }
            }
            found = m_diskFonts.emplace(entry.face.Get(), std::move(font)).first;
        }
        if (found->second.identity.empty()) return {};
        std::vector<BYTE> key = found->second.identity;
        // 保留 glyphCount；用持久字体身份替换进程内 face 指针，其余精确键逐字节保存。
        Append(key, entry.key.data(), sizeof(UINT));
        const size_t skip = sizeof(UINT) + sizeof(IDWriteFontFace *);
        if (entry.key.size() < skip) return {};
        Append(key, entry.key.data() + skip, entry.key.size() - skip);
        const UINT32 paint = m_dc7 ? m_dc7->GetPaintFeatureLevel() : 0;
        Append(key, &paint, sizeof(paint));
        const BYTE dependent = usesForeground ? 1 : 0;
        Append(key, &dependent, sizeof(dependent));
        if (usesForeground) Append(key, &entry.foreground, sizeof(entry.foreground));
        return key;
    }
    Entry *InsertEntry(Entry value) {
        const size_t bytes = size_t(value.width) * value.height * 4;
        while (!m_entries.empty() && (m_entries.size() >= 256 || m_bytes + bytes > 32 * 1024 * 1024)) {
            auto victim = --m_entries.end();
            const size_t removed = size_t(victim->width) * victim->height * 4;
            m_bytes -= removed;
            const auto victims = m_index.equal_range(victim->hash);
            for (auto it = victims.first; it != victims.second; ++it)
                if (it->second == victim) { m_index.erase(it); break; }
            if (victim->prewarmed) { --m_warmEntries; m_warmBytes -= removed; }
            m_entries.pop_back(); ++Stats().evictions;
        }
        m_entries.push_front(std::move(value));
        try { m_index.emplace(m_entries.front().hash, m_entries.begin()); }
        catch (...) { m_entries.pop_front(); throw; }
        m_bytes += bytes;
        return &m_entries.front();
    }
    Entry *TryDisk(Entry &entry) {
        if (!m_disk) return nullptr;
        QkRtScope scope("color.disk.lookup", "{}", true);
        try {
            QkColorDiskCache::Tile tile = {};
            bool found = false;
            for (bool dependent : {false, true}) {
                const auto key = DiskKey(entry, dependent);
                if (!key.empty() && m_disk->Find(key, tile)) { found = true; break; }
            }
            if (!found) { QkRtPoint("color.disk.miss"); return nullptr; }
            entry.width = tile.width; entry.height = tile.height; entry.left = tile.left; entry.top = tile.top;
            entry.usesForeground = (tile.flags & 1) != 0; entry.color = (tile.flags & 2) != 0;
            if (tile.width && tile.height) {
                Ptr<IWICBitmap> bitmap; Ptr<IWICBitmapLock> pixels;
                const WICRect rect = {0, 0, static_cast<INT>(tile.width), static_cast<INT>(tile.height)};
                HRESULT hr = m_wic->CreateBitmap(tile.width, tile.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
                if (SUCCEEDED(hr)) hr = bitmap->Lock(&rect, WICBitmapLockWrite, &pixels);
                UINT stride = 0, available = 0; BYTE *data = nullptr;
                if (SUCCEEDED(hr)) hr = pixels->GetStride(&stride);
                if (SUCCEEDED(hr)) hr = pixels->GetDataPointer(&available, &data);
                if (FAILED(hr) || available < size_t(stride) * tile.height) return nullptr;
                for (UINT y = 0; y < tile.height; ++y) memcpy(data + size_t(y) * stride, tile.pixels + size_t(y) * tile.width * 4, size_t(tile.width) * 4);
                pixels.Reset();
                if (FAILED(CWICWrapperBitmap::Create(bitmap.Get(), &entry.bitmap))) return nullptr;
            }
            auto result = InsertEntry(std::move(entry));
            return result;
        } catch (...) { QkRtPoint("color.disk.disabled.lookup"); return nullptr; }
    }
    void QueueDisk(const Entry &entry, const BYTE *pixels, UINT stride) {
        if (!m_disk) return;
        try {
            const auto key = DiskKey(entry, entry.usesForeground);
            m_disk->Queue(key, entry.width, entry.height, entry.left, entry.top,
                (entry.usesForeground ? 1 : 0) | (entry.color ? 2 : 0), pixels, stride);
        } catch (...) { QkRtPoint("disk.queue.failed"); }
    }
