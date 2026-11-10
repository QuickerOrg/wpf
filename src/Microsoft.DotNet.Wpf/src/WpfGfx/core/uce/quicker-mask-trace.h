#pragma once
#include "quicker-coverage-abi.h"
// 质量归因专用；不在正式采样设置文字 trace。
inline void QkTraceMaskVertices(const WCHAR* phase,UINT mode,const INT* bounds,float opacity,const QkCoverageVertex* vertices,UINT count){
 WCHAR prefix[1024];if(!GetEnvironmentVariableW(L"QUICKER_G0C_TEXT_TRACE",prefix,1024))return;static LONG sequence=0;LONG id=InterlockedIncrement(&sequence);WCHAR path[1200];_snwprintf_s(path,1200,_TRUNCATE,L"%s.mask-%s-%ld.qkmv",prefix,phase,id);HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;
 UINT h[]={0x564d4b51,count,mode,static_cast<UINT>(bounds[0]),static_cast<UINT>(bounds[1]),static_cast<UINT>(bounds[2]),static_cast<UINT>(bounds[3]),0};memcpy(&h[7],&opacity,4);DWORD n;WriteFile(f,h,sizeof h,&n,nullptr);if(count)WriteFile(f,vertices,count*sizeof(QkCoverageVertex),&n,nullptr);CloseHandle(f);
}
