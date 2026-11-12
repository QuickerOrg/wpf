#pragma once

// 正式 B 不编译这些探针；B 诊断副本须显式定义 QUICKER_WPF_S5I_TIMING。
#if QUICKER_WPF_D3D11_FACADE || QUICKER_WPF_S5I_TIMING
#define QK_INTERFACE_METRICS(X) \
    X(Texture,"texture-api") X(Srv,"srv-api") X(Rtv,"rtv-api") X(Query,"query-api") \
    X(FixedSource,"fixed-source") X(ShaderLookup,"shader-lookup") X(ShaderCompile,"shader-compile") \
    X(Draw,"draw-submit") X(Apply,"draw-apply") X(Present,"present-api") X(Trace,"trace") \
    X(TextureWrapper,"texture-wrapper") X(TextureRelease,"texture-release")
enum class QkInterfaceMetric {
#define QK_METRIC_ENUM(n,s) n,
    QK_INTERFACE_METRICS(QK_METRIC_ENUM)
#undef QK_METRIC_ENUM
    Count
};
bool QkInterfaceEnabled();
void QkInterfaceAdd(QkInterfaceMetric metric,LONGLONG qpc,ULONGLONG cycles);
HRESULT QkInterfaceAppend(char *&cursor,size_t &remaining);
class QkInterfaceScope {
    QkInterfaceMetric m_metric;
    bool m_active;
    LARGE_INTEGER m_start={};
    ULONGLONG m_cycles=0;
public:
    explicit QkInterfaceScope(QkInterfaceMetric metric):m_metric(metric),m_active(QkInterfaceEnabled()) {
        if(m_active) { QueryThreadCycleTime(GetCurrentThread(),&m_cycles); QueryPerformanceCounter(&m_start); }
    }
    void Stop() {
        if(!m_active) return;
        LARGE_INTEGER end; ULONGLONG cycles=0;
        QueryPerformanceCounter(&end); QueryThreadCycleTime(GetCurrentThread(),&cycles);
        QkInterfaceAdd(m_metric,end.QuadPart-m_start.QuadPart,cycles-m_cycles); m_active=false;
    }
    ~QkInterfaceScope() { Stop(); }
};
#define QK_INTERFACE_SCOPE(n) QkInterfaceScope qkInterfaceScope(QkInterfaceMetric::n)
template<class F> auto QkInterfaceCall(QkInterfaceMetric metric,F call)->decltype(call()) { QkInterfaceScope scope(metric); return call(); }
#define QK_INTERFACE_CALL(n,expr) QkInterfaceCall(QkInterfaceMetric::n,[&](){ return (expr); })
#else
#define QK_INTERFACE_SCOPE(n) ((void)0)
#define QK_INTERFACE_CALL(n,expr) (expr)
#endif
