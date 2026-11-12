// 聚合在内存中，不逐调用写文件。qpc 是墙钟，cycles 是当前线程硬件周期，不能换算成固定频率 CPU 毫秒。
#include "s5i-interface-timing.h"
#if QUICKER_WPF_D3D11_FACADE || QUICKER_WPF_S5I_TIMING
static volatile LONG64 qkInterfaceMetrics[static_cast<UINT>(QkInterfaceMetric::Count)][3]={};
bool QkInterfaceEnabled() {
    static const bool enabled=[]() { WCHAR value[2]={}; return GetEnvironmentVariableW(L"QUICKER_WPF_INTERFACE_TIMING",value,2)==1 && value[0]==L'1'; }();
    return enabled;
}
void QkInterfaceAdd(QkInterfaceMetric metric,LONGLONG qpc,ULONGLONG cycles) {
    auto &values=qkInterfaceMetrics[static_cast<UINT>(metric)];
    InterlockedIncrement64(&values[0]); InterlockedExchangeAdd64(&values[1],qpc); InterlockedExchangeAdd64(&values[2],cycles);
}
HRESULT QkInterfaceAppend(char *&cursor,size_t &remaining) {
    if(!QkInterfaceEnabled()) return S_OK;
    static const char *names[]={
#define QK_METRIC_NAME(n,s) s,
        QK_INTERFACE_METRICS(QK_METRIC_NAME)
#undef QK_METRIC_NAME
    };
    static const char *units[]={"count","qpc","cycles"};
    HRESULT hr=S_OK;
    for(UINT n=0;SUCCEEDED(hr) && n<ARRAYSIZE(names);++n) for(UINT u=0;SUCCEEDED(hr) && u<3;++u)
        hr=StringCchPrintfExA(cursor,remaining,&cursor,&remaining,0,",\"interface.%s.%s\":%lld",names[n],units[u],InterlockedCompareExchange64(&qkInterfaceMetrics[n][u],0,0));
    return hr;
}
#if !QUICKER_WPF_D3D11_FACADE
// 只存在于单独输出目录的 B 诊断副本，正式 B 原二进制保持冻结。
extern "C" __declspec(dllexport) HRESULT WINAPI QuickerFacadeReadCounters(char *buffer,UINT capacity) {
    if(!buffer || capacity<8192) return E_INVALIDARG;
    LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc);
    char *cursor=buffer; size_t remaining=capacity;
    HRESULT hr=StringCchPrintfExA(cursor,remaining,&cursor,&remaining,0,"{\"schema\":1,\"enabled\":%s,\"qpc\":%lld,\"counters\":{\"diagnostic-b\":0",QkInterfaceEnabled()?"true":"false",qpc.QuadPart);
    if(SUCCEEDED(hr)) hr=QkInterfaceAppend(cursor,remaining);
    if(SUCCEEDED(hr)) hr=StringCchPrintfA(cursor,remaining,"}}");
    return hr;
}
#endif
#endif
