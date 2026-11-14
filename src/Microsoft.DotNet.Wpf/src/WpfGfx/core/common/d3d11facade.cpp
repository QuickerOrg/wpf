#include "precomp.hpp"

#if QUICKER_WPF_D3D11_FACADE
#include <d3dcompiler.h>
#include <set>
#include <string>
#include <regex>
#include <sstream>
#include <psapi.h>
#include <dxgi1_4.h>

LONGLONG QkFacadeClock() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }
#include "s5i-interface-timing.h"
#include "d3d11facade-pool.inl"
#include "d3d11facade-counters.inl"
void QkFacadeTiming(const char *name, LONGLONG start, HRESULT result, UINT detail)
{
    WCHAR path[32768]; DWORD n=GetEnvironmentVariableW(L"QUICKER_FACADE_TIMING",path,ARRAYSIZE(path));
    if(!n || n>=ARRAYSIZE(path)) return;
    LONGLONG end=QkFacadeClock(); LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    char line[320]; StringCchPrintfA(line,ARRAYSIZE(line),
        "{\"name\":\"%s\",\"start\":%lld,\"ms\":%.6f,\"hr\":%ld,\"detail\":%u,\"thread\":%lu}\n",
        name,start,(end-start)*1000.0/frequency.QuadPart,result,detail,GetCurrentThreadId());
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return;
    DWORD written; WriteFile(file,line,static_cast<DWORD>(strlen(line)),&written,nullptr);CloseHandle(file);
}

// 分类只覆盖一次实际 Draw，嵌套调用退出时恢复，不改变绘制顺序。
struct QkFacadeBindingReset {
    QkD3D11State &state;
    explicit QkFacadeBindingReset(QkD3D11State &value):state(value) { state.InvalidateBindings(); }
    ~QkFacadeBindingReset() { state.InvalidateBindings(); }
};
struct QkFacadeDrawKind {
    UINT &slot; UINT saved;
    QkFacadeDrawKind(UINT &value, UINT kind):slot(value),saved(value) { slot=kind; }
    ~QkFacadeDrawKind() { slot=saved; }
};
// 消融开关只在进程启动时读取；正式 B 不编译此文件。
static bool QkFacadeVertexRingEnabled() {
    static const bool enabled=[](){WCHAR v[2]={};return !(GetEnvironmentVariableW(L"QUICKER_FACADE_DISABLE_VERTEX_RING",v,2)==1 && v[0]==L'1');}();return enabled;
}
static bool QkFacadeUnusedSamplerSkipEnabled() {
    static const bool enabled=[](){WCHAR v[2]={};return !(GetEnvironmentVariableW(L"QUICKER_FACADE_DISABLE_UNUSED_SAMPLER_SKIP",v,2)==1 && v[0]==L'1');}();return enabled;
}
static bool QkFacadeBindingCacheEnabled() {
    static const bool enabled=[](){WCHAR v[2]={};return !(GetEnvironmentVariableW(L"QUICKER_FACADE_DISABLE_BINDING_CACHE",v,2)==1 && v[0]==L'1');}();return enabled;
}
void QkFacadeTrace(const char *line)
{
    static const bool enabled=[]() {
        WCHAR value[2]={};
        DWORD n=GetEnvironmentVariableW(L"QUICKER_FACADE_TRACE",value,2);
        if(n==1) return value[0]==L'1';
        if(GetEnvironmentVariableW(L"QUICKER_FACADE_LOG",nullptr,0)) return true;
#if DBG
        return true;
#else
        return false;
#endif
    }();
    // 缺口及软件后备仍显式报告；Release 的逐 Draw trace 默认关闭。
    if(!enabled && strncmp(line,"QK-FACADE-UNIMPL",16)!=0 && !strstr(line,"software fallback")) return;
    QK_INTERFACE_SCOPE(Trace);
    { QK_INTERFACE_SCOPE(TraceDebug); OutputDebugStringA(line); }
    WCHAR path[32768] = {};
    DWORD n = GetEnvironmentVariableW(L"QUICKER_FACADE_LOG", path, ARRAYSIZE(path));
    if (!n || n >= ARRAYSIZE(path)) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}

// 诊断输出独立于逐 Draw 日志，避免性能测量启用高频渲染日志。
void QkAccountEvent(const char *event)
{
    WCHAR path[32768]; DWORD n=GetEnvironmentVariableW(L"QUICKER_FACADE_ACCOUNT",path,ARRAYSIZE(path));
    if(!n || n>=ARRAYSIZE(path)) return;
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return;
    DWORD written; WriteFile(f,event,static_cast<DWORD>(strlen(event)),&written,nullptr); CloseHandle(f);
}
static const GUID QkAccountGuid={0x50f93e2d,0x91a7,0x4d08,{0x9b,0xb2,0x5a,0x76,0xcd,0x63,0x20,0x13}};
class QkAccountToken : public IUnknown
{
    LONG refs=1;
public:
    const char *kind; ULONGLONG bytes; void *object; void *device;
    void Write(int delta) { QkCountResource(kind,delta);
        WCHAR path[2]; if(!GetEnvironmentVariableW(L"QUICKER_FACADE_ACCOUNT",path,ARRAYSIZE(path))) return;
        char line[320]; StringCchPrintfA(line,320,
        "{\"event\":\"resource\",\"object\":\"%p\",\"device\":\"%p\",\"kind\":\"%s\",\"bytes\":%llu,\"delta\":%d}\n",object,device,kind,bytes,delta); QkAccountEvent(line); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override { *out=nullptr; if(iid!=__uuidof(IUnknown)) return E_NOINTERFACE; *out=this; AddRef(); return S_OK; }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG n=InterlockedDecrement(&refs); if(!n) { Write(-1); delete this; } return n; }
};
void QkAccount(ID3D11DeviceChild *object,const char *kind,ULONGLONG bytes)
{
    WCHAR path[2]; if(!object || (!GetEnvironmentVariableW(L"QUICKER_FACADE_ACCOUNT",path,2) && !QkFacadeCountersEnabled())) return;
    UINT size=0; object->GetPrivateData(QkAccountGuid,&size,nullptr); if(size) return;
    auto token=new QkAccountToken; token->kind=kind; token->bytes=bytes; token->object=object;
    ID3D11Device *device=nullptr; object->GetDevice(&device); token->device=device; device->Release();
    token->Write(1); object->SetPrivateDataInterface(QkAccountGuid,token); token->Release();
}

HRESULT QkFacadeUnimpl(const char *name)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::set<std::string> reported;
    AcquireSRWLockExclusive(&lock);
    // 全窗口巡检保留重复命中，避免后续窗口被进程级去重掩盖；返回失败语义不变。
    WCHAR sweep[2] = {};
    bool repeat = GetEnvironmentVariableW(L"QUICKER_FACADE_WINDOW_SWEEP", sweep, ARRAYSIZE(sweep)) == 1 && sweep[0] == L'1';
    if (reported.insert(name).second || repeat)
    {
        std::string line = std::string("QK-FACADE-UNIMPL ") + name + "\n";
        QkFacadeTrace(line.c_str());
    }
    ReleaseSRWLockExclusive(&lock);
    return E_NOTIMPL;
}

static bool QkAccountEnabled() { WCHAR value[2]; return GetEnvironmentVariableW(L"QUICKER_FACADE_ACCOUNT",value,2)!=0; }
static void QkAccountBudget(ID3D11Device *device)
{
    if(!QkAccountEnabled()) return;
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIAdapter3> adapter3;
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter.As(&adapter3))) return;
    for(UINT i=0;i<2;++i) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info={}; HRESULT hr=adapter3->QueryVideoMemoryInfo(0,static_cast<DXGI_MEMORY_SEGMENT_GROUP>(i),&info);
        char line[320]; StringCchPrintfA(line,320,"{\"event\":\"budget\",\"device\":\"%p\",\"segment\":%u,\"usage\":%llu,\"budget\":%llu,\"hr\":%ld}\n",device,i,info.CurrentUsage,info.Budget,hr); QkAccountEvent(line);
    }
}

// 弱索引不延长设备寿命；移除的设备不会被新的显示器包装复用。
static INIT_ONCE qkSharedOnce=INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION qkSharedLock;
static std::vector<QkD3D11State*> qkSharedStates;
static std::map<ID3D11DeviceContext*,QkD3D11State*> qkActiveStates;
static std::vector<QkD3D11State*> qkEntryStack;
static ULONGLONG qkPresentSerial=0, qkTrimmedSerial=0;
#include "s5m-segments.inl"
void QkD3D11State::EnterShared() {
    InitOnceExecuteOnce(&qkSharedOnce,[](PINIT_ONCE,PVOID,PVOID*)->BOOL { InitializeCriticalSection(&qkSharedLock); return TRUE; },nullptr,nullptr);
    EnterCriticalSection(&qkSharedLock);
}
void QkD3D11State::LeaveShared() { LeaveCriticalSection(&qkSharedLock); }
void QkD3D11State::InvalidateTransientCache() {
    if(m_transientPool) m_transientPool->Invalidate();
    m_sparePresentQueries.clear(); m_presentQueries.clear();
}
HRESULT QkD3D11State::DeviceStatus() const {
    QkFacadeSharedGuard guard;
    HRESULT hr = FAILED(removedReason) ? removedReason : device->GetDeviceRemovedReason();
    if (FAILED(hr) && SUCCEEDED(removedReason)) {
        for(auto state:qkSharedStates) if(state->device.Get()==device.Get()) { state->removedReason=hr; state->InvalidateTransientCache(); }
        char line[220];StringCchPrintfA(line,ARRAYSIZE(line),"QK-FACADE native removed hr=0x%08x generation=%llu tick=%llu\n",hr,generation,GetTickCount64());QkFacadeTrace(line);
    }
    return FAILED(hr) ? WGXERR_DISPLAYSTATEINVALID : S_OK;
}
void QkD3D11State::ExperimentRemoval() {
    QkFacadeSharedGuard guard;
    static bool consumed = false;
    WCHAR path[32768] = {};
    DWORD n = GetEnvironmentVariableW(L"QUICKER_FACADE_EXPERIMENT_REMOVE_REQUEST", path, ARRAYSIZE(path));
    if (consumed || !n || n >= ARRAYSIZE(path) || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return;
    consumed = true;
    // 只污染本进程、同一实际设备的全部包装；旧包装存活也不得再次被复用。
    for (auto state : qkSharedStates) if (state->device.Get() == device.Get()) { state->removedReason = DXGI_ERROR_DEVICE_REMOVED; state->InvalidateTransientCache(); }
    char line[240]; StringCchPrintfA(line, ARRAYSIZE(line),
        "QK-FACADE experiment removed hr=0x%08x generation=%llu tick=%llu thread=%lu\n", DXGI_ERROR_DEVICE_REMOVED, generation, GetTickCount64(), GetCurrentThreadId());
    QkFacadeTrace(line);
}
void QkD3D11State::Activate() {
    if(!context1) return;
    auto &active=qkActiveStates[context.Get()];
    if(active==this) return;
    ComPtr<ID3DDeviceContextState> previous;
    context1->SwapDeviceContextState(savedContext.Get(),&previous);
    // 不同适配器的上下文各自持有状态；只保存同一即时上下文的前任。
    if(active) active->savedContext=previous;
    active=this;
}
// 嵌套进入另一个显示器设备后，返回时恢复外层上下文；临时引用保护析构路径。
void QkD3D11State::EnterState(QkD3D11State *state) {
    auto prior=state?qkActiveStates[state->context.Get()]:nullptr;
    if(prior) prior->AddRef();
    qkEntryStack.push_back(prior);
    if(state) { state->InvalidateBindings(); state->Activate(); }
}
void QkD3D11State::LeaveState() {
    auto prior=qkEntryStack.back(); qkEntryStack.pop_back();
    if(prior) { prior->Activate(); prior->Release(); }
}
void QkD3D11State::RetireMediaTexture(QkTextureHandle *texture)
{
    QkFacadeSharedGuard guard;
    // 关闭媒体时解除最后一帧的门面绑定及保存的上下文引用，避免等待其它窗口绘制才回收。
    for (auto state : qkSharedStates) {
        bool entered = false;
        for (UINT slot = 0; slot < ARRAYSIZE(state->m_textures); ++slot) {
            if (state->m_textures[slot].Get() != texture) continue;
            if (!entered) { EnterState(state); entered = true; }
            state->InvalidateBindings();
            state->m_textures[slot].Reset();
            ID3D11ShaderResourceView *empty = nullptr;
            state->context->PSSetShaderResources(slot, 1, &empty);
        }
        if (entered) {
            ComPtr<ID3DDeviceContextState> clean;
            state->context1->SwapDeviceContextState(nullptr, &clean);
            state->context1->SwapDeviceContextState(clean.Get(), nullptr);
            state->savedContext = clean;
            LeaveState();
        }
    }
}
QkD3D11State::~QkD3D11State() {
    EnterShared();
    InvalidateTransientCache();
    auto active=qkActiveStates.find(context.Get());
    if(active!=qkActiveStates.end() && active->second==this) { if(context) context->ClearState(); qkActiveStates.erase(active); }
    auto found=std::find(qkSharedStates.begin(),qkSharedStates.end(),this); if(found!=qkSharedStates.end()) qkSharedStates.erase(found);
    // 析构成员前主动清空绑定，避免 context 持有已释放包装的资源；不 Trim 活跃兄弟设备。
    bool last=true; for(auto state:qkSharedStates) if(state->device.Get()==device.Get()) last=false;
    if(last && context) { context->ClearState(); QK_INTERFACE_CALL(DriverFlush,context->Flush()); }
    LeaveShared();
}

static bool QkLayerDiagnosticFlag(const wchar_t *name)
{
    WCHAR value[4]={}; return GetEnvironmentVariableW(name,value,ARRAYSIZE(value))==1 && value[0]==L'1';
}
bool QkLayerNativeCopyEnabled()
{
    // S5l 正式 ARM 收益未达到停止线，默认保留 S5k；仅显式实验 opt-in 启用。
    static const bool enabled=QkLayerDiagnosticFlag(L"QUICKER_FACADE_ENABLE_LAYER_NATIVE_COPY") &&
        !QkLayerDiagnosticFlag(L"QUICKER_FACADE_DISABLE_LAYER_NATIVE_COPY"); return enabled;
}
// 只有登记与回调访问这些值，且始终持有 qkSharedLock；不持有设备或 UI 对象。
static PTP_TIMER qkHideTrimTimer=nullptr;
static ULONGLONG qkHideTrimArmedSerial=0, qkHideTrimDue=0;
static DWORD QkHideTrimDelay()
{
    static const DWORD delay=[]()->DWORD {
        WCHAR value[16]={}; DWORD n=GetEnvironmentVariableW(L"QUICKER_FACADE_HIDE_TRIM_DELAY_MS",value,ARRAYSIZE(value));
        if(!n || n>=ARRAYSIZE(value)) return 60000;
        ULONGLONG result=0;
        for(DWORD i=0;i<n;++i) {
            if(value[i]<L'0' || value[i]>L'9') return 60000;
            result=result*10+value[i]-L'0'; if(result>MAXDWORD) return 60000;
        }
        return static_cast<DWORD>(result);
    }();
    return delay;
}
static void QkSetHideTrimTimer(DWORD delay)
{
    LARGE_INTEGER due; due.QuadPart=-static_cast<LONGLONG>(delay)*10000;
    FILETIME time={due.LowPart,static_cast<DWORD>(due.HighPart)};
    SetThreadpoolTimer(qkHideTrimTimer,&time,0,0);
}
// 调用方已持共享锁；0 延迟与旧路径使用完全相同的枚举和回收动作。
static void QkTrimSharedDevicesIfIdle(bool delayed)
{
    QK_INTERFACE_SCOPE(HideTrim);
    bool visible=false;
    { QK_INTERFACE_SCOPE(S5mEnum);
    EnumWindows([](HWND hwnd,LPARAM arg)->BOOL {
        DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
        if(pid==GetCurrentProcessId() && IsWindowVisible(hwnd)) { *reinterpret_cast<bool*>(arg)=true; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&visible)); }
    if(visible) { if(delayed) QkFacadeCount(QkFacadeCounter::TrimCancelledVisible); return; }
    // 仅进程全部顶层窗口隐藏后的空闲边界回收驱动内部重命名/暂存存储。
    // Trim 保留应用持有的资源内容；不清空 WPF 缓存，不触发 GC，不等待或跳过动画帧。
    std::set<ID3D11Device*> trimmed;
    for(auto state:qkSharedStates) if(trimmed.insert(state->device.Get()).second) {
        ComPtr<IDXGIDevice3> dxgi;
        if(SUCCEEDED(state->device.As(&dxgi))) {
            PROCESS_MEMORY_COUNTERS_EX before={},after={}; before.cb=after.cb=sizeof(before);
            if(QkAccountEnabled()) GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));
            { QK_INTERFACE_SCOPE(S5mFlush); QK_INTERFACE_CALL(DriverFlush,state->context->Flush()); }
            { QK_INTERFACE_SCOPE(S5mTrim); dxgi->Trim(); }
            QkFacadeCount(QkFacadeCounter::TrimExecuted);
            QkS5mPoint(31);
            if(QkAccountEnabled()) GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
            char line[320]; StringCchPrintfA(line,320,"{\"event\":\"idle-trim\",\"device\":\"%p\",\"privateBefore\":%llu,\"privateAfter\":%llu}\n",state->device.Get(),static_cast<ULONGLONG>(before.PrivateUsage),static_cast<ULONGLONG>(after.PrivateUsage)); QkAccountEvent(line); QkAccountBudget(state->device.Get());
        }
    }
    qkTrimmedSerial=qkPresentSerial;
}

static void CALLBACK QkHideTrimCallback(PTP_CALLBACK_INSTANCE,void *,PTP_TIMER)
{
    QkFacadeSharedGuard guard;
    // SetThreadpoolTimer 不撤回已经排队的回调；旧回调不得提前消费重新登记的期限。
    ULONGLONG now=GetTickCount64();
    if(now<qkHideTrimDue) { QkSetHideTrimTimer(static_cast<DWORD>(qkHideTrimDue-now)); return; }
    if(qkPresentSerial==qkTrimmedSerial) return;
    QkFacadeCount(QkFacadeCounter::TrimFired);
    QkS5mPoint(30);
    if(qkPresentSerial!=qkHideTrimArmedSerial) {
        QkFacadeCount(QkFacadeCounter::TrimCancelledPresent); return;
    }
    QkTrimSharedDevicesIfIdle(true);
}
void QkD3D11State::TrimIfIdle()
{
    static const bool disabled=QkLayerDiagnosticFlag(L"QUICKER_FACADE_DISABLE_HIDE_TRIM");
    if(disabled) return;
    QkFacadeSharedGuard guard;
    if(qkPresentSerial==qkTrimmedSerial) return;
    DWORD delay=QkHideTrimDelay();
    if(!delay) { QkTrimSharedDevicesIfIdle(false); return; }
    if(!qkHideTrimTimer) {
        // 线程池可能在 FreeLibrary 后调用函数地址。固定模块到进程结束，从根上消除
        // 动态卸载和回调之间的竞态；也避免在 DllMain 的 loader lock 内等待驱动/线程池。
        // 仅延迟策略懒创建时固定；进程终止由 OS 终止工作线程并回收计时器，不等待 UI。
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&QkHideTrimCallback),&module)) return;
        qkHideTrimTimer=CreateThreadpoolTimer(QkHideTrimCallback,nullptr,nullptr);
        if(!qkHideTrimTimer) { QkFacadeTrace("QK-FACADE hide trim timer creation failed\n"); return; }
    }
    qkHideTrimArmedSerial=qkPresentSerial;
    qkHideTrimDue=GetTickCount64()+delay;
    QkSetHideTrimTimer(delay);
    QkFacadeCount(QkFacadeCounter::TrimArmed);
    QkS5mPoint(29);
}

HRESULT QkD3D11State::Create(HMONITOR monitor, QkD3D11State **out)
{
    struct Guard { Guard(){QkD3D11State::EnterShared();} ~Guard(){QkD3D11State::LeaveShared();} } guard;
    *out = nullptr;
    WCHAR disabled[4] = {};
    if (GetEnvironmentVariableW(L"QUICKER_FACADE_EXPERIMENT_NO_HARDWARE", disabled, ARRAYSIZE(disabled)) == 1 && disabled[0] == L'1') {
        QkFacadeTrace("QK-FACADE software fallback: experiment no hardware\n");
        return WGXERR_NO_HARDWARE_DEVICE;
    }
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIAdapter1> selected;
    for (UINT a = 0; ; ++a)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(a, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return hr;
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        for (UINT o = 0; ; ++o)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od;
            if (output && SUCCEEDED(output->GetDesc(&od)) && od.Monitor == monitor)
            { selected = adapter; break; }
        }
        if (selected) break;
    }
    if (!selected) return WGXERR_NO_HARDWARE_DEVICE;
    QkD3D11State *state = new QkD3D11State;
    if (!state) return E_OUTOFMEMORY;
    hr=selected->GetDesc(&state->adapter);
    if(FAILED(hr)) { state->Release(); return hr; }
    for(auto existing:qkSharedStates) {
        if(memcmp(&existing->adapter.AdapterLuid,&state->adapter.AdapterLuid,sizeof(LUID))==0 && SUCCEEDED(existing->DeviceStatus())) {
            state->device=existing->device; state->context=existing->context; state->featureLevel=existing->featureLevel; break;
        }
    }
    if(!state->device) {
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    PROCESS_MEMORY_COUNTERS_EX before={},after={}; before.cb=after.cb=sizeof(before);
    if(QkAccountEnabled()) GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),sizeof(before));
    hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &state->device, &state->featureLevel, &state->context);
    if(QkAccountEnabled()) GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),sizeof(after));
    char account[400]; StringCchPrintfA(account,400,"{\"event\":\"device-create\",\"device\":\"%p\",\"monitor\":\"%p\",\"privateBefore\":%llu,\"privateAfter\":%llu,\"hr\":%ld}\n",state->device.Get(),monitor,static_cast<ULONGLONG>(before.PrivateUsage),static_cast<ULONGLONG>(after.PrivateUsage),hr); QkAccountEvent(account);
    if (SUCCEEDED(hr)) hr = selected->GetDesc(&state->adapter);
    } else {
        char line[240]; StringCchPrintfA(line,240,"{\"event\":\"device-share\",\"device\":\"%p\",\"monitor\":\"%p\"}\n",state->device.Get(),monitor); QkAccountEvent(line);
    }
    if(SUCCEEDED(hr)) hr=state->context.As(&state->context1);
    ComPtr<ID3D11Device1> device1;
    if(SUCCEEDED(hr)) hr=state->device.As(&device1);
    if(SUCCEEDED(hr)) {
        D3D_FEATURE_LEVEL selectedLevel;
        hr=device1->CreateDeviceContextState(0,&state->featureLevel,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selectedLevel,&state->savedContext);
    }
    UINT support = 0;
    if (SUCCEEDED(hr)) hr = state->device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
    if (SUCCEEDED(hr) && (support & required) != required) hr = WGXERR_INSUFFICIENT_GPU_CAPS;
    if (FAILED(hr)) { state->Release(); return hr; }
    static volatile LONG64 nextGeneration = 0;
    state->generation = InterlockedIncrement64(&nextGeneration);
    state->m_monitor=monitor;
    qkSharedStates.push_back(state);
    *out = state;
    return S_OK;
}

void QkD3D11State::ReadCaps(D3DCAPS9 *caps, UINT *memory) const
{
    ZeroMemory(caps, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->Caps2 = D3DCAPS2_CANSHARERESOURCE | D3DCAPS2_CANAUTOGENMIPMAP;
    caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION;
    // 这是原算法的能力镜像；真正创建的 shader 固定为 SM4，绝不提交旧 token。
    caps->PixelShaderVersion = D3DPS_VERSION(2, 0);
    caps->VertexShaderVersion = D3DVS_VERSION(2, 0);
    caps->MaxTextureWidth = caps->MaxTextureHeight = featureLevel >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
    caps->MaxStreams = 1;
    caps->MaxTextureBlendStages = caps->MaxSimultaneousTextures = 8;
    caps->MaxPrimitiveCount = 0xFFFFF;
    caps->MaxVertexIndex = 0xFFFFFF;
    caps->MaxAnisotropy = 1;
    caps->RasterCaps = D3DPRASTERCAPS_SCISSORTEST;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_COLORWRITEENABLE;
    caps->SrcBlendCaps = caps->DestBlendCaps = D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_ZERO |
        D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_BLENDFACTOR;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT |
        D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_BORDER;
    caps->TextureOpCaps = D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_MODULATE;
    *memory = static_cast<UINT>(min(static_cast<SIZE_T>(UINT_MAX), adapter.DedicatedVideoMemory + adapter.SharedSystemMemory));
}

HRESULT QkReadFacadeCaps(HMONITOR monitor, D3DCAPS9 *caps, UINT *memory, LUID *luid)
{
    struct Guard { Guard(){QkD3D11State::EnterShared();} ~Guard(){QkD3D11State::LeaveShared();} } guard;
    struct Caps { D3DCAPS9 caps; UINT memory; LUID luid; };
    static SRWLOCK lock=SRWLOCK_INIT;
    static std::map<ULONGLONG,Caps> cached;
    static ComPtr<IDXGIFactory1> factory;
    AcquireSRWLockExclusive(&lock);
    if(!factory || !factory->IsCurrent()) {
        cached.clear(); factory.Reset();
        HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if(FAILED(hr)) { ReleaseSRWLockExclusive(&lock); return hr; }
    }
    ULONGLONG key=0; bool selected=false;
    for(UINT a=0;!selected;++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(a,&adapter)!=S_OK) break;
        for(UINT o=0;;++o) {
            ComPtr<IDXGIOutput> output;
            if(adapter->EnumOutputs(o,&output)!=S_OK) break;
            DXGI_OUTPUT_DESC desc;
            if(SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==monitor) {
                DXGI_ADAPTER_DESC1 ad; HRESULT hr=adapter->GetDesc1(&ad);
                if(FAILED(hr)) { ReleaseSRWLockExclusive(&lock); return hr; }
                memcpy(&key,&ad.AdapterLuid,sizeof(key)); selected=true; break;
            }
        }
    }
    if(!selected) { ReleaseSRWLockExclusive(&lock); return WGXERR_NO_HARDWARE_DEVICE; }
    auto found=cached.find(key);
    if(found!=cached.end()) { *caps=found->second.caps; *memory=found->second.memory; *luid=found->second.luid; ReleaseSRWLockExclusive(&lock); return S_OK; }
    QkD3D11State *state=nullptr;
    HRESULT hr=QkD3D11State::Create(monitor,&state);
    if(SUCCEEDED(hr)) {
        state->ReadCaps(caps,memory); *luid=state->adapter.AdapterLuid;
        cached[key]={*caps,*memory,*luid}; state->Release();
        QkFacadeTrace("QK-FACADE caps probe cached per adapter LUID\n");
    }
    ReleaseSRWLockExclusive(&lock); return hr;
}

HRESULT QkD3D11State::Initialize()
{
    struct Guard { Guard(){QkD3D11State::EnterShared();} ~Guard(){QkD3D11State::LeaveShared();} } guard;
    Activate();
    HRESULT hr=S_OK;
    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, &m_constants);
    if (FAILED(hr)) return hr;
    QkAccount(m_constants.Get(),"constant-buffer",16);
    cb.ByteWidth=sizeof(m_vsData); hr=device->CreateBuffer(&cb,nullptr,&m_vsConstants); if(FAILED(hr)) return hr;
    cb.ByteWidth=sizeof(m_psData); hr=device->CreateBuffer(&cb,nullptr,&m_psConstants); if(FAILED(hr)) return hr;
    QkAccount(m_vsConstants.Get(),"constant-buffer",sizeof(m_vsData)); QkAccount(m_psConstants.Get(),"constant-buffer",sizeof(m_psData));
    for (UINT i = 0; i < 2; ++i)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = i != 0;
        hr = device->CreateRasterizerState(&rd, &m_raster[i]);
        if(SUCCEEDED(hr)) QkAccount(m_raster[i].Get(),"raster-state");
        if (FAILED(hr)) return hr;
    }
    char identity[256];
    StringCchPrintfA(identity, ARRAYSIZE(identity), "QK-FACADE device hardware generation=%llu monitor=%p FL=0x%x vendor=0x%x device=0x%x shader=vs_4_0/ps_4_0\n",
        generation, m_monitor, featureLevel, adapter.VendorId, adapter.DeviceId);
    QkFacadeTrace(identity);
    return S_OK;
}

HRESULT QkFacadeBuffer::Lock(UINT offset, UINT count, void **data, DWORD flags)
{
    QkFacadeSharedGuard sharedGuard;
    *data = nullptr;
    if (m_mapped || offset > size || count > size-offset) return E_INVALIDARG;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = QK_INTERFACE_CALL(S5mMap,context->Map(buffer.Get(), 0, (flags & D3DLOCK_DISCARD) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped));
    if (SUCCEEDED(hr)) { m_mapped = true; *data = static_cast<BYTE *>(mapped.pData) + offset; }
    return hr;
}
HRESULT QkFacadeBuffer::Unlock()
{
    QkFacadeSharedGuard sharedGuard;
    if (!m_mapped) return E_INVALIDARG;
    context->Unmap(buffer.Get(), 0); m_mapped = false; return S_OK;
}
HRESULT QkD3D11State::CreateBuffer(UINT bytes, UINT bind, QkFacadeBuffer **out)
{
    *out = nullptr;
    QkFacadeBuffer *b = new QkFacadeBuffer;
    if (!b) return E_OUTOFMEMORY;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DYNAMIC; desc.BindFlags = bind; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&desc, nullptr, &b->buffer);
    if (FAILED(hr)) { b->Release(); return hr; }
    QkAccount(b->buffer.Get(),bind==D3D11_BIND_VERTEX_BUFFER?"dynamic-VB":"dynamic-IB",bytes);
    b->context = context; b->size = bytes; b->generation = generation; *out = b; return S_OK;
}
HRESULT QkD3D11State::BindTarget(ID3D11RenderTargetView *rtv, UINT w, UINT h, bool opaque, std::shared_ptr<QkOpaqueState> opaqueState)
{
    InvalidateBindings();
    ID3D11ShaderResourceView *empty[8]={}; context->PSSetShaderResources(0,8,empty);
    targetOpaqueState=opaqueState; target = rtv; width = w; height = h; targetOpaque = opaque;
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    return DeviceStatus();
}
HRESULT QkD3D11State::RecordPresent()
{
    QK_INTERFACE_SCOPE(PresentRecord);
    if(!m_loggedFirstPresent) {
        char line[180];StringCchPrintfA(line,ARRAYSIZE(line),"QK-FACADE first-present generation=%llu tick=%llu thread=%lu\n",generation,GetTickCount64(),GetCurrentThreadId());
        QkFacadeTrace(line);m_loggedFirstPresent=true;
    }
    ++qkPresentSerial;
    QkAccountBudget(device.Get());
    QkAccountEvent("{\"event\":\"present\"}\n");
    D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
    // 即使调用方没有查询排队数，也要回收已完成的 fence，不能随呈现次数积累。
    UINT queued; HRESULT hr=GetNumQueuedPresents(&queued); if(FAILED(hr)) return hr;
    ComPtr<ID3D11Query> query;
    if(!m_sparePresentQueries.empty()) { query=std::move(m_sparePresentQueries.back()); m_sparePresentQueries.pop_back(); QkFacadeCount(QkFacadeCounter::QueryReuse); }
    if(!query) { QK_INTERFACE_SCOPE(Query); hr=device->CreateQuery(&desc,&query); if(FAILED(hr)) return hr; QkAccount(query.Get(),"present-query"); }
    context->End(query.Get()); QK_INTERFACE_CALL(DriverFlush,context->Flush());
    m_presentQueries.push_back(query); return S_OK;
}
HRESULT QkD3D11State::GetNumQueuedPresents(UINT *count)
{
    LONGLONG started=QkFacadeClock();
    *count = 0;
    while (!m_presentQueries.empty())
    {
        BOOL done = FALSE;
        HRESULT hr = QK_INTERFACE_CALL(DriverQuery,context->GetData(m_presentQueries.front().Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH));
        if (FAILED(hr)) return hr;
        if (hr == S_FALSE || !done) break;
        if(m_sparePresentQueries.size()<16) m_sparePresentQueries.push_back(m_presentQueries.front());
        m_presentQueries.erase(m_presentQueries.begin());
    }
    // 与原 WPF GPU marker 同一含义：尚未完成的真实 GPU 命令队列，非 DWM 扫描输出队列。
    // PresentCount 可能跳帧，不能用 submitted-displayed 的差伪造排队数。
    *count=static_cast<UINT>(m_presentQueries.size());
    HRESULT status=DeviceStatus(); QkFacadeTiming("GetNumQueuedPresents",started,status,*count);
    return status;
}
HRESULT QkD3D11State::WaitForVBlank()
{
    QkFacadeCount(QkFacadeCounter::WaitForVBlank);
    // 默认关闭的诊断候选：借用 WPF 已有的无硬件 vblank 调度回退，不改托管时钟。
    WCHAR option[2]={};
    if(GetEnvironmentVariableW(L"QUICKER_FACADE_NO_VBLANK_WAIT",option,ARRAYSIZE(option))==1 && option[0]==L'1') {
        QkFacadeTrace("QK-FACADE diagnostic vblank-wait bypass\n");
        return WGXERR_NO_HARDWARE_DEVICE;
    }
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
    QkFacadeCount(QkFacadeCounter::OutputLookup);
    HRESULT hr=device.As(&dxgi);
    if(SUCCEEDED(hr)) hr=dxgi->GetAdapter(&adapter);
    if(FAILED(hr)) return WGXERR_NO_HARDWARE_DEVICE;
    for(UINT i=0;;++i) {
        ComPtr<IDXGIOutput> output; hr=adapter->EnumOutputs(i,&output);
        if(hr==DXGI_ERROR_NOT_FOUND) break;
        if(FAILED(hr)) return WGXERR_NO_HARDWARE_DEVICE;
        DXGI_OUTPUT_DESC desc={};
        if(SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==m_monitor) {
            LONGLONG started=QkFacadeClock();
            QkFacadeTiming("WaitForVBlank-enter",started,S_OK);
            hr=output->WaitForVBlank();
            QkFacadeTiming("WaitForVBlank",started,hr);
            return SUCCEEDED(hr)?S_OK:WGXERR_NO_HARDWARE_DEVICE;
        }
    }
    // RDP/无物理输出沿用原 WPF 的无硬件 vblank 结果，让原调度器处理。
    return WGXERR_NO_HARDWARE_DEVICE;
}
HRESULT QkD3D11State::SetRenderState(D3DRENDERSTATETYPE type, DWORD value)
{
    if (static_cast<UINT>(type) >= ARRAYSIZE(m_states)) return E_INVALIDARG;
    m_states[type] = value; return S_OK; // 缓存意图，Draw 时统一验证并提交。
}
HRESULT QkD3D11State::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= 8 || type >= 33) return E_INVALIDARG;
    m_stages[stage][type] = value; return S_OK;
}
HRESULT QkD3D11State::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (sampler >= 16 || type >= 14) return E_INVALIDARG;
    m_samplers[sampler][type] = value; return S_OK;
}
HRESULT QkD3D11State::SetTexture(DWORD n, QkBaseTextureHandle *p) {
    if(n>=8) return E_INVALIDARG;
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    m_textures[n]=p; return S_OK;
}
HRESULT QkD3D11State::SetVertexShader(QkVertexShaderHandle *p) {
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundVS=p; return S_OK;
}
HRESULT QkD3D11State::SetPixelShader(QkPixelShaderHandle *p) {
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    if(p && p->builtinResource) {
        char line[96];StringCchPrintfA(line,96,"QK-FACADE bind builtin PS resource=%u\n",p->builtinResource);QkFacadeTrace(line);
    }
    m_boundPS=p; return S_OK;
}
HRESULT QkD3D11State::SetDepthStencilSurface(QkSurfaceHandle *p) { return p ? QkFacadeUnimpl("SetDepthStencilSurface") : S_OK; }
HRESULT QkD3D11State::SetTransform(D3DTRANSFORMSTATETYPE type, const void *matrix)
{
    if (!matrix) return E_INVALIDARG;
    m_transforms[type] = *static_cast<const CBaseMatrix *>(matrix); return S_OK;
}
HRESULT QkD3D11State::SetVertexShaderConstantF(UINT start, const float *data, UINT count) {
    if(!data || start>256 || count>256-start) return E_INVALIDARG;
    if(memcmp(m_vsData+start,data,count*16)) { memcpy(m_vsData+start,data,count*16); m_vsDirty=true; } return S_OK;
}
HRESULT QkD3D11State::SetPixelShaderConstantF(UINT start, const float *data, UINT count) {
    if(!data || start>224 || count>224-start) return E_INVALIDARG;
    if(memcmp(m_psData+start,data,count*16)) { memcpy(m_psData+start,data,count*16); m_psDirty=true; } return S_OK;
}
HRESULT QkD3D11State::SetPixelShaderConstantI(UINT, const int *, UINT) { return QkFacadeUnimpl("SetPixelShaderConstantI"); }
HRESULT QkD3D11State::SetPixelShaderConstantB(UINT, const BOOL *, UINT) { return QkFacadeUnimpl("SetPixelShaderConstantB"); }
HRESULT QkD3D11State::SetMaterial(const D3DMATERIAL9 *material)
{
    if (!material) return E_INVALIDARG;
    m_material = *material; return S_OK; // Lighting=false 时保留材料，开启光照由 Draw 显式拒绝。
}
HRESULT QkD3D11State::SetStreamSource(UINT stream, QkVertexBufferHandle *b, UINT offset, UINT stride)
{
    if (stream) return QkFacadeUnimpl("SetStreamSource.nonzero");
    if (b && b->generation != generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundVB = b ? b->buffer : nullptr; m_offset = offset; m_stride = stride; return S_OK;
}
HRESULT QkD3D11State::SetIndices(QkIndexBufferHandle *b)
{
    if (b && b->generation != generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundIB = b ? b->buffer : nullptr; return S_OK;
}

static D3D11_BLEND QkBlend(DWORD value)
{
    switch (value) {
    case D3DBLEND_ZERO: return D3D11_BLEND_ZERO;
    case D3DBLEND_ONE: return D3D11_BLEND_ONE;
    case D3DBLEND_SRCALPHA: return D3D11_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_INVDESTALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
    case D3DBLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_COLOR;
    case D3DBLEND_BLENDFACTOR: return D3D11_BLEND_BLEND_FACTOR;
    case D3DBLEND_INVBLENDFACTOR: return D3D11_BLEND_INV_BLEND_FACTOR;
    default: return static_cast<D3D11_BLEND>(0); }
}
HRESULT QkD3D11State::Apply()
{
    if (!target || !width || !height) return E_UNEXPECTED;
    if (!(m_fvf & D3DFVF_DIFFUSE) || m_states[D3DRS_ZENABLE] || m_states[D3DRS_STENCILENABLE] || m_states[D3DRS_LIGHTING]) return QkFacadeUnimpl("Draw.nonSolidState");
    if (m_states[D3DRS_ALPHATESTENABLE] || m_states[D3DRS_FOGENABLE] || m_states[D3DRS_CLIPPLANEENABLE] ||
        m_states[D3DRS_SRGBWRITEENABLE] || m_states[D3DRS_SEPARATEALPHABLENDENABLE] ||
        m_states[D3DRS_CULLMODE] != D3DCULL_NONE || m_states[D3DRS_FILLMODE] != D3DFILL_SOLID ||
        m_states[D3DRS_BLENDOP] != D3DBLENDOP_ADD) return QkFacadeUnimpl("Draw.unsupportedRenderState");
    if ((m_fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ && (m_fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW)
        return QkFacadeUnimpl("Draw.vertexLayout");
    unsigned long long key = m_states[D3DRS_ALPHABLENDENABLE] | (static_cast<unsigned long long>(m_states[D3DRS_SRCBLEND]) << 8) |
        (static_cast<unsigned long long>(m_states[D3DRS_DESTBLEND]) << 16) | (static_cast<unsigned long long>(m_states[D3DRS_COLORWRITEENABLE]) << 24)
        | (static_cast<unsigned long long>(targetOpaque) << 32);
    auto found = m_blends.find(key);
    if (found == m_blends.end())
    {
        D3D11_BLEND_DESC bd = {};
        auto &b = bd.RenderTarget[0]; b.BlendEnable = !!m_states[D3DRS_ALPHABLENDENABLE];
        b.SrcBlend = b.SrcBlendAlpha = b.BlendEnable ? QkBlend(m_states[D3DRS_SRCBLEND]) : D3D11_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = b.BlendEnable ? QkBlend(m_states[D3DRS_DESTBLEND]) : D3D11_BLEND_ZERO;
        if (b.SrcBlendAlpha == D3D11_BLEND_INV_SRC_COLOR) b.SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (b.DestBlendAlpha == D3D11_BLEND_INV_SRC_COLOR) b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (!b.SrcBlend || !b.DestBlend) return QkFacadeUnimpl("Blend.factor");
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask = static_cast<UINT8>(m_states[D3DRS_COLORWRITEENABLE]);
        // XRGB 逻辑目标的 Alpha 恒为 1；物理 BGRA 仍供 flip GPU 拷贝与 GetDC 使用。
        if(targetOpaque) b.RenderTargetWriteMask &= ~D3D11_COLOR_WRITE_ENABLE_ALPHA;
        ComPtr<ID3D11BlendState> state;
        HRESULT hr = device->CreateBlendState(&bd, &state); if(SUCCEEDED(hr)) QkAccount(state.Get(),"blend-state"); if (FAILED(hr)) return hr;
        found = m_blends.insert(std::make_pair(key, state)).first;
    }
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr=S_OK;
    float size[4] = {static_cast<float>(m_viewport.Width), static_cast<float>(m_viewport.Height), static_cast<float>(m_viewport.X), static_cast<float>(m_viewport.Y)};
    if(!m_sizeValid || memcmp(size,m_sizeData,sizeof(size))) {
        QK_INTERFACE_SCOPE(SizeUpload);
        hr=QK_INTERFACE_CALL(S5mMap,context->Map(m_constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)); if(FAILED(hr)) return hr;
        memcpy(map.pData,size,sizeof(size)); context->Unmap(m_constants.Get(),0);
        memcpy(m_sizeData,size,sizeof(size)); m_sizeValid=true;
    }
    { QK_INTERFACE_SCOPE(RenderBindings);
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    DWORD blend=m_states[D3DRS_BLENDFACTOR];
    float factor[]={((blend>>16)&255)/255.f,((blend>>8)&255)/255.f,(blend&255)/255.f,(blend>>24)/255.f};
    context->OMSetBlendState(found->second.Get(), factor, UINT_MAX);
    context->OMSetDepthStencilState(nullptr, 0); // 没有 DSV，原 2D Z 关闭意图在上面验证。
    context->RSSetState(m_raster[!!m_states[D3DRS_SCISSORTESTENABLE]].Get());
    context->RSSetScissorRects(1, &m_scissor);
    D3D11_VIEWPORT vp = {static_cast<float>(m_viewport.X), static_cast<float>(m_viewport.Y), static_cast<float>(m_viewport.Width), static_cast<float>(m_viewport.Height), m_viewport.MinZ, m_viewport.MaxZ};
    context->RSSetViewports(1, &vp);
    }
    { QK_INTERFACE_SCOPE(ShaderApply); hr=ApplyShaders(); } if(FAILED(hr)) return hr;
    QK_INTERFACE_SCOPE(IaBindings);
    bool valid=m_iaValid && QkFacadeBindingCacheEnabled();
    if(!valid || m_appliedVB!=m_boundVB.Get() || m_appliedStride!=m_stride || m_appliedOffset!=m_offset) {
        context->IASetVertexBuffers(0,1,m_boundVB.GetAddressOf(),&m_stride,&m_offset);
        m_appliedVB=m_boundVB.Get();m_appliedStride=m_stride;m_appliedOffset=m_offset;
    }
    if(!valid || m_appliedIB!=m_boundIB.Get()) { context->IASetIndexBuffer(m_boundIB.Get(),DXGI_FORMAT_R16_UINT,0);m_appliedIB=m_boundIB.Get(); }
    m_iaValid=true;
    return S_OK;
}
HRESULT QkD3D11State::Draw(D3DPRIMITIVETYPE type, UINT vertices, UINT start)
{
    if (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP && type != D3DPT_LINELIST) return QkFacadeUnimpl("Draw.topology");
    HRESULT hr; { QK_INTERFACE_SCOPE(Apply); hr = Apply(); } if (FAILED(hr)) { char line[128]; StringCchPrintfA(line,128,"QK-FACADE Draw failed hr=0x%08x fvf=0x%x\n",hr,m_fvf); QkFacadeTrace(line); return hr; }
    context->IASetPrimitiveTopology(type == D3DPT_TRIANGLELIST ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : type == D3DPT_TRIANGLESTRIP ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    if(!vertices) { QK_INTERFACE_SCOPE(ZeroDraw); }
    { QkInterfaceScope category(static_cast<QkInterfaceMetric>(static_cast<UINT>(QkInterfaceMetric::HwDraw)+m_drawKind)); QK_INTERFACE_SCOPE(Draw); context->Draw(vertices, start); }
    QkFacadeTrace("QK-FACADE Draw original-hw\n");
    return DeviceStatus();
}
HRESULT QkD3D11State::DrawIndexed(UINT indices, UINT startIndex, INT baseVertex)
{
    HRESULT hr; { QK_INTERFACE_SCOPE(Apply); hr = Apply(); } if (FAILED(hr)) { char line[128]; StringCchPrintfA(line,128,"QK-FACADE Draw failed hr=0x%08x fvf=0x%x\n",hr,m_fvf); QkFacadeTrace(line); return hr; }
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); { QkInterfaceScope category(static_cast<QkInterfaceMetric>(static_cast<UINT>(QkInterfaceMetric::HwDraw)+m_drawKind)); QK_INTERFACE_SCOPE(Draw); context->DrawIndexed(indices, startIndex, baseVertex); }
    QkFacadeTrace("QK-FACADE DrawIndexed original-hw\n");
    return DeviceStatus();
}
HRESULT QkD3D11State::Upload(ComPtr<ID3D11Buffer> &buffer, UINT &capacity, UINT bind, const void *data, UINT bytes, UINT *offset)
{
    bool ring=offset && QkFacadeVertexRingEnabled();
    UINT required=ring?max(bytes,16384U):bytes;
    if (required > capacity)
    {
        m_vbCursor=0;
        D3D11_BUFFER_DESC bd = {}; // UP 通常只传几个顶点；WRITE_DISCARD 的重命名以整个容量计费。
        // 按实际上传量增长，避免为每次小绘制重命名至少 1 MiB。
        bd.ByteWidth = max(required, 256U); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = bind; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        buffer.Reset(); HRESULT hr = device->CreateBuffer(&bd, nullptr, &buffer); if (FAILED(hr)) return hr; capacity = bd.ByteWidth; QkAccount(buffer.Get(),bind==D3D11_BIND_VERTEX_BUFFER?"upload-VB":"upload-IB",bd.ByteWidth);
    }
    QK_INTERFACE_SCOPE(VertexUpload);
    UINT position=0;
    if(ring && m_vbCursor<=capacity && capacity-m_vbCursor>=15) {
        position=(m_vbCursor+15)&~15U;
        if(bytes>capacity-position) position=0;
    }
    D3D11_MAP mode=position?D3D11_MAP_WRITE_NO_OVERWRITE:D3D11_MAP_WRITE_DISCARD;
    D3D11_MAPPED_SUBRESOURCE map; HRESULT hr=QK_INTERFACE_CALL(S5mMap,context->Map(buffer.Get(),0,mode,0,&map));
    if(FAILED(hr)) return hr;
    memcpy(static_cast<BYTE*>(map.pData)+position,data,bytes);context->Unmap(buffer.Get(),0);
    if(offset) *offset=position;
    if(ring) m_vbCursor=position+bytes;
    QkFacadeCount(position?QkFacadeCounter::UploadNoOverwrite:QkFacadeCounter::UploadDiscard);
    return S_OK;
}
HRESULT QkD3D11State::DrawUP(D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride)
{
    if (!data || !stride || primitives > (UINT_MAX-2)/3) return E_INVALIDARG;
    UINT count = type == D3DPT_TRIANGLELIST ? primitives*3 : type == D3DPT_LINELIST ? primitives*2 : primitives+2;
    if (count > UINT_MAX/stride) return E_INVALIDARG;
    if (type == D3DPT_TRIANGLEFAN && count > 65536) return QkFacadeUnimpl("DrawUP.largeFan");
    UINT vertexOffset=0;
    HRESULT hr = Upload(m_uploadVB, m_vbCapacity, D3D11_BIND_VERTEX_BUFFER, data, count*stride,&vertexOffset); if (FAILED(hr)) return hr;
    auto savedVB = m_boundVB; auto savedIB = m_boundIB;
    UINT savedStride = m_stride, savedOffset = m_offset;
    m_boundVB = m_uploadVB; m_stride = stride; m_offset = vertexOffset;
    if (type == D3DPT_TRIANGLEFAN)
    {
        std::vector<WORD> indices(primitives*3);
        for (UINT i=0;i<primitives;++i) { indices[i*3]=0; indices[i*3+1]=static_cast<WORD>(i+1); indices[i*3+2]=static_cast<WORD>(i+2); }
        hr = Upload(m_uploadIB, m_ibCapacity, D3D11_BIND_INDEX_BUFFER, indices.data(), primitives*3*sizeof(WORD));
        if (SUCCEEDED(hr)) { m_boundIB = m_uploadIB; hr = DrawIndexed(primitives*3, 0, 0); }
    }
    else { hr = Draw(type, count, 0); }
    // UP 上传是临时绑定；不能污染原状态管理器缓存的 VB/IB 身份。
    m_boundVB = savedVB; m_boundIB = savedIB; m_stride = savedStride; m_offset = savedOffset;
    return hr;
}
HRESULT QkD3D11State::DrawIndexedUP(UINT vertices, UINT primitives, const WORD *indices, const void *data, UINT stride)
{
    if (!indices || !data || !stride || vertices > UINT_MAX/stride || primitives > UINT_MAX/(3*sizeof(WORD))) return E_INVALIDARG;
    UINT vertexOffset=0;
    HRESULT hr = Upload(m_uploadVB, m_vbCapacity, D3D11_BIND_VERTEX_BUFFER, data, vertices*stride,&vertexOffset); if (FAILED(hr)) return hr;
    hr = Upload(m_uploadIB, m_ibCapacity, D3D11_BIND_INDEX_BUFFER, indices, primitives*3*sizeof(WORD)); if (FAILED(hr)) return hr;
    auto savedVB = m_boundVB; auto savedIB = m_boundIB;
    UINT savedStride = m_stride, savedOffset = m_offset;
    m_boundVB = m_uploadVB; m_boundIB = m_uploadIB; m_stride = stride; m_offset = vertexOffset;
    hr = DrawIndexed(primitives*3, 0, 0);
    m_boundVB = savedVB; m_boundIB = savedIB; m_stride = savedStride; m_offset = savedOffset;
    return hr;
}
HRESULT QkD3D11State::Clear(DWORD flags, DWORD color, UINT count, const D3DRECT *rects)
{
    QK_INTERFACE_SCOPE(ClearTotal);
    QkFacadeDrawKind drawKind(m_drawKind,1);
    if (flags != D3DCLEAR_TARGET) return QkFacadeUnimpl("Clear.depthStencil");
    if (!target) return E_UNEXPECTED;
    float rgba[] = {((color>>16)&255)/255.f, ((color>>8)&255)/255.f, (color&255)/255.f, (color>>24)/255.f};
    // D3D9 Clear 受 viewport/scissor 约束；D3D11 ClearRenderTargetView 不受约束。
    // 局部文字更新时必须保留脏区外内容。
    RECT clip={static_cast<LONG>(m_viewport.X),static_cast<LONG>(m_viewport.Y),
        static_cast<LONG>(m_viewport.X+m_viewport.Width),static_cast<LONG>(m_viewport.Y+m_viewport.Height)};
    if(m_states[D3DRS_SCISSORTESTENABLE]) IntersectRect(&clip,&clip,&m_scissor);
    RECT bounds={0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    if(!IntersectRect(&clip,&clip,&bounds)) return S_OK;
    if(!count && EqualRect(&clip,&bounds)) {
        if(FAILED(DeviceStatus())) return WGXERR_DISPLAYSTATEINVALID;
        if(targetOpaque && QkLayerNativeCopyEnabled()) rgba[3]=1;
        context->ClearRenderTargetView(target.Get(), rgba);
        HRESULT hr=DeviceStatus();
        if(targetOpaqueState) targetOpaqueState->alphaKnownOpaque=SUCCEEDED(hr) && rgba[3]==1;
        return hr;
    }
    D3DRECT all={clip.left,clip.top,clip.right,clip.bottom};
    if(!count) {rects=&all;count=1;}
    auto savedVS=m_boundVS; auto savedPS=m_boundPS;
    DWORD savedStages[8][33]; memcpy(savedStages,m_stages,sizeof(m_stages));
    m_boundVS.Reset(); m_boundPS.Reset();
    m_stages[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG1; m_stages[0][D3DTSS_COLORARG1]=D3DTA_DIFFUSE;
    m_stages[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1; m_stages[0][D3DTSS_ALPHAARG1]=D3DTA_DIFFUSE;
    m_stages[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;
    DWORD saved[210]; memcpy(saved, m_states, sizeof(saved)); DWORD fvf = m_fvf; D3DVIEWPORT9 vp = m_viewport;
    auto vb = m_boundVB; auto ib = m_boundIB; UINT stride = m_stride, offset = m_offset;
    m_states[D3DRS_ALPHABLENDENABLE]=FALSE; m_states[D3DRS_SCISSORTESTENABLE]=FALSE; m_states[D3DRS_COLORWRITEENABLE]=15;
    m_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE; m_viewport = {0,0,width,height,0,1};
    struct V { float x,y,z; DWORD c; };
    HRESULT hr = S_OK;
    for (UINT i=0; i<count && SUCCEEDED(hr); ++i)
    {
        RECT requested={rects[i].x1,rects[i].y1,rects[i].x2,rects[i].y2},actual;
        if(!IntersectRect(&actual,&requested,&clip)) continue;
        float l=static_cast<float>(actual.left),t=static_cast<float>(actual.top),r=static_cast<float>(actual.right),b=static_cast<float>(actual.bottom);
        V v[] = {{l,t,0,color},{r,t,0,color},{l,b,0,color},{l,b,0,color},{r,t,0,color},{r,b,0,color}};
        hr = DrawUP(D3DPT_TRIANGLELIST, 2, v, sizeof(V));
    }
    m_boundVS=savedVS; m_boundPS=savedPS; memcpy(m_stages,savedStages,sizeof(m_stages));
    memcpy(m_states, saved, sizeof(saved)); m_fvf=fvf; m_viewport=vp; m_boundVB=vb; m_boundIB=ib; m_stride=stride; m_offset=offset;
    return hr;
}
#include "d3d11facade-resources.inl"
#endif
