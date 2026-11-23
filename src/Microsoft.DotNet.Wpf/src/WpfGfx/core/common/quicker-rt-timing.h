#pragma once

// EMOJI8: diagnostics only; no clock, cycle query, allocation or I/O when unset.
#if QUICKER_WPF_D3D11_FACADE
#include <string>
bool QkRtEnabled();
bool QkRtCapturing();
void QkRtPoint(const char *name, const char *detail = "{}");
std::string QkRtQuote(const std::string &value);
class QkRtScope
{
    bool active;
    const char *name;
    std::string detail;
    LONGLONG start = 0;
    ULONG64 cycles = 0, id = 0, parent = 0;
    LONG phase = 0;
public:
    explicit QkRtScope(const char *label, const char *metadata = "{}", bool background = false);
    ~QkRtScope();
    void Stop();
    QkRtScope(const QkRtScope &) = delete;
    QkRtScope &operator=(const QkRtScope &) = delete;
};
template<class F> auto QkRtCall(const char *name, F operation, const char *detail = "{}") -> decltype(operation())
{
    QkRtScope scope(name, detail);
    return operation();
}
#define QK_RT_JOIN_(a,b) a##b
#define QK_RT_JOIN(a,b) QK_RT_JOIN_(a,b)
#define QK_RT_SCOPE(name) QkRtScope QK_RT_JOIN(qkRtScope_,__LINE__)(name)
#define QK_RT_CALL(name,expr) QkRtCall(name,[&](){return (expr);})
#else
#define QK_RT_SCOPE(name)
#define QK_RT_CALL(name,expr) (expr)
#endif
