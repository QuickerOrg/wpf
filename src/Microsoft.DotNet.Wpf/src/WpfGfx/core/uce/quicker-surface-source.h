#pragma once
// WPF 内部扩展接口；只用于显式选择的新 D3D11 目标，不注册 COM。
class CMilEffectDuce;
static const GUID IID_IQuickerSurfaceSource={0x589ed83a,0xc64b,0x48bc,{0xa7,0xea,0xa6,0x32,0xec,0xbb,0xee,0x67}};
struct IQuickerSurfaceSource : public IUnknown {
 virtual HRESULT STDMETHODCALLTYPE GetSurfaceToken(void** token,UINT* width,UINT* height)=0;
 virtual HRESULT STDMETHODCALLTYPE GetSurfaceCapabilities(UINT* maxDimension,bool* quality)=0;
 virtual HRESULT STDMETHODCALLTYPE CheckSurfaceEffect(CMilEffectDuce* effect)=0;
};
