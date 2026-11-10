// 仅显式诊断导出：复用原 WPF realization 字形 alpha，默认完全关闭。
#pragma once
inline void QkCaptureGlyphAlpha(const BYTE* data,UINT size,const RECT& rect){
 wchar_t root[1024]{};if(!GetEnvironmentVariableW(L"QUICKER_G0B_GLYPH_CAPTURE",root,1024)||!data)return;
 UINT w=UINT(rect.right-rect.left),h=UINT(rect.bottom-rect.top);if(!w||!h||size<UINT64(w)*h||size>16*1024*1024)return;
 static volatile LONG counter=0;LONG n=InterlockedIncrement(&counter);if(n>128)return;
 wchar_t file[1200]{};swprintf_s(file,L"%s/mask-%03ld.qkam",root,n);HANDLE f=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;
 UINT header[]={0x4d414b51,w,h,size};DWORD done=0;WriteFile(f,header,sizeof header,&done,nullptr);WriteFile(f,data,size,&done,nullptr);CloseHandle(f);
}
