// Included by d3d11facade.cpp only, inside QUICKER_WPF_D3D11_FACADE.
#if QUICKER_WPF_D3D11_FACADE
namespace {
WCHAR qkRtPath[32768] = {};
volatile LONG qkRtPhase = 0;
volatile LONG64 qkRtNext = 0;
thread_local ULONG64 qkRtParent = 0;
SRWLOCK qkRtLock = SRWLOCK_INIT;
struct QkRtEvent {
    const char *name;
    std::string detail;
    LONGLONG start, end;
    ULONG64 cyclesBefore, cyclesAfter, id, parent, cpu100ns;
    DWORD tid;
    LONG phase;
};
// POD pointer: no static COM or diagnostic destructor / DllMain cleanup.
std::vector<QkRtEvent> *qkRtEvents = nullptr;
ULONG64 qkRtDropped = 0;
void QkRtAppend(QkRtEvent event)
{
    AcquireSRWLockExclusive(&qkRtLock);
    try {
        if (!qkRtEvents) { qkRtEvents = new std::vector<QkRtEvent>(); qkRtEvents->reserve(16384); }
        if (qkRtEvents->size() < 200000) qkRtEvents->push_back(std::move(event));
        else ++qkRtDropped;
    } catch (...) { ++qkRtDropped; } // Diagnostics must never change a rendering HRESULT.
    ReleaseSRWLockExclusive(&qkRtLock);
}
ULONG64 QkRtCpu()
{
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0;
    return (static_cast<ULONG64>(k.dwHighDateTime) << 32) + k.dwLowDateTime +
        (static_cast<ULONG64>(u.dwHighDateTime) << 32) + u.dwLowDateTime;
}
}
bool QkRtEnabled()
{
    static const bool enabled = []() {
        const DWORD n = GetEnvironmentVariableW(L"QUICKER_WPF_RT_TIMING_LOG", qkRtPath, ARRAYSIZE(qkRtPath));
        return n > 0 && n < ARRAYSIZE(qkRtPath);
    }();
    return enabled;
}
bool QkRtCapturing() { return QkRtEnabled() && InterlockedCompareExchange(&qkRtPhase, 0, 0) != 1; }
std::string QkRtQuote(const std::string &value)
{
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c >= 32) result += c;
    }
    return result + '"';
}
QkRtScope::QkRtScope(const char *label, const char *metadata, bool background)
    : active(background ? QkRtEnabled() : QkRtCapturing()), name(label)
{
    if (!active) return;
    try { detail = metadata; } catch (...) { active = false; return; }
    phase = InterlockedCompareExchange(&qkRtPhase, 0, 0);
    id = InterlockedIncrement64(&qkRtNext); parent = qkRtParent; qkRtParent = id;
    QueryThreadCycleTime(GetCurrentThread(), &cycles); start = QkFacadeClock();
}
QkRtScope::~QkRtScope()
{
    Stop();
}
void QkRtScope::Stop()
{
    if (!active) return;
    active = false;
    const LONGLONG end = QkFacadeClock(); ULONG64 after = 0;
    QueryThreadCycleTime(GetCurrentThread(), &after);
    qkRtParent = parent;
    QkRtAppend({name, std::move(detail), start, end, cycles, after, id, parent, 0, GetCurrentThreadId(), phase});
}
void QkRtPoint(const char *name, const char *detail)
{
    if (!QkRtEnabled()) return;
    ULONG64 cycles = 0; QueryThreadCycleTime(GetCurrentThread(), &cycles);
    const LONGLONG now = QkFacadeClock();
    try { QkRtAppend({name, detail, now, now, cycles, cycles, 0, qkRtParent, QkRtCpu(), GetCurrentThreadId(),
        InterlockedCompareExchange(&qkRtPhase, 0, 0)}); } catch (...) {}
}
// Called only by the experiment UI thread after the last measured Present.
// phase=1 disables high-frequency scopes during scrolling; background events remain available.
extern "C" __declspec(dllexport) void __cdecl QkRtTimingControl(int phase)
{
    if (!QkRtEnabled()) return;
    if (phase >= 0) { InterlockedExchange(&qkRtPhase, phase); QkRtPoint("probe.phase"); return; }
    AcquireSRWLockExclusive(&qkRtLock);
    HANDLE file = CreateFileW(qkRtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
        char header[200]; StringCchPrintfA(header, ARRAYSIZE(header),
            "{\"type\":\"header\",\"qpcFrequency\":%lld,\"dropped\":%llu,\"format\":1}\n", frequency.QuadPart, qkRtDropped);
        DWORD written; WriteFile(file, header, static_cast<DWORD>(strlen(header)), &written, nullptr);
        if (qkRtEvents) for (const auto &event : *qkRtEvents) {
            char numbers[700]; StringCchPrintfA(numbers, ARRAYSIZE(numbers),
                "{\"name\":\"%s\",\"tid\":%lu,\"phase\":%ld,\"id\":%llu,\"parent\":%llu,\"startQpc\":%lld,\"endQpc\":%lld,\"wallMs\":%.9f,\"cyclesBefore\":%llu,\"cyclesAfter\":%llu,\"cycles\":%llu,\"cpu100ns\":%llu,\"detail\":",
                event.name, event.tid, event.phase, event.id, event.parent, event.start, event.end,
                (event.end-event.start)*1000.0/frequency.QuadPart, event.cyclesBefore, event.cyclesAfter,
                event.cyclesAfter-event.cyclesBefore, event.cpu100ns);
            const std::string line = std::string(numbers) + event.detail + "}\n";
            WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&qkRtLock);
}
#endif
