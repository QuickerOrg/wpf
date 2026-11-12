// 仅由门面变体包含。环境变量必须在启动前设为 1；进程内不动态切换统计状态。
static volatile LONG64 qkFacadeCounters[static_cast<UINT>(QkFacadeCounter::Count)] = {};
bool QkFacadeCountersEnabled()
{
    static const bool enabled=[]() {
        WCHAR value[2]={};
        return GetEnvironmentVariableW(L"QUICKER_FACADE_COUNTERS",value,ARRAYSIZE(value))==1 && value[0]==L'1';
    }();
    return enabled;
}
void QkFacadeCount(QkFacadeCounter counter,ULONGLONG amount)
{
    if(QkFacadeCountersEnabled()) InterlockedExchangeAdd64(&qkFacadeCounters[static_cast<UINT>(counter)],static_cast<LONG64>(amount));
}
static void QkCountResource(const char *kind,int delta)
{
    if(!QkFacadeCountersEnabled()) return;
    QkFacadeCount(delta>0?QkFacadeCounter::ResourceCreate:QkFacadeCounter::ResourceRelease);
    QkFacadeCounter create;
    if(strstr(kind,"shader")) create=QkFacadeCounter::ShaderCreate;
    else if(strstr(kind,"state") || strcmp(kind,"input-layout")==0) create=QkFacadeCounter::StateCreate;
    else if(strstr(kind,"buffer") || strstr(kind,"-VB") || strstr(kind,"-IB")) create=QkFacadeCounter::BufferCreate;
    else if(strstr(kind,"query")) create=QkFacadeCounter::QueryCreate;
    else if(strcmp(kind,"SRV")==0 || strcmp(kind,"RTV")==0) create=QkFacadeCounter::ViewCreate;
    else create=QkFacadeCounter::TextureCreate;
    QkFacadeCount(static_cast<QkFacadeCounter>(static_cast<UINT>(create)+(delta>0?0:1)));
}

// 私有实验导出，不进入 WPF 托管公共 API。只复制单调计数；不暂停渲染、不刷新 GPU、不分配资源。
extern "C" __declspec(dllexport) HRESULT WINAPI QuickerFacadeReadCounters(char *buffer,UINT capacity)
{
    if(!buffer || capacity<4096) return E_INVALIDARG;
    static const char *names[]={
#define QK_COUNTER_LABEL(name, label) label,
        QK_FACADE_COUNTER_LIST(QK_COUNTER_LABEL)
#undef QK_COUNTER_LABEL
    };
    char *cursor=buffer; size_t remaining=capacity;
    HRESULT hr=StringCchPrintfExA(cursor,remaining,&cursor,&remaining,0,
        "{\"schema\":1,\"enabled\":%s,\"qpc\":%lld,\"counters\":{",QkFacadeCountersEnabled()?"true":"false",QkFacadeClock());
    for(UINT n=0;SUCCEEDED(hr) && n<ARRAYSIZE(names);++n)
        hr=StringCchPrintfExA(cursor,remaining,&cursor,&remaining,0,"%s\"%s\":%lld",n?",":"",names[n],InterlockedCompareExchange64(&qkFacadeCounters[n],0,0));
    if(SUCCEEDED(hr)) hr=StringCchPrintfA(cursor,remaining,"}}");
    return hr;
}
