#pragma once
// [DEBUG-wpf-uw] 进程级实验计数；单独统计实际等待，不能以Dwm结果代替成功等待。
__declspec(selectany) volatile LONG64 g_wheelPacing[20] = {};
inline bool WheelPacingCountersEnabled(){static const bool v=[](){WCHAR x[8]{};return GetEnvironmentVariableW(L"QUICKER_GPU_PACING_COUNTERS",x,8)==1&&x[0]==L'1';}();return v;}
inline bool WheelWpfPacingEnabled(){static const bool v=[](){WCHAR x[16]{};return GetEnvironmentVariableW(L"QUICKER_GPU_WPF_PACING",x,16)==3&&wcscmp(x,L"dwm")==0;}();return v;}
inline void WheelPacingAdd(int i,LONG64 n=1){if(WheelPacingCountersEnabled())InterlockedExchangeAdd64(&g_wheelPacing[i],n);}
inline HRESULT WheelPacingDeviceResult(HRESULT hr){WheelPacingAdd(11);if(SUCCEEDED(hr))WheelPacingAdd(12);return hr;}
