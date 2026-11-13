// 聚合在内存中，不逐调用写文件。qpc 是墙钟，cycles 是当前线程硬件周期，不能换算成固定频率 CPU 毫秒。
#include "s5i-interface-timing.h"
#if QUICKER_WPF_D3D11_FACADE || QUICKER_WPF_S5I_TIMING

// S5k 独立栈/格式诊断：仅在显式文件开关下采样，在夹具读计数的边界外落盘。
// 正式性能进程不开此开关；有界缓冲溢出会写入 dropped，汇总必须拒收。
static SRWLOCK qkS5kLock=SRWLOCK_INIT;
struct QkS5kEvent { LONGLONG qpc; UINT thread,kind,count,values[20],depth; ULONG_PTR stack[12]; };
static QkS5kEvent qkS5kEvents[1024];
static UINT qkS5kCount=0,qkS5kDropped=0;
static const WCHAR *QkS5kPath() {
    static WCHAR path[32768]={};
    static const bool init=[](){DWORD n=GetEnvironmentVariableW(L"QUICKER_WPF_S5K_DETAILS",path,ARRAYSIZE(path));if(n>=ARRAYSIZE(path))path[0]=0;return true;}();
    (void)init;return path;
}
void QkS5kRecord(UINT kind,const UINT *values,UINT count) {
    if(!QkS5kPath()[0])return;
    QkS5kEvent e={};LARGE_INTEGER q;QueryPerformanceCounter(&q);e.qpc=q.QuadPart;
    e.kind=kind;e.thread=GetCurrentThreadId();e.count=min(count,20U);memcpy(e.values,values,e.count*sizeof(UINT));
    void *frames[12]={};e.depth=CaptureStackBackTrace(1,12,frames,nullptr);
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&QkS5kRecord),&module);
    for(UINT i=0;i<e.depth;++i)e.stack[i]=reinterpret_cast<ULONG_PTR>(frames[i])-reinterpret_cast<ULONG_PTR>(module);
    AcquireSRWLockExclusive(&qkS5kLock);
    if(qkS5kCount<ARRAYSIZE(qkS5kEvents))qkS5kEvents[qkS5kCount++]=e;else ++qkS5kDropped;
    ReleaseSRWLockExclusive(&qkS5kLock);
}
void QkS5kFlush() {
    if(!QkS5kPath()[0])return;
    AcquireSRWLockExclusive(&qkS5kLock);
    HANDLE f=CreateFileW(QkS5kPath(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE) {
        char line[2048];DWORD written;
        for(UINT i=0;i<qkS5kCount;++i) {
            auto &e=qkS5kEvents[i];char *p=line;size_t left=ARRAYSIZE(line);
            StringCchPrintfExA(p,left,&p,&left,0,"{\"qpc\":%lld,\"thread\":%u,\"kind\":%u,\"values\":[",e.qpc,e.thread,e.kind);
            for(UINT j=0;j<e.count;++j)StringCchPrintfExA(p,left,&p,&left,0,"%s%u",j?",":"",e.values[j]);
            StringCchPrintfExA(p,left,&p,&left,0,"],\"stack\":[");
            for(UINT j=0;j<e.depth;++j)StringCchPrintfExA(p,left,&p,&left,0,"%s%llu",j?",":"",static_cast<unsigned long long>(e.stack[j]));
            StringCchPrintfA(p,left,"]}\n");WriteFile(f,line,static_cast<DWORD>(strlen(line)),&written,nullptr);
        }
        StringCchPrintfA(line,ARRAYSIZE(line),"{\"dropped\":%u}\n",qkS5kDropped);WriteFile(f,line,static_cast<DWORD>(strlen(line)),&written,nullptr);CloseHandle(f);
        qkS5kCount=0;qkS5kDropped=0;
    }
    ReleaseSRWLockExclusive(&qkS5kLock);
}

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
    QkS5kFlush();
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
