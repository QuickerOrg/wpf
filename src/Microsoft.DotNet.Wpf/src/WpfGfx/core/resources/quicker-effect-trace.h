#pragma once
#include "../glyph/quicker-text-trace.h"
inline void QkCaptureEffectInput(IDirect3DTexture9* texture){
 WCHAR root[1024]{};if(!GetEnvironmentVariableW(L"QUICKER_G0C_TEXT_TRACE",root,1024))return;
 IDirect3DDevice9* device=nullptr;IDirect3DSurface9* surface=nullptr;IDirect3DSurface9* read=nullptr;
 if(SUCCEEDED(texture->GetDevice(&device))&&SUCCEEDED(texture->GetSurfaceLevel(0,&surface))){D3DSURFACE_DESC desc{};if(SUCCEEDED(surface->GetDesc(&desc))&&desc.Format==D3DFMT_A8R8G8B8&&SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr))&&SUCCEEDED(device->GetRenderTargetData(surface,read))){D3DLOCKED_RECT map{};if(SUCCEEDED(read->LockRect(&map,nullptr,D3DLOCK_READONLY))){WCHAR path[1100]{};_snwprintf_s(path,ARRAYSIZE(path),_TRUNCATE,L"%s.source-%ux%u.qkbg",root,desc.Width,desc.Height);HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){UINT head[]={0x47424b51,desc.Width,desc.Height,desc.Width*4};DWORD written;WriteFile(file,head,sizeof head,&written,nullptr);for(UINT y=0;y<desc.Height;y++)WriteFile(file,static_cast<BYTE*>(map.pBits)+y*map.Pitch,desc.Width*4,&written,nullptr);CloseHandle(file);}read->UnlockRect();}}}
 if(read)read->Release();if(surface)surface->Release();if(device)device->Release();
}

inline void QkTraceEffectDraw(IDirect3DTexture9* texture){
 WCHAR path[1024]{};if(!GetEnvironmentVariableW(L"QUICKER_G0C_TEXT_TRACE",path,1024))return;IDirect3DDevice9* device=nullptr;if(FAILED(texture->GetDevice(&device)))return;D3DVIEWPORT9 vp{};if(SUCCEEDED(device->GetViewport(&vp))){float v[]={float(vp.X),float(vp.Y),float(vp.Width),float(vp.Height),0,0};QkTraceTextState("old-draw-viewport",0,0,false,v);}for(UINT i=0;i<4;i++){float v[6]{};if(SUCCEEDED(device->GetVertexShaderConstantF(i,v,1)))QkTraceTextState("old-draw-vs",i,0,false,v);}device->Release();
}
