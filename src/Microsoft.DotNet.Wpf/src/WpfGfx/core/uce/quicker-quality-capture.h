#pragma once
// 仅显式质量进程：按真实 HWND/token 在 Present 边界捕获，不依赖全局最后一个窗口。
// 正式采样无此环境变量；不会读回图像或修改 dump 路径。
class QkQualityCaptureScope {
 bool observe=false;LONGLONG beginQpc=0,apiQpc=0;
 static LONGLONG Qpc(){LARGE_INTEGER q;QueryPerformanceCounter(&q);return q.QuadPart;}
 void Record(const char* kind,HRESULT hr){if(!observe)return;WCHAR root[768],file[1024];if(!GetEnvironmentVariableW(L"QUICKER_G0G_CAPTURE_DIR",root,768))return;_snwprintf_s(file,1024,_TRUNCATE,L"%s/present-%p.csv",root,window);FILE* f=nullptr;_wfopen_s(&f,file,L"a");if(f){fprintf(f,"%s,%lld,%lld,%lld,%llu,%llu,%lu,%llu,%llu,%llu,%llu,%llu\n",kind,beginQpc,apiQpc,Qpc(),static_cast<UINT64>(CComposition::GetFrameLastComposed()),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(token)),static_cast<DWORD>(hr),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.WheelSubmitCount"))),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.NativePresents"))),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.PortGlyphCalls"))),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.WpfGpuSceneTarget"))),static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.WpfGpuSuppressCount"))));fclose(f);}}
 HWND window=nullptr;HANDLE token=nullptr;bool wanted=false;DWORD oldSize=0;WCHAR path[1024],oldPath[1024];
public:
 explicit QkQualityCaptureScope(HWND hwnd):window(hwnd){
  static const bool tracing=[](){WCHAR v[8];return GetEnvironmentVariableW(L"QUICKER_G0H_TIMELINE",v,8)==1&&v[0]==L'1';}();observe=tracing;beginQpc=observe?Qpc():0;
  static const bool enabled=[](){WCHAR flag[2];return GetEnvironmentVariableW(L"QUICKER_G0G_CAPTURE_DIR",flag,2)>0;}();if(!enabled)return;
  token=GetPropW(hwnd,L"Quicker.QualityCaptureToken");if(!token||token==GetPropW(hwnd,L"Quicker.QualityCapturedToken"))return;
  WCHAR root[768];DWORD n=GetEnvironmentVariableW(L"QUICKER_G0G_CAPTURE_DIR",root,768);if(!n||n>=768)return;
  _snwprintf_s(path,1024,_TRUNCATE,L"%s/%p-%llu.qkbg",root,hwnd,static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(token)));
  oldSize=GetEnvironmentVariableW(L"QUICKER_PG_D3D9_DUMP",oldPath,1024);if(oldSize>=1024)return;
  wanted=SetEnvironmentVariableW(L"QUICKER_PG_D3D9_DUMP",path)!=FALSE;
 }
 void Finish(HRESULT hr){
  if(observe){apiQpc=Qpc();ULONG_PTR count=reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.WheelSubmitCount"));if(SUCCEEDED(hr)&&count!=reinterpret_cast<ULONG_PTR>(GetPropW(window,L"Quicker.G0hLastSubmitCount"))){SetPropW(window,L"Quicker.G0hLastSubmitCount",reinterpret_cast<HANDLE>(count));SetPropW(window,L"Quicker.G0hLastSubmitQpc",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(apiQpc)));}Record("present",hr);}
  if(!wanted||FAILED(hr))return;
  WCHAR flag[8];if(GetEnvironmentVariableW(L"QUICKER_WPF_NATIVE_CONTRACT",flag,8)==1&&flag[0]==L'1'){
   auto module=GetModuleHandleW(L"Quicker.WpfD3D11Gpu.dll");typedef HRESULT (__cdecl *Dump)(HWND,const WCHAR*);auto dump=module?reinterpret_cast<Dump>(GetProcAddress(module,"QkPortDump")):nullptr;if(!dump||FAILED(dump(window,path)))return;
  }
  WIN32_FILE_ATTRIBUTE_DATA info{};if(GetFileAttributesExW(path,GetFileExInfoStandard,&info)&&(info.nFileSizeHigh||info.nFileSizeLow>16)){SetPropW(window,L"Quicker.QualityCapturedToken",token);SetPropW(window,L"Quicker.G0hCaptureApiQpc",reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(apiQpc)));Record("capture",hr);}
 }
 ~QkQualityCaptureScope(){if(wanted)SetEnvironmentVariableW(L"QUICKER_PG_D3D9_DUMP",oldSize?oldPath:nullptr);}
};
