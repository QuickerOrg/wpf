#pragma once
// [DEBUG-wpf-bpu] 调用返回计数，不代表屏幕已显示。仅实验开关启用。
inline bool WheelRunCountersEnabled(){
 static const bool enabled=[](){WCHAR x[16]{};if(GetEnvironmentVariableW(L"QUICKER_GPU_RUN_COUNTERS",x,16)==1&&x[0]==L'1')return true;return GetEnvironmentVariableW(L"QUICKER_GPU_ACCOUNTING",x,16)==1&&x[0]==L'1';}();return enabled;
}
inline void WheelRecordSubmit(HWND hwnd,HRESULT hr,ULONG_PTR kind){
 if(!hwnd||!WheelRunCountersEnabled())return;
 LARGE_INTEGER now{};QueryPerformanceCounter(&now);
 ULONG_PTR count=reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,L"Quicker.WheelSubmitCount"))+1;
 SetPropW(hwnd,L"Quicker.WheelSubmitCount",reinterpret_cast<HANDLE>(count));
 SetPropW(hwnd,L"Quicker.WheelSubmitHr",reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(hr)));
 SetPropW(hwnd,L"Quicker.WheelSubmitKind",reinterpret_cast<HANDLE>(kind));
 SetPropW(hwnd,L"Quicker.WheelSubmitQpc",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(now.QuadPart)));
 ULONG_PTR state=reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,L"Quicker.WheelStateQpc"));
 if(state&&static_cast<ULONG_PTR>(now.QuadPart)>=state&&reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,L"Quicker.WheelFirstState"))!=state){
  SetPropW(hwnd,L"Quicker.WheelFirstHr",reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(hr)));
  SetPropW(hwnd,L"Quicker.WheelFirstKind",reinterpret_cast<HANDLE>(kind));
  SetPropW(hwnd,L"Quicker.WheelFirstQpc",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(now.QuadPart)));
  SetPropW(hwnd,L"Quicker.WheelFirstState",reinterpret_cast<HANDLE>(state)); // 最后发布这一代的完整记录。
 }
}
