// S5m 诊断专用。只由门面 cpp 包含；缓冲落盘不在测量边界内部。
void QkS5mMetrics(ULONGLONG *cycles, LONGLONG *qpc);
static const WCHAR *QkS5mPath() {
    static WCHAR path[32768]={};
    static const bool init=[](){if(QkInterfaceEnabled()){DWORD n=GetEnvironmentVariableW(L"QUICKER_WPF_S5M_SEGMENTS",path,ARRAYSIZE(path));if(n>=ARRAYSIZE(path))path[0]=0;}return true;}();
    (void)init;return path;
}
struct QkS5mEvent {
    UINT kind,iteration,thread,visible; LONGLONG qpc; ULONGLONG cycles,privateBytes,gpu[2],window; HRESULT gpuHr[2];
    ULONGLONG metricCycles[9]; LONGLONG metricQpc[9];
};
static SRWLOCK qkS5mLock=SRWLOCK_INIT;
static QkS5mEvent qkS5mEvents[4096];
static UINT qkS5mCount=0,qkS5mDropped=0;
void QkS5mPoint(UINT kind,UINT iteration,HWND hwnd) {
    if(!QkInterfaceEnabled() || !QkS5mPath()[0])return;
    QkS5mEvent e={};e.kind=kind;e.iteration=iteration;e.thread=GetCurrentThreadId();e.visible=hwnd && IsWindowVisible(hwnd);e.window=reinterpret_cast<ULONG_PTR>(hwnd);
    e.qpc=QkFacadeClock();QueryThreadCycleTime(GetCurrentThread(),&e.cycles);QkS5mMetrics(e.metricCycles,e.metricQpc);
    e.gpuHr[0]=e.gpuHr[1]=E_PENDING;
    static const bool memoryEnabled=[](){WCHAR v[2]={};return GetEnvironmentVariableW(L"QUICKER_WPF_S5M_MEMORY",v,2)==1 && v[0]==L'1';}();
    if(memoryEnabled && (kind==3 || kind==6 || kind==7 || kind==10)) {
        PROCESS_MEMORY_COUNTERS_EX memory={};memory.cb=sizeof(memory);
        if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))e.privateBytes=memory.PrivateUsage;
        QkFacadeSharedGuard guard;
        // 同进程 DXGI CurrentUsage 是适配器级进程值；同适配器不重复相加。
        if(!qkSharedStates.empty()) {
            ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIAdapter3> adapter3;
            if(SUCCEEDED(qkSharedStates.front()->device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter.As(&adapter3))) {
                for(UINT i=0;i<2;++i){DXGI_QUERY_VIDEO_MEMORY_INFO info={};e.gpuHr[i]=adapter3->QueryVideoMemoryInfo(0,static_cast<DXGI_MEMORY_SEGMENT_GROUP>(i),&info);e.gpu[i]=info.CurrentUsage;}
            }
        }
    }
    AcquireSRWLockExclusive(&qkS5mLock);
    if(qkS5mCount<ARRAYSIZE(qkS5mEvents))qkS5mEvents[qkS5mCount++]=e;else ++qkS5mDropped;
    ReleaseSRWLockExclusive(&qkS5mLock);
}
void QkS5mFlush() {
    if(!QkInterfaceEnabled() || !QkS5mPath()[0])return;
    AcquireSRWLockExclusive(&qkS5mLock);
    HANDLE f=CreateFileW(QkS5mPath(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE) {
        char line[4096];DWORD written;
        for(UINT i=0;i<qkS5mCount;++i){
            auto &e=qkS5mEvents[i];char *p=line;size_t left=ARRAYSIZE(line);
            StringCchPrintfExA(p,left,&p,&left,0,"{\"kind\":%u,\"iteration\":%u,\"thread\":%u,\"visible\":%u,\"window\":%llu,\"qpc\":%lld,\"cycles\":%llu,\"privateBytes\":%llu,\"gpu\":[%llu,%llu],\"gpuHr\":[%ld,%ld],\"metricCycles\":[",e.kind,e.iteration,e.thread,e.visible,e.window,e.qpc,e.cycles,e.privateBytes,e.gpu[0],e.gpu[1],e.gpuHr[0],e.gpuHr[1]);
            for(UINT j=0;j<9;++j)StringCchPrintfExA(p,left,&p,&left,0,"%s%llu",j?",":"",e.metricCycles[j]);
            StringCchPrintfExA(p,left,&p,&left,0,"],\"metricQpc\":[");
            for(UINT j=0;j<9;++j)StringCchPrintfExA(p,left,&p,&left,0,"%s%lld",j?",":"",e.metricQpc[j]);
            StringCchPrintfA(p,left,"]}\n");WriteFile(f,line,static_cast<DWORD>(strlen(line)),&written,nullptr);
        }
        StringCchPrintfA(line,ARRAYSIZE(line),"{\"dropped\":%u}\n",qkS5mDropped);WriteFile(f,line,static_cast<DWORD>(strlen(line)),&written,nullptr);CloseHandle(f);qkS5mCount=0;qkS5mDropped=0;
    }
    ReleaseSRWLockExclusive(&qkS5mLock);
}
extern "C" __declspec(dllexport) void WINAPI QuickerS5mMark(UINT kind,UINT iteration) {
    if(kind==99)QkS5mFlush();else QkS5mPoint(kind,iteration);
}
