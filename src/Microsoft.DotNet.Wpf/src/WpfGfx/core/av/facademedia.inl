// 门面专用媒体实现：保留 IMILMedia/媒体通道，用 MF 替换 WMP/EVR。
// 本文件只在 QUICKER_WPF_D3D11_FACADE 下编译，不实现 D3D9 互操作。
#include <mfapi.h>
#include <mfmediaengine.h>
#include <d3d10.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

// MF 回调不进入播放器锁，也不操作 GPU；防止 Shutdown/Transfer 与回调互等。
class QkMediaNotify final : public IMFMediaEngineNotify
{
    LONG m_refs = 1;
public:
    struct Event { DWORD kind; HRESULT hr; };
    CRITICAL_SECTION lock;
    std::vector<Event> events;
    QkMediaNotify() { InitializeCriticalSection(&lock); }
    ~QkMediaNotify() { DeleteCriticalSection(&lock); }
    STDMETHOD(QueryInterface)(REFIID id, void **out) override {
        *out = nullptr;
        if (id != __uuidof(IUnknown) && id != __uuidof(IMFMediaEngineNotify)) return E_NOINTERFACE;
        *out = static_cast<IMFMediaEngineNotify *>(this); AddRef(); return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_refs); }
    STDMETHOD_(ULONG, Release)() override { LONG n = InterlockedDecrement(&m_refs); if (!n) delete this; return n; }
    STDMETHOD(EventNotify)(DWORD kind, DWORD_PTR p1, DWORD p2) override {
        UNREFERENCED_PARAMETER(p1);
        EnterCriticalSection(&lock); events.push_back({kind, static_cast<HRESULT>(p2)}); LeaveCriticalSection(&lock);
        return S_OK;
    }
    std::vector<Event> Take() {
        EnterCriticalSection(&lock); std::vector<Event> result; result.swap(events); LeaveCriticalSection(&lock); return result;
    }
};

// CPU 不能读取尚未下载的 GPU 帧。硬件缓存未命中时明确失败，禁止上传黑色占位冒充视频。
class QkMediaBitmap final : public CClientMemoryBitmap
{
public:
    std::vector<BYTE> storage;
    STDMETHOD(CopyPixels)(const MILRect *, UINT, UINT, BYTE *) override {
        QkFacadeTrace("QK-MEDIA software bitmap read unsupported\n"); return E_NOTIMPL;
    }
};

class QkMediaPlayer final : public IMILMedia, public IMILSurfaceRendererProvider, public IAVSurfaceRenderer
{
    LONG m_refs = 1;
    CRITICAL_SECTION m_lock;
    HANDLE m_stop = nullptr, m_thread = nullptr;
    ComPtr<MediaInstance> m_instance;
    ComPtr<QkMediaNotify> m_notify;
    ComPtr<IMFMediaEngine> m_engine;
    ComPtr<IMFMediaEngineEx> m_ex;
    ComPtr<IMFDXGIDeviceManager> m_manager;
    ComPtr<QkD3D11State> m_state;
    ComPtr<QkTextureHandle> m_frame;
    ComPtr<QkMediaBitmap> m_bitmap;
    ID3D11Device *m_cachedDevice = nullptr;
    bool m_startedMF = false, m_loaded = false, m_opened = false, m_failed = false;
    bool m_buffering = false, m_scrubbing = false;
    double m_rate = 0, m_volume = .5, m_balance = 0, m_seek = 0;
    bool m_pendingSeek = false;
    DWORD m_width = 0, m_height = 0;
    LONGLONG m_tick = -1, m_serial = 0;
    LONGLONG m_loggedSecond = -1;
    LONGLONG m_renderedSerial = -1;
    struct Guard {
        CRITICAL_SECTION &lock;
        Guard(CRITICAL_SECTION &value) : lock(value) { EnterCriticalSection(&lock); }
        ~Guard() { LeaveCriticalSection(&lock); }
    };
    void Trace(const char *action, HRESULT hr = S_OK) {
        char line[320]; StringCchPrintfA(line, ARRAYSIZE(line),
            "QK-MEDIA id=%u %s hr=0x%08x position=%.3f size=%ux%u generation=%llu rate=%.3f target=%.3f\n",
            m_instance->GetID(), action, hr, m_engine ? m_engine->GetCurrentTime() : 0,
            m_width, m_height, m_state ? m_state->generation : 0,
            m_engine ? m_engine->GetPlaybackRate() : 0, m_rate); QkFacadeTrace(line);
    }
    void Fail(HRESULT hr) {
        if (!m_failed) { m_failed = true; Trace("MediaFailed", hr); m_instance->GetMediaEventProxy().RaiseEvent(AVMediaFailed, hr); }
    }
    HRESULT InitEngine() {
        // 共享同适配器门面设备；即使音频也绑定 manager，但不创建任何视频纹理。
        QkFacadeSharedGuard shared;
        HRESULT hr = QkD3D11State::Create(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &m_state);
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(hr)) hr = m_state->context.As(&multithread);
        if (SUCCEEDED(hr)) multithread->SetMultithreadProtected(TRUE);
        UINT token = 0;
        if (SUCCEEDED(hr)) hr = MFCreateDXGIDeviceManager(&token, &m_manager);
        if (SUCCEEDED(hr)) hr = m_manager->ResetDevice(m_state->device.Get(), token);
        ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attributes, 4);
        m_notify.Attach(new QkMediaNotify());
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_notify.Get());
        if (SUCCEEDED(hr)) hr = attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_manager.Get());
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        ComPtr<IMFMediaEngineClassFactory> factory;
        if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateInstance(0, attributes.Get(), &m_engine);
        if (SUCCEEDED(hr)) hr = m_engine.As(&m_ex);
        if (SUCCEEDED(hr)) hr = m_engine->SetAutoPlay(FALSE);
        Trace("initialize DXGI hardware-enabled; decoder selection=MF (not independently verified)", hr);
        return hr;
    }
    void ReleaseEngine() {
        if (m_engine) m_engine->Shutdown();
        if (m_frame) {
            char event[160]; StringCchPrintfA(event, ARRAYSIZE(event), "{\"event\":\"media-frame-release-request\",\"id\":%u,\"object\":\"%p\"}\n", m_instance->GetID(), m_frame->texture.Get()); QkAccountEvent(event);
            QkD3D11State::RetireMediaTexture(m_frame.Get());
        }
        m_bitmap.Reset(); m_frame.Reset(); m_cachedDevice = nullptr;
        m_ex.Reset(); m_engine.Reset(); m_manager.Reset(); m_notify.Reset(); m_state.Reset();
        m_loaded = m_opened = m_buffering = false; m_width = m_height = 0; m_tick = -1;
        m_renderedSerial = -1; m_loggedSecond = -1;
        QkAccountEvent("{\"event\":\"media-release\"}\n");
    }
    static DWORD WINAPI Run(void *value) {
        QkMediaPlayer *self = static_cast<QkMediaPlayer *>(value);
        HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (WaitForSingleObject(self->m_stop, 10) == WAIT_TIMEOUT) self->Poll();
        if (SUCCEEDED(apartment)) CoUninitialize(); return 0;
    }
    void Poll() {
        bool repaint = false;
        {
            QkFacadeSharedGuard shared;
            Guard guard(m_lock);
            if (!m_engine || m_failed) return;
            if (FAILED(m_state->DeviceStatus())) { Fail(DXGI_ERROR_DEVICE_REMOVED); ReleaseEngine(); return; }
            for (auto event : m_notify->Take()) {
                switch (event.kind) {
                case MF_MEDIA_ENGINE_EVENT_LOADEDDATA:
                    m_loaded = true;
                    m_engine->GetNativeVideoSize(&m_width, &m_height);
                    if (m_pendingSeek) { m_ex->SetCurrentTimeEx(m_seek, MF_MEDIA_ENGINE_SEEK_MODE_NORMAL); m_pendingSeek = false; }
                    m_engine->SetVolume(m_volume); m_ex->SetBalance(m_balance);
                    if (m_rate > 0) { m_engine->SetDefaultPlaybackRate(m_rate); m_engine->SetPlaybackRate(m_rate); m_engine->Play(); }
                    // 托管 MediaPlayerState 以 Prerolled（不是底层 Opened）公布 MediaOpened。
                    if (!m_opened) { m_opened = true; Trace("MediaOpened/Prerolled"); m_instance->GetMediaEventProxy().RaiseEvent(AVMediaPrerolled); }
                    repaint = true; break;
                case MF_MEDIA_ENGINE_EVENT_ENDED:
                    Trace("MediaEnded"); m_instance->GetMediaEventProxy().RaiseEvent(AVMediaEnded); break;
                case MF_MEDIA_ENGINE_EVENT_ERROR:
                    Fail(FAILED(event.hr) ? event.hr : E_FAIL); break;
                case MF_MEDIA_ENGINE_EVENT_WAITING:
                    if (!m_buffering) { m_buffering = true; m_instance->GetMediaEventProxy().RaiseEvent(AVMediaBufferingStarted); } break;
                case MF_MEDIA_ENGINE_EVENT_CANPLAY:
                case MF_MEDIA_ENGINE_EVENT_PLAYING:
                    if (m_buffering) { m_buffering = false; m_instance->GetMediaEventProxy().RaiseEvent(AVMediaBufferingEnded); } break;
                case MF_MEDIA_ENGINE_EVENT_SEEKED: repaint = m_rate > 0 || m_scrubbing || !m_frame; Trace("Seeked"); break;
                default: break;
                }
            }
            if (m_loaded && m_engine->HasVideo() && (m_rate > 0 || m_scrubbing || !m_frame)) {
                LONGLONG tick = 0;
                HRESULT hr = m_engine->OnVideoStreamTick(&tick);
                if (hr == S_OK && tick != m_tick) {
                    m_tick = tick; ++m_serial; repaint = true;
                    if (tick / 10000000 != m_loggedSecond) {
                        m_loggedSecond = tick / 10000000;
                        char action[96]; StringCchPrintfA(action, ARRAYSIZE(action), "frame pts=%.3f", tick / 10000000.); Trace(action);
                    }
                }
                else if (FAILED(hr)) Fail(hr);
            }
        }
        // 不持播放器锁进入 CompositionNotifier，避免通道注销与 BeginRender 的锁序相反。
        if (repaint) m_instance->GetCompositionNotifier().NotifyComposition();
    }
    QkMediaPlayer(MediaInstance *instance) : m_instance(instance) { InitializeCriticalSection(&m_lock); }
    ~QkMediaPlayer() {
        Shutdown(); if (m_startedMF) MFShutdown(); DeleteCriticalSection(&m_lock);
    }
public:
    static HRESULT Create(MediaInstance *instance, IMILMedia **out) {
        auto player = new QkMediaPlayer(instance);
        HRESULT hr = MFStartup(MF_VERSION);
        player->m_startedMF = SUCCEEDED(hr);
        if (SUCCEEDED(hr)) {
            player->m_stop = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            if (!player->m_stop) hr = HRESULT_FROM_WIN32(GetLastError());
        }
        if (SUCCEEDED(hr)) {
            player->m_thread = CreateThread(nullptr, 0, Run, player, 0, nullptr);
            if (!player->m_thread) hr = HRESULT_FROM_WIN32(GetLastError());
        }
        if (FAILED(hr)) { player->Release(); return hr; }
        *out = static_cast<IMILMedia *>(player); return S_OK;
    }
    STDMETHOD(QueryInterface)(REFIID id, void **out) override {
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IMILMedia) *out = static_cast<IMILMedia *>(this);
        else if (id == IID_IMILSurfaceRendererProvider) *out = static_cast<IMILSurfaceRendererProvider *>(this);
        else if (id == IID_IAVSurfaceRenderer) *out = static_cast<IAVSurfaceRenderer *>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_refs); }
    STDMETHOD_(ULONG, Release)() override { LONG n = InterlockedDecrement(&m_refs); if (!n) delete this; return n; }
    STDMETHOD(Open)(LPCWSTR url) override {
        // 与绘制一致：共享设备锁在播放器锁之前；回调从不进入这两个锁。
        QkFacadeSharedGuard shared; Guard guard(m_lock);
        ReleaseEngine(); m_failed = false; m_seek = 0; m_pendingSeek = false;
        HRESULT hr = InitEngine();
        BSTR source = SysAllocString(url);
        if (SUCCEEDED(hr)) hr = source ? m_engine->SetSource(source) : E_OUTOFMEMORY;
        SysFreeString(source);
        if (FAILED(hr)) { Fail(hr); ReleaseEngine(); }
        return S_OK; // 媒体打开失败通过 MediaFailed 异步送达，沿用托管契约。
    }
    STDMETHOD(Stop)() override { Guard guard(m_lock); m_rate = 0; m_seek = 0; m_pendingSeek = true; if (m_engine) { m_engine->Pause(); if (m_loaded) { m_engine->SetCurrentTime(0); m_pendingSeek = false; } } return S_OK; }
    STDMETHOD(Close)() override { QkFacadeSharedGuard shared; Guard guard(m_lock); Trace("Close"); ReleaseEngine(); m_rate = 0; m_seek = 0; return S_OK; }
    STDMETHOD(GetPosition)(LONGLONG *out) override { Guard guard(m_lock); *out = static_cast<LONGLONG>((m_engine && m_loaded && !m_pendingSeek ? m_engine->GetCurrentTime() : m_seek) * 10000000); return S_OK; }
    STDMETHOD(SetPosition)(LONGLONG value) override { Guard guard(m_lock); m_seek = max(0., value / 10000000.); m_pendingSeek = !m_loaded; HRESULT hr = m_loaded ? m_ex->SetCurrentTimeEx(m_seek, MF_MEDIA_ENGINE_SEEK_MODE_NORMAL) : S_OK; Trace("Seek", hr); return hr; }
    STDMETHOD(SetRate)(double value) override { Guard guard(m_lock); if (value < 0) return E_INVALIDARG; m_rate = value; if (!m_engine || !m_loaded) return S_OK; HRESULT hr = value == 0 ? m_engine->Pause() : m_engine->SetDefaultPlaybackRate(value); if (SUCCEEDED(hr) && value > 0) hr = m_engine->SetPlaybackRate(value); if (SUCCEEDED(hr) && value > 0) hr = m_engine->Play(); Trace(value ? "Play" : "Pause", hr); return hr; }
    STDMETHOD(SetVolume)(double value) override { Guard guard(m_lock); m_volume = value; return m_engine ? m_engine->SetVolume(value) : S_OK; }
    STDMETHOD(SetBalance)(double value) override { Guard guard(m_lock); m_balance = value; return m_ex ? m_ex->SetBalance(value) : S_OK; }
    STDMETHOD(SetIsScrubbingEnabled)(bool value) override { Guard guard(m_lock); m_scrubbing = value; return S_OK; }
    STDMETHOD(IsBuffering)(bool *out) override { Guard guard(m_lock); *out = m_buffering; return S_OK; }
    STDMETHOD(CanPause)(bool *out) override { *out = true; return S_OK; }
    STDMETHOD(GetDownloadProgress)(double *out) override {
        Guard guard(m_lock); *out = 0; if (!m_engine || !m_loaded) return S_OK;
        ComPtr<IMFMediaTimeRange> range; HRESULT hr = m_engine->GetBuffered(&range);
        double duration = m_engine->GetDuration(), end = 0;
        if (SUCCEEDED(hr) && range->GetLength() && duration > 0 && SUCCEEDED(range->GetEnd(range->GetLength()-1, &end))) *out = min(1., end / duration);
        return S_OK;
    }
    STDMETHOD(GetBufferingProgress)(double *out) override { return GetDownloadProgress(out); }
    STDMETHOD(HasVideo)(bool *out) override { Guard guard(m_lock); *out = m_engine && m_engine->HasVideo(); return S_OK; }
    STDMETHOD(HasAudio)(bool *out) override { Guard guard(m_lock); *out = m_engine && m_engine->HasAudio(); return S_OK; }
    STDMETHOD(GetNaturalWidth)(UINT *out) override { Guard guard(m_lock); *out = m_width; return S_OK; }
    STDMETHOD(GetNaturalHeight)(UINT *out) override { Guard guard(m_lock); *out = m_height; return S_OK; }
    STDMETHOD(GetMediaLength)(LONGLONG *out) override { Guard guard(m_lock); double duration = m_engine && m_loaded ? m_engine->GetDuration() : 0; *out = duration >= 0 && duration < 9e11 ? static_cast<LONGLONG>(duration * 10000000) : 0; return S_OK; }
    STDMETHOD(NeedUIFrameUpdate)() override { m_instance->GetCompositionNotifier().NeedUIFrameUpdate(); return S_OK; }
    STDMETHOD(Shutdown)() override {
        if (m_stop) SetEvent(m_stop);
        if (m_thread) { WaitForSingleObject(m_thread, INFINITE); CloseHandle(m_thread); m_thread = nullptr; }
        Close(); if (m_stop) { CloseHandle(m_stop); m_stop = nullptr; } return S_OK;
    }
    STDMETHOD(ProcessExitHandler)() override { m_instance->GetMediaEventProxy().Shutdown(); return Shutdown(); }
    STDMETHOD(GetSurfaceRenderer)(IAVSurfaceRenderer **out) override { *out = this; AddRef(); return S_OK; }
    STDMETHOD(RegisterResource)(CMilSlaveVideo *resource) override { return m_instance->GetCompositionNotifier().RegisterResource(resource); }
    STDMETHOD(UnregisterResource)(CMilSlaveVideo *resource) override { m_instance->GetCompositionNotifier().UnregisterResource(resource); return S_OK; }
    STDMETHOD(BeginComposition)(CMilSlaveVideo *, BOOL, BOOL, LONGLONG *last, BOOL *fresh) override {
        Guard guard(m_lock); *fresh = *last != m_serial; *last = m_serial; return S_OK;
    }
    STDMETHOD(BeginRender)(CD3DDeviceLevel1 *device, IWGXBitmapSource **out) override {
        *out = nullptr;
        QkFacadeSharedGuard shared; Guard guard(m_lock);
        if (!m_engine || !m_loaded || m_failed || !m_width || !m_height) return S_OK;
        // 注入失效也覆盖 DeviceStatus：不把旧代际帧交给新设备；明确失败并释放媒体代际。
        if (!device || FAILED(m_state->DeviceStatus()) || device->Facade()->device.Get() != m_state->device.Get()) {
            Fail(DXGI_ERROR_DEVICE_REMOVED); ReleaseEngine(); return S_OK;
        }
        CGuard<CD3DDeviceLevel1> deviceGuard(*device);
        HRESULT hr = S_OK;
        if (m_frame && m_frame->generation != device->Facade()->generation) {
            // 同一物理设备的显示器包装仍有独立资源代际，不能复用另一包装的缓存键。
            QkD3D11State::RetireMediaTexture(m_frame.Get());
            m_bitmap.Reset(); m_frame.Reset(); m_cachedDevice = nullptr; m_renderedSerial = -1;
        }
        if (!m_frame) {
            D3DSURFACE_DESC desc = {}; desc.Width = m_width; desc.Height = m_height; desc.Format = D3DFMT_A8R8G8B8;
            desc.Type = D3DRTYPE_TEXTURE; desc.Usage = D3DUSAGE_RENDERTARGET; desc.Pool = D3DPOOL_DEFAULT;
            hr = device->Facade()->CreateTexture(&desc, 1, &m_frame);
            if (SUCCEEDED(hr)) {
                char event[180]; StringCchPrintfA(event, ARRAYSIZE(event), "{\"event\":\"media-frame-create\",\"id\":%u,\"object\":\"%p\",\"bytes\":%llu}\n", m_instance->GetID(), m_frame->texture.Get(), static_cast<ULONGLONG>(m_width)*m_height*4); QkAccountEvent(event);
            }
        }
        if (SUCCEEDED(hr) && !m_bitmap) {
            m_bitmap.Attach(new QkMediaBitmap()); m_bitmap->AddRef();
            m_bitmap->storage.resize(static_cast<size_t>(m_width)*m_height*4);
            hr = m_bitmap->HrInit(m_width, m_height, MilPixelFormat::PBGRA32bpp, static_cast<UINT>(m_bitmap->storage.size()), m_bitmap->storage.data(), m_width*4);
        }
        RECT destination = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
        MFARGB border = {0,0,0,255};
        if (SUCCEEDED(hr) && m_renderedSerial != m_serial) {
            ComPtr<ID3DDeviceContextState> previous;
            auto state = device->Facade();
            state->context1->SwapDeviceContextState(nullptr, &previous);
            hr = m_engine->TransferVideoFrame(m_frame->texture.Get(), nullptr, &destination, &border);
            state->context1->SwapDeviceContextState(previous.Get(), nullptr);
            if (SUCCEEDED(hr)) m_renderedSerial = m_serial;
        }
        if (SUCCEEDED(hr) && m_cachedDevice != device->Facade()->device.Get()) {
            hr = CacheHwTextureOnBitmap(m_frame.Get(), m_bitmap.Get(), device);
            if (SUCCEEDED(hr)) { m_cachedDevice = device->Facade()->device.Get(); Trace("GPU frame cached"); }
        }
        if (SUCCEEDED(hr)) { *out = m_bitmap.Get(); (*out)->AddRef(); }
        else { Fail(hr); ReleaseEngine(); }
        return S_OK;
    }
    STDMETHOD(EndRender)() override { return S_OK; }
    STDMETHOD(EndComposition)(CMilSlaveVideo *) override { return S_OK; }
    STDMETHOD(GetContentRectF)(MilPointAndSizeF *out) override { Guard guard(m_lock); out->X = out->Y = 0; out->Width = static_cast<float>(m_width); out->Height = static_cast<float>(m_height); return S_OK; }
    STDMETHOD(GetContentRect)(MilPointAndSizeL *out) override { Guard guard(m_lock); out->X = out->Y = 0; out->Width = m_width; out->Height = m_height; return S_OK; }
};
