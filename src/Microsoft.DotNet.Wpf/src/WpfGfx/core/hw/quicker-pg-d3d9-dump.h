#pragma once
// 仅在显式环境路径下，将真实 D3D9 GPU 的既有呈现回读缓冲原样保存为质量参照。
// 不调用软件离屏光栅器，也不改变正常呈现的内容或返回值。
inline void QkPgDumpBackBuffer(const void* buffer,UINT width,UINT height,UINT stride,D3DFORMAT format)
{
    WCHAR file[1024]={};
    DWORD size=GetEnvironmentVariableW(L"QUICKER_PG_D3D9_DUMP",file,ARRAYSIZE(file));
    if(!size||size>=ARRAYSIZE(file)||format!=D3DFMT_A8R8G8B8)return;
    HANDLE h=CreateFileW(file,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    DWORD header[]={0x47424b51,width,height,width*4};DWORD written=0;
    WriteFile(h,header,sizeof(header),&written,nullptr);
    for(UINT y=0;y<height;++y)WriteFile(h,static_cast<const BYTE*>(buffer)+y*stride,width*4,&written,nullptr);
    CloseHandle(h);
}
