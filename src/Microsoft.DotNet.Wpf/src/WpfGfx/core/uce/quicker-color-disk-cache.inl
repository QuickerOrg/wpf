// 仅 facade 包含。未显式指定目录时不构造本类，不查询字体文件身份、不访问文件。
#if QUICKER_WPF_D3D11_FACADE
class QkColorDiskCache : public std::enable_shared_from_this<QkColorDiskCache>
{
    static constexpr UINT32 Magic = 0x39474351, Version = 1;
    static constexpr size_t Limit = 16 * 1024 * 1024, QueueLimit = 8 * 1024 * 1024;
#pragma pack(push, 1)
    struct Header { UINT32 magic, version, size, reserved; };
    struct Record {
        UINT32 size, keySize, width, height;
        float left, top;
        UINT32 flags, bytes;
        UINT64 checksum;
    };
#pragma pack(pop)
    HANDLE m_file = INVALID_HANDLE_VALUE, m_mapping = nullptr, m_wake = nullptr, m_thread = nullptr;
    const BYTE *m_view = nullptr;
    size_t m_length = 0, m_writeLength = 0, m_queued = 0;
    bool m_writable = false, m_running = false, m_stop = false, m_rebuild = false;
    std::wstring m_path;
    SRWLOCK m_lock = SRWLOCK_INIT;
    std::map<std::vector<BYTE>, size_t> m_index; // 初始化后只读；不解析可变/不可信指针。
    std::vector<std::vector<BYTE>> m_pending;
    std::set<std::vector<BYTE>> m_written;
    static UINT64 Hash(const BYTE *data, size_t size) {
        UINT64 hash = 14695981039346656037ull;
        for (size_t i = 0; i < size; ++i) { hash ^= data[i]; hash *= 1099511628211ull; }
        return hash;
    }
    static void Event(const char *name, size_t entries = 0, size_t bytes = 0) {
        if (!QkRtEnabled()) return;
        char info[160]; StringCchPrintfA(info, ARRAYSIZE(info), "{\"entries\":%zu,\"bytes\":%zu}", entries, bytes);
        QkRtPoint(name, info);
    }
    void Unmap() {
        if (m_view) UnmapViewOfFile(m_view);
        if (m_mapping) CloseHandle(m_mapping);
        m_view = nullptr; m_mapping = nullptr; m_length = 0; m_index.clear();
    }
    bool Rebuild() {
        // 初始化已脱离旧映射；此函数仅在写线程运行，不访问只读索引。
        if (!m_writable) return false;
        LARGE_INTEGER zero = {};
        if (!SetFilePointerEx(m_file, zero, nullptr, FILE_BEGIN) || !SetEndOfFile(m_file)) return false;
        const Header header = {Magic, Version, sizeof(Header), 0};
        DWORD written = 0;
        if (!WriteFile(m_file, &header, sizeof(header), &written, nullptr) || written != sizeof(header)) return false;
        m_writeLength = sizeof(header); Event("disk.rebuild"); return true;
    }
    bool LoadIndex() {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(m_file, &size) || size.QuadPart < sizeof(Header) || size.QuadPart > Limit) return false;
        m_length = static_cast<size_t>(size.QuadPart);
        m_mapping = CreateFileMappingW(m_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!m_mapping) return false;
        m_view = static_cast<const BYTE *>(MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0));
        if (!m_view) return false;
        Header header; memcpy(&header, m_view, sizeof(header));
        if (header.magic != Magic || header.version != Version || header.size != sizeof(Header) || header.reserved) return false;
        size_t offset = sizeof(Header);
        while (offset < m_length) {
            if (m_length - offset < sizeof(Record)) return false;
            Record record; memcpy(&record, m_view + offset, sizeof(record));
            if (record.size < sizeof(Record) || record.size > m_length - offset || !record.keySize || record.keySize > 32768 ||
                record.width > 1024 || record.height > 1024 || record.bytes != size_t(record.width) * record.height * 4 ||
                record.size != sizeof(Record) + size_t(record.keySize) + record.bytes || record.flags > 3 ||
                !std::isfinite(record.left) || !std::isfinite(record.top) || fabsf(record.left) > 1e7f || fabsf(record.top) > 1e7f) return false;
            // 校验包含几何/标志/完整键/像素；只把 checksum 自身置零。
            std::vector<BYTE> checked(m_view + offset, m_view + offset + record.size);
            memset(checked.data() + offsetof(Record, checksum), 0, sizeof(UINT64));
            if (Hash(checked.data(), checked.size()) != record.checksum) return false;
            const BYTE *key = m_view + offset + sizeof(Record);
            std::vector<BYTE> exact(key, key + record.keySize);
            if (m_index.size() >= 4096) return false;
            m_index[exact] = offset; m_written.insert(std::move(exact));
            offset += record.size;
        }
        m_writeLength = m_length;
        Event("disk.index", m_index.size(), m_length); return true;
    }
    struct ThreadWork { std::shared_ptr<QkColorDiskCache> owner; HMODULE module; };
    static DWORD WINAPI WriteThread(void *parameter) {
        auto work = static_cast<ThreadWork *>(parameter);
        auto owner = work->owner; const HMODULE module = work->module; delete work;
        SetThreadDescription(GetCurrentThread(), L"QkEmojiDiskWrite");
        const BOOL background = SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        if (!background) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        Event("disk.thread.entry");
        try { owner->WriteLoop(); } catch (...) {
            AcquireSRWLockExclusive(&owner->m_lock);
            owner->m_writable = false; owner->m_running = false;
            owner->m_pending.clear(); owner->m_queued = 0;
            ReleaseSRWLockExclusive(&owner->m_lock);
            Event("disk.write.exception");
        }
        Event("disk.thread.exit");
        if (background) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        owner.reset();
        FreeLibraryAndExitThread(module, 0);
    }
    void WriteLoop() {
        // 新文件的创建、失效包截断与头部写入也留在低优先级线程。
        if (m_file == INVALID_HANDLE_VALUE)
            m_file = CreateFileW(m_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE || (m_rebuild && !Rebuild())) {
            AcquireSRWLockExclusive(&m_lock);
            m_writable = false; m_running = false; m_pending.clear(); m_queued = 0;
            ReleaseSRWLockExclusive(&m_lock); Event("disk.write.failed.initialize"); return;
        }
        m_rebuild = false;
        for (;;) {
            WaitForSingleObject(m_wake, 100); // 合并短批；不持有字形缓存锁或 COM 对象。
            std::vector<std::vector<BYTE>> batch;
            AcquireSRWLockExclusive(&m_lock);
            batch.swap(m_pending); m_queued = 0;
            if (batch.empty()) { m_running = false; ReleaseSRWLockExclusive(&m_lock); break; }
            ReleaseSRWLockExclusive(&m_lock);
            size_t writtenBytes = 0, appended = 0;
            for (auto &record : batch) {
                Record header; memcpy(&header, record.data(), sizeof(header));
                const auto begin = record.begin() + sizeof(Record);
                std::vector<BYTE> key(begin, begin + header.keySize);
                if (m_written.count(key)) continue;
                if (record.size() > Limit - m_writeLength) { Event("disk.capacity"); continue; }
                LARGE_INTEGER end; end.QuadPart = static_cast<LONGLONG>(m_writeLength);
                DWORD written = 0;
                if (!SetFilePointerEx(m_file, end, nullptr, FILE_BEGIN) ||
                    !WriteFile(m_file, record.data(), static_cast<DWORD>(record.size()), &written, nullptr) || written != record.size()) {
                    AcquireSRWLockExclusive(&m_lock); m_writable = false; ReleaseSRWLockExclusive(&m_lock);
                    Event("disk.write.failed"); break;
                }
                m_writeLength += written; writtenBytes += written; ++appended; m_written.insert(std::move(key));
            }
            Event("disk.append", appended, writtenBytes);
        }
    }
public:
    ~QkColorDiskCache() {
        Unmap();
        if (m_file != INVALID_HANDLE_VALUE) CloseHandle(m_file);
        if (m_wake) CloseHandle(m_wake);
        if (m_thread) CloseHandle(m_thread);
    }
    static std::shared_ptr<QkColorDiskCache> Open() {
        WCHAR directory[32768];
        const DWORD length = GetEnvironmentVariableW(L"QUICKER_WPF_COLOR_GLYPHS_DISK_CACHE", directory, ARRAYSIZE(directory));
        if (!length || length >= ARRAYSIZE(directory)) return {};
        QK_RT_SCOPE("disk.open");
        auto disk = std::make_shared<QkColorDiskCache>();
        // 不创建目录；只有调用者显式配置的已有目录被访问。
        std::wstring file(directory); if (file.back() != L'/' && file.back() != L'\\') file += L'\\'; file += L"color-glyphs-v1.pack";
        disk->m_path = file;
        disk->m_file = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const DWORD openError = GetLastError();
        disk->m_writable = disk->m_file != INVALID_HANDLE_VALUE;
        if (!disk->m_writable && (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND)) {
            disk->m_writable = true; disk->m_rebuild = true; disk->m_writeLength = sizeof(Header);
            Event("disk.create.pending");
        } else {
            if (!disk->m_writable) {
                // 首个进程是唯一 writer；第二进程快照只读，无等待/重试/跨进程锁。
                disk->m_file = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                Event("disk.readonly");
            }
            if (disk->m_file == INVALID_HANDLE_VALUE) { Event("disk.disabled.open"); return {}; }
            const bool loaded = disk->LoadIndex();
            if (!loaded || (disk->m_writable && disk->m_writeLength >= Limit - 1024 * 1024)) {
                // 不把无效/超限映射交给渲染线程。重建延迟到有数据要写时。
                disk->Unmap(); disk->m_written.clear();
                if (!disk->m_writable) { Event("disk.disabled.invalid"); return {}; }
                disk->m_rebuild = true; disk->m_writeLength = sizeof(Header); Event("disk.rebuild.pending");
            }
        }
        disk->m_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!disk->m_wake) disk->m_writable = false;
        return disk;
    }
    struct Tile { UINT width, height, flags; float left, top; const BYTE *pixels; };
    bool Find(const std::vector<BYTE> &key, Tile &tile) const {
        const auto found = m_index.find(key);
        if (found == m_index.end()) return false;
        Record record; memcpy(&record, m_view + found->second, sizeof(record));
        tile = {record.width, record.height, record.flags, record.left, record.top,
            m_view + found->second + sizeof(Record) + record.keySize};
        return true;
    }
    void Queue(const std::vector<BYTE> &key, UINT width, UINT height, float left, float top, UINT flags, const BYTE *pixels, UINT stride) {
        if (key.empty() || key.size() > 32768 || width > 1024 || height > 1024) return;
        const size_t bytes = size_t(width) * height * 4, size = sizeof(Record) + key.size() + bytes;
        // 前台不排队等待写线程；只复制独立像素，发布后后台拥有数据。
        if (!TryAcquireSRWLockExclusive(&m_lock)) { Event("disk.queue.busy"); return; }
        if (!m_writable || m_stop || size > QueueLimit - m_queued) { ReleaseSRWLockExclusive(&m_lock); return; }
        try {
            std::vector<BYTE> data(size);
            Record record = {static_cast<UINT32>(size), static_cast<UINT32>(key.size()), width, height, left, top, flags, static_cast<UINT32>(bytes), 0};
            memcpy(data.data(), &record, sizeof(record)); memcpy(data.data() + sizeof(record), key.data(), key.size());
            for (UINT y = 0; y < height; ++y) memcpy(data.data() + sizeof(record) + key.size() + size_t(y) * width * 4, pixels + size_t(y) * stride, size_t(width) * 4);
            record.checksum = Hash(data.data(), data.size()); memcpy(data.data(), &record, sizeof(record));
            m_pending.push_back(std::move(data)); m_queued += size;
            if (!m_running) {
                HMODULE module = nullptr;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&WriteThread), &module)) {
                    auto work = new(std::nothrow) ThreadWork{shared_from_this(), module};
                    HANDLE thread = work ? CreateThread(nullptr, 0, WriteThread, work, 0, nullptr) : nullptr;
                    if (thread) { if (m_thread) CloseHandle(m_thread); m_thread = thread; m_running = true; }
                    else { delete work; FreeLibrary(module); }
                }
            }
            SetEvent(m_wake);
        } catch (...) { Event("disk.queue.failed"); } // 可选缓存失败不能改变绘制。
        ReleaseSRWLockExclusive(&m_lock);
    }
    void Stop(bool join = false) {
        AcquireSRWLockExclusive(&m_lock); m_stop = true; if (m_wake) SetEvent(m_wake);
        HANDLE thread = m_thread; ReleaseSRWLockExclusive(&m_lock);
        if (!join) { Event("disk.stop.signaled"); return; }
        if (thread) {
            const DWORD result = WaitForSingleObject(thread, 2000);
            Event(result == WAIT_OBJECT_0 ? "disk.stop.joined" : "disk.stop.deferred");
        }
    }
};
#endif
