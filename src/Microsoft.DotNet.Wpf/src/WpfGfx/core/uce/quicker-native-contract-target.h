#pragma once
#include <vector>
#include "quicker-native-contract-abi.h"
// 仅单窗契约探针。继承 dummy 复用非绘制辅助函数；所有未支持的绘制/中间目标显式失败。
class CQuickerNativeContractRT final : public CDummyRenderTarget
{
    LONG refs=1; HWND hwnd; CMILMatrix deviceMatrix; CMilRectF bounds; bool active=false;
    typedef HRESULT (__cdecl *BeginFn)(HWND,const float*);
    typedef HRESULT (__cdecl *PathFn)(HWND,const float*,const QkNativeSegment*,int,int,const float*,const float*);
    typedef HRESULT (__cdecl *EndFn)(HWND,bool);
    typedef void (__cdecl *ReleaseFn)(HWND);
    HMODULE module=nullptr; BeginFn begin=nullptr; PathFn path=nullptr; EndFn end=nullptr; ReleaseFn release=nullptr;
    HRESULT Reject(const char* feature){
        Bump(L"Quicker.NativeRejects");WCHAR p[1024]={};
        if(GetEnvironmentVariableW(L"QUICKER_NATIVE_CONTRACT_LOG",p,ARRAYSIZE(p))){
            HANDLE f=CreateFileW(p,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(f,feature,static_cast<DWORD>(strlen(feature)),&written,nullptr);WriteFile(f,"\r\n",2,&written,nullptr);CloseHandle(f);}
        }
        return E_NOTIMPL;
    }
    void Bump(const wchar_t* key){SetPropW(hwnd,key,reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(GetPropW(hwnd,key))+1));}
public:
    explicit CQuickerNativeContractRT(HWND h):hwnd(h){deviceMatrix.SetToIdentity();bounds=CMilRectF(0,0,1,1,XYWH_Parameters);}
    ~CQuickerNativeContractRT(){if(active&&end)end(hwnd,false);if(release)release(hwnd);}
    static bool Enabled(HWND h){wchar_t x[8]={};return GetEnvironmentVariableW(L"QUICKER_WPF_NATIVE_CONTRACT",x,8)==1&&x[0]==L'1'&&h!=nullptr;}
    static HRESULT Create(HWND h,IMILRenderTargetHWND** result){
        *result=nullptr;auto t=new CQuickerNativeContractRT(h);if(!t)return E_OUTOFMEMORY;
        t->module=LoadLibraryExW(L"Quicker.WpfD3D11Gpu.dll",nullptr,LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!t->module){delete t;return HRESULT_FROM_WIN32(GetLastError());}
        t->begin=reinterpret_cast<BeginFn>(GetProcAddress(t->module,"QkNativeBegin"));
        t->path=reinterpret_cast<PathFn>(GetProcAddress(t->module,"QkNativePath"));
        t->end=reinterpret_cast<EndFn>(GetProcAddress(t->module,"QkNativeEnd"));
        t->release=reinterpret_cast<ReleaseFn>(GetProcAddress(t->module,"GpuRelease"));
        if(!t->begin||!t->path||!t->end||!t->release){delete t;return E_NOINTERFACE;}
        BumpCreated(h);*result=static_cast<IMILRenderTargetHWND*>(t);return S_OK;
    }
    static void BumpCreated(HWND h){SetPropW(h,L"Quicker.NativeCreated",reinterpret_cast<HANDLE>(1));}
    STDMETHODIMP_(ULONG) AddRef() override{return InterlockedIncrement(&refs);}
    STDMETHODIMP_(ULONG) Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    STDMETHODIMP QueryInterface(REFIID iid,void** result) override{
        if(!result)return E_POINTER;*result=nullptr;
        if(iid==IID_IRenderTargetInternal)*result=static_cast<IRenderTargetInternal*>(this);
        else if(iid==IID_IUnknown||iid==IID_IMILRenderTargetHWND)*result=static_cast<IMILRenderTargetHWND*>(this);
        else if(iid==IID_IMILRenderTarget)*result=static_cast<IMILRenderTarget*>(static_cast<IRenderTargetInternal*>(this));
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    STDMETHODIMP_(void) GetBounds(MilRectF* result) override{*result=bounds;}
    STDMETHODIMP_(const CMILMatrix*) GetDeviceTransform() const override{return &deviceMatrix;}
    STDMETHODIMP GetType(DWORD* result) override{*result=HWRasterRenderTarget;return S_OK;}
    UINT GetRealizationCacheIndex() override{return CMILResourceCache::SwRealizationCacheIndex;}
    STDMETHODIMP Clear(const MilColorF* color,const CAliasedClip* clip=nullptr) override{
        if(clip&&!clip->IsNullClip()){CMilRectF rc;clip->GetAsCMilRectF(&rc);if(rc.left>bounds.left||rc.top>bounds.top||rc.right<bounds.right||rc.bottom<bounds.bottom)return Reject(__FUNCTION__);}if(active){auto hr=end(hwnd,false);active=false;if(FAILED(hr))return hr;}
        MilColorF converted={};if(color)converted=Convert_MilColorF_scRGB_To_MilColorF_sRGB(color);float c[4]={converted.r,converted.g,converted.b,converted.a};auto hr=begin(hwnd,c);active=SUCCEEDED(hr);return hr;
    }
    STDMETHODIMP DrawPath(CContextState* state,BrushContext* brushContext,IShapeData* shape,CPlainPen* pen,CBrushRealizer* stroke,CBrushRealizer* fill) override{
        if(!active)return E_UNEXPECTED;if(pen||stroke)return Reject(__FUNCTION__);if(!fill)return S_OK;
        auto hr=fill->EnsureRealization(CMILResourceCache::SwRealizationCacheIndex,DisplayId::None,brushContext,state,static_cast<IRenderTargetInternal*>(this));if(FAILED(hr))return hr;
        auto b=fill->GetRealizedBrushNoRef(false);if(!b)return S_OK;if(b->GetType()!=BrushSolid)return Reject(__FUNCTION__);
        MilColorF color;static_cast<CMILBrushSolid*>(b)->GetColor(&color);color=Convert_MilColorF_scRGB_To_MilColorF_sRGB(&color);float c[4]={color.r,color.g,color.b,color.a};
        std::vector<QkNativeSegment> segments;
        for(UINT i=0;i<shape->GetFigureCount();++i){auto& f=shape->GetFigure(i);if(f.IsEmpty())continue;auto& p=f.GetStartPoint();segments.push_back({0,p.X,p.Y,0,0,0,0,f.IsFillable()?1:0});
            if(f.SetToFirstSegment()){do{BYTE type;const MilPoint2F* points;f.GetCurrentSegment(type,points);
                if((type&MilCoreSeg::TypeMask)==MilCoreSeg::TypeLine)segments.push_back({1,points[0].X,points[0].Y,0,0,0,0,0});
                else if((type&MilCoreSeg::TypeMask)==MilCoreSeg::TypeBezier)segments.push_back({2,points[0].X,points[0].Y,points[1].X,points[1].Y,points[2].X,points[2].Y,0});else return Reject(__FUNCTION__);
            }while(f.SetToNextSegment());}
            segments.push_back({3,0,0,0,0,0,0,f.IsClosed()?1:0});
        }
        if(segments.empty())return S_OK;auto& m=state->WorldToDevice;float matrix[6]={m._11,m._12,m._21,m._22,m._41,m._42};
        CMilRectF rc;float clip[4];float* pClip=nullptr;if(!state->AliasedClip.IsNullClip()){state->AliasedClip.GetAsCMilRectF(&rc);clip[0]=rc.left;clip[1]=rc.top;clip[2]=rc.right;clip[3]=rc.bottom;pClip=clip;}
        hr=path(hwnd,matrix,segments.data(),static_cast<int>(segments.size()),shape->GetFillMode()==MilFillMode::Winding,c,pClip);if(SUCCEEDED(hr))Bump(L"Quicker.NativePaths");return hr;
    }
    STDMETHODIMP SetPosition(const MilRectF* rect) override{bounds=CMilRectF(0,0,rect->right-rect->left,rect->bottom-rect->top,XYWH_Parameters);return S_OK;}
    STDMETHODIMP GetInvalidRegions(const MilRectF** regions,UINT* count,bool* whole) override{*regions=nullptr;*count=0;*whole=true;return S_OK;}
    STDMETHODIMP Present() override{if(!active)return S_OK;active=false;auto hr=end(hwnd,true);if(SUCCEEDED(hr))Bump(L"Quicker.NativePresents");return hr;}
    STDMETHODIMP CanAccelerateScroll(bool* result) override{*result=false;return S_OK;}
    STDMETHODIMP GetNumQueuedPresents(UINT* result) override{*result=0;return S_OK;}
    STDMETHODIMP BeginLayer(const MilRectF&,MilAntiAliasMode::Enum,const IShapeData*,const CMILMatrix*,FLOAT,CBrushRealizer*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP EndLayer() override{return Reject(__FUNCTION__);}
    STDMETHODIMP_(void) EndAndIgnoreAllLayers() override{}
    STDMETHODIMP CreateRenderTargetBitmap(UINT,UINT,IntermediateRTUsage,MilRTInitialization::Flags,IMILRenderTargetBitmap** result,const DynArray<bool>* =nullptr) override{*result=nullptr;return Reject(__FUNCTION__);}
    STDMETHODIMP DrawBitmap(CContextState*,IWGXBitmapSource*,IMILEffectList*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP DrawMesh3D(CContextState*,BrushContext*,CMILMesh3D*,CMILShader*,IMILEffectList*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP DrawInfinitePath(CContextState*,BrushContext*,CBrushRealizer*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP DrawGlyphs(DrawGlyphsParameters&) override{return Reject(__FUNCTION__);}
    STDMETHODIMP DrawVideo(CContextState*,IAVSurfaceRenderer*,IWGXBitmapSource*,IMILEffectList*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP ComposeEffect(CContextState*,CMILMatrix*,CMilEffectDuce*,UINT,UINT,IMILRenderTargetBitmap*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP Begin3D(const MilRectF&,MilAntiAliasMode::Enum,bool,FLOAT) override{return Reject(__FUNCTION__);}
    STDMETHODIMP End3D() override{return Reject(__FUNCTION__);}
};
