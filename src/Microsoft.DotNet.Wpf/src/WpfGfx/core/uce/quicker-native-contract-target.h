#pragma once
#include <vector>
#include "quicker-native-contract-abi.h"
#include "quicker-coverage-sink.h"
#include "quicker-glyph-painter.h"
#include "quicker-surface-abi.h"
#include "quicker-surface-source.h"
#include "quicker-aliased-geometry-sink.h"
#include "quicker-mask-trace.h"
#include "quicker-mask-coverage-sink.h"
// 仅单窗契约探针。继承 dummy 复用非绘制辅助函数；所有未支持的绘制/中间目标显式失败。
class CQuickerNativeContractRT final : public CDummyRenderTarget, public IQuickerSurfaceSource
{
    CQuickerNativeContractRT* ownerTarget=nullptr;bool offscreen=false,surfaceMode=false;
    typedef HRESULT (__cdecl *SurfaceCreateFn)(HWND,HWND,UINT,UINT);
    typedef HRESULT (__cdecl *SurfaceComposeFn)(HWND,const QkSurfacePacket*);
    typedef HRESULT (__cdecl *SurfaceCapsFn)(UINT*,bool*);
    typedef HRESULT (__cdecl *LayerPushFn)(HWND,const QkCoverageVertex*,UINT,const INT*,float,bool);
    typedef HRESULT (__cdecl *LayerPopFn)(HWND);typedef void (__cdecl *LayerDiscardFn)(HWND);
    LayerPushFn layerPush=nullptr;LayerPopFn layerPop=nullptr;LayerDiscardFn layerDiscard=nullptr;
    struct LayerState{bool gpu;bool hint;};std::vector<LayerState> layerStates;
    SurfaceCreateFn surfaceCreate=nullptr;SurfaceComposeFn surfaceCompose=nullptr;SurfaceCapsFn surfaceCaps=nullptr;
    LONG refs=1; HWND hwnd; CMILMatrix deviceMatrix; CMilRectF bounds; bool active=false,hadUnsupported=false;
    typedef HRESULT (__cdecl *BeginFn)(HWND,const float*);
    typedef HRESULT (__cdecl *PathFn)(HWND,const float*,const QkNativeSegment*,int,int,const float*,const float*);
    typedef HRESULT (__cdecl *EndFn)(HWND,bool);
    typedef void (__cdecl *ReleaseFn)(HWND);
    typedef HRESULT (__cdecl *CoverageFn)(HWND,const QkCoverageVertex*,int,const float*,const INT*);
    typedef HRESULT (__cdecl *GlyphFn)(HWND,const QkGlyphPacket*);
    typedef HRESULT (__cdecl *BeginRegionFn)(HWND,const float*,const INT*);BeginRegionFn beginRegion=nullptr;bool contentsValid=false;UINT validWidth=0,validHeight=0;
    static bool RetentionEnabled(){static const bool v=[](){WCHAR s[8]{};return GetEnvironmentVariableW(L"QUICKER_WPF_RETAIN_CONTENTS",s,8)==1&&s[0]==L'1';}();return v;}
    bool portText=false,forceClearType=false;GlyphFn glyph=nullptr;CGlyphPainterMemory glyphMemory;
    bool portGeometry=false; CoverageFn coverage=nullptr; std::vector<RECT> portClips,portViewports;
    HMODULE module=nullptr; BeginFn begin=nullptr; PathFn path=nullptr; EndFn end=nullptr; ReleaseFn release=nullptr;
    HRESULT Reject(const char* feature){
        auto root=this;while(root->ownerTarget)root=root->ownerTarget;root->hadUnsupported=true;root->contentsValid=false;
        SetPropW(Window(),L"Quicker.NativeRejects",reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(GetPropW(Window(),L"Quicker.NativeRejects"))+1));WCHAR p[1024]={};
        if(GetEnvironmentVariableW(L"QUICKER_NATIVE_CONTRACT_LOG",p,ARRAYSIZE(p))){
            HANDLE f=CreateFileW(p,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(f,feature,static_cast<DWORD>(strlen(feature)),&written,nullptr);WriteFile(f,"\r\n",2,&written,nullptr);CloseHandle(f);}
        }
        return E_NOTIMPL;
    }
    static bool CountersEnabled(){static const bool value=[](){WCHAR v[8]{};return GetEnvironmentVariableW(L"QUICKER_GPU_RUN_COUNTERS",v,8)==1&&v[0]==L'1';}();return value;}
    struct CounterSlot{const wchar_t* key=nullptr;ULONG_PTR value=0;};CounterSlot counters[64]{};UINT counterCount=0;
    static int CounterMode(){static const int mode=[](){WCHAR v[16]{};GetEnvironmentVariableW(L"QUICKER_GPU_COUNTER_PUBLICATION",v,16);return !wcscmp(v,L"quiet")?2:(!wcscmp(v,L"frame")?1:0);}();return mode;}
    CQuickerNativeContractRT* CounterOwner(){auto p=this;while(p->ownerTarget)p=p->ownerTarget;return p;}
    void Bump(const wchar_t* key){if(!CountersEnabled())return;if(!CounterMode()){SetPropW(Window(),key,reinterpret_cast<HANDLE>(reinterpret_cast<ULONG_PTR>(GetPropW(Window(),key))+1));return;}
      auto p=CounterOwner();for(UINT i=0;i<p->counterCount;i++)if(!wcscmp(p->counters[i].key,key)){++p->counters[i].value;return;}
      if(p->counterCount<ARRAYSIZE(p->counters))p->counters[p->counterCount++]={key,1};else SetPropW(Window(),L"Quicker.NativeRejects",reinterpret_cast<HANDLE>(1));}
    void FlushCounters(){if(!CountersEnabled()||!CounterMode())return;auto p=CounterOwner();ULONG_PTR presents=0;for(UINT i=0;i<p->counterCount;i++)if(!wcscmp(p->counters[i].key,L"Quicker.NativePresents"))presents=p->counters[i].value;
      bool all=CounterMode()==1||presents==1||presents%60==0;for(UINT i=0;i<p->counterCount;i++){auto& c=p->counters[i];if(all||!wcscmp(c.key,L"Quicker.NativePresents")||!wcscmp(c.key,L"Quicker.NativeUnqualifiedFrames"))SetPropW(p->Window(),c.key,reinterpret_cast<HANDLE>(c.value));}}

HWND Window() const{return ownerTarget?ownerTarget->Window():hwnd;}
    BOOL SurfaceRect(RECT* r) const{if(!offscreen)return GetClientRect(hwnd,r);r->left=0;r->top=0;r->right=static_cast<LONG>(bounds.right);r->bottom=static_cast<LONG>(bounds.bottom);return TRUE;}
public:
    explicit CQuickerNativeContractRT(HWND h):hwnd(h){deviceMatrix.SetToIdentity();bounds=CMilRectF(0,0,1,1,XYWH_Parameters);}
    ~CQuickerNativeContractRT(){if(layerDiscard)layerDiscard(hwnd);if(active&&end)end(hwnd,false);if(release)release(hwnd);if(module)FreeLibrary(module);if(ownerTarget)ownerTarget->Release();}
    static bool Enabled(HWND h){wchar_t x[8]={};return GetEnvironmentVariableW(L"QUICKER_WPF_NATIVE_CONTRACT",x,8)==1&&x[0]==L'1'&&h!=nullptr;}
    static HRESULT Create(HWND h,IMILRenderTargetHWND** result){
        *result=nullptr;auto t=new CQuickerNativeContractRT(h);if(!t)return E_OUTOFMEMORY;
        t->module=LoadLibraryExW(L"Quicker.WpfD3D11Gpu.dll",nullptr,LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!t->module){delete t;return HRESULT_FROM_WIN32(GetLastError());}
        wchar_t pg[8]={};t->portGeometry=GetEnvironmentVariableW(L"QUICKER_WPF_PORT_GEOMETRY",pg,8)==1&&pg[0]==L'1';
        if(t->portGeometry){
            t->begin=reinterpret_cast<BeginFn>(GetProcAddress(t->module,"QkPortBegin"));
            t->coverage=reinterpret_cast<CoverageFn>(GetProcAddress(t->module,"QkPortVertices"));
            t->end=reinterpret_cast<EndFn>(GetProcAddress(t->module,"QkPortEnd"));
            t->release=reinterpret_cast<ReleaseFn>(GetProcAddress(t->module,"QkPortRelease"));
            if(!t->begin||!t->coverage||!t->end||!t->release){delete t;return E_NOINTERFACE;}
            wchar_t pt[8]{};t->portText=GetEnvironmentVariableW(L"QUICKER_WPF_PORT_TEXT",pt,8)==1&&pt[0]==L'1';
            if(t->portText){t->glyph=reinterpret_cast<GlyphFn>(GetProcAddress(t->module,"QkTextGlyphV3"));t->begin=reinterpret_cast<BeginFn>(GetProcAddress(t->module,"QkTextBegin"));t->release=reinterpret_cast<ReleaseFn>(GetProcAddress(t->module,"QkTextRelease"));if(!t->glyph||!t->begin||!t->release){delete t;return E_NOINTERFACE;}}
            wchar_t surfaces[8]{};t->surfaceMode=t->portText&&GetEnvironmentVariableW(L"QUICKER_WPF_PORT_SURFACES",surfaces,8)==1&&surfaces[0]==L'1';
            if(t->surfaceMode){t->surfaceCreate=reinterpret_cast<SurfaceCreateFn>(GetProcAddress(t->module,"QkSurfaceCreate"));t->surfaceCompose=reinterpret_cast<SurfaceComposeFn>(GetProcAddress(t->module,"QkSurfaceComposeV3"));t->surfaceCaps=reinterpret_cast<SurfaceCapsFn>(GetProcAddress(t->module,"QkSurfaceCapabilities"));t->release=reinterpret_cast<ReleaseFn>(GetProcAddress(t->module,"QkSurfaceRootRelease"));t->layerPush=reinterpret_cast<LayerPushFn>(GetProcAddress(t->module,"QkLayerPushV1"));t->layerPop=reinterpret_cast<LayerPopFn>(GetProcAddress(t->module,"QkLayerPopV1"));t->layerDiscard=reinterpret_cast<LayerDiscardFn>(GetProcAddress(t->module,"QkLayerDiscardV1"));if(!t->layerPush||!t->layerPop||!t->layerDiscard||!t->surfaceCreate||!t->surfaceCompose||!t->surfaceCaps||!t->release){delete t;return E_NOINTERFACE;}}
            if(RetentionEnabled()){if(!t->surfaceMode){delete t;return E_NOINTERFACE;}t->beginRegion=reinterpret_cast<BeginRegionFn>(GetProcAddress(t->module,"QkTextBeginRegionV1"));if(!t->beginRegion){delete t;return E_NOINTERFACE;}}
            BumpCreated(h);*result=static_cast<IMILRenderTargetHWND*>(t);return S_OK;
        }
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
        else if(iid==IID_IQuickerSurfaceSource&&surfaceMode)*result=static_cast<IQuickerSurfaceSource*>(this);
        else if(offscreen&&iid==IID_IMILRenderTargetBitmap)*result=static_cast<IMILRenderTargetBitmap*>(this);
        else if(offscreen&&iid==IID_IWGXBitmapSource)*result=static_cast<IWGXBitmapSource*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    STDMETHODIMP_(void) GetBounds(MilRectF* result) override{if(!offscreen){RECT r{};if(GetClientRect(hwnd,&r))bounds=CMilRectF(0,0,float(r.right),float(r.bottom),XYWH_Parameters);else bounds.SetEmpty();}*result=bounds;}
    STDMETHODIMP_(const CMILMatrix*) GetDeviceTransform() const override{return &deviceMatrix;}
    STDMETHODIMP GetType(DWORD* result) override{*result=HWRasterRenderTarget;return S_OK;}
    STDMETHODIMP ReadEnabledDisplays(DynArray<bool>* enabled) override{
        if(!enabled||enabled->GetCount()==0)return Reject("P-R no logical output for cache residency");
        // 本原型全目标共用一个D3D11设备/纹理域，缓存可供其所有逻辑输出使用。
        // 不能继承dummy的空实现：原缓存管理器会永远等待首次驻留，静默跳过缓存视觉。
        for(UINT i=0;i<enabled->GetCount();++i)(*enabled)[i]=true;
        Bump(L"Quicker.CacheDisplayReads");return S_OK;
    }
    UINT GetRealizationCacheIndex() override{return CMILResourceCache::SwRealizationCacheIndex;}
    STDMETHODIMP Clear(const MilColorF* color,const CAliasedClip* clip=nullptr) override{
        if(!offscreen){RECT r{};if(!GetClientRect(hwnd,&r))return E_FAIL;if(r.right<=0||r.bottom<=0){contentsValid=false;Bump(L"Quicker.ZeroSurfaceSkips");return S_OK;}}
        bool partial=false;INT rect[4]{};
        if(RetentionEnabled()||offscreen){
            // 离屏BitmapCache自身会局部刷新，独立于窗口是否采用整窗对照。
            // 完全复用原 CHwSurfaceRenderTarget::Clear 的整数矩形裁剪算法。
            if(!color)return S_OK;RECT size{};if(!SurfaceRect(&size))return E_FAIL;
            CMILSurfaceRect surface,area;surface.left=0;surface.top=0;surface.right=size.right;surface.bottom=size.bottom;
            if(!IntersectCAliasedClipWithSurfaceRect(clip,surface,&area))return S_OK;
            rect[0]=area.left;rect[1]=area.top;rect[2]=area.right;rect[3]=area.bottom;
            partial=area.left!=0||area.top!=0||area.right!=size.right||area.bottom!=size.bottom;
            if(partial&&(!contentsValid||!beginRegion))return Reject("P-R partial clear without retained contents");
        }else if(clip&&!clip->IsNullClip()){CMilRectF rc;clip->GetAsCMilRectF(&rc);if(rc.left>bounds.left||rc.top>bounds.top||rc.right<bounds.right||rc.bottom<bounds.bottom)return Reject(__FUNCTION__);}
        if(active){auto hr=end(hwnd,false);active=false;if(FAILED(hr)){contentsValid=false;return hr;}}
        MilColorF converted={};if(color)converted=Convert_MilColorF_scRGB_To_MilColorF_sRGB(color);float c[4]={converted.r,converted.g,converted.b,converted.a};
        auto hr=partial?beginRegion(hwnd,c,rect):begin(hwnd,c);active=SUCCEEDED(hr);
        if(FAILED(hr))contentsValid=false;else{Bump(partial?L"Quicker.RetainedPartialClears":L"Quicker.RetainedFullClears");if(offscreen)contentsValid=true;}
        return hr;
    }
    HRESULT PaintCoverage(CContextState* state,BrushContext* brushContext,const IShapeData* shape,CBrushRealizer* realizer,const CMatrix<CoordinateSpace::Shape,CoordinateSpace::Device>* matrix){
        if(!realizer)return S_OK;
        if(!surfaceMode&&state->RenderState->AntiAliasMode==MilAntiAliasMode::None)return Reject("P-G aliased geometry");
        HRESULT hr=realizer->EnsureRealization(CMILResourceCache::SwRealizationCacheIndex,DisplayId::None,brushContext,state,static_cast<IRenderTargetInternal*>(this));if(FAILED(hr))return hr;
        auto brush=realizer->GetRealizedBrushNoRef(false);if(!brush)return S_OK;if(surfaceMode&&brush->GetType()==BrushBitmap)return PaintSurfaceBitmapBrush(state,shape,realizer,static_cast<CMILBrushBitmap*>(brush),matrix);if(brush->GetType()!=BrushSolid)return Reject("P-G non-solid brush");
        // 对齐原 FillPathWithBrush：先按原 snapping stack 修正形状，再生成覆盖率。
        CRectF<CoordinateSpace::Shape> rawBounds;hr=shape->GetTightBounds(rawBounds);if(FAILED(hr))return hr;CShape snapShape;CShapeClipperForFEB corrected(shape,&rawBounds,matrix);hr=corrected.ApplyGuidelines(state->m_pSnappingStack,&snapShape);if(FAILED(hr))return hr;shape=corrected.GetShape();matrix=corrected.GetShapeToDeviceTransformOrNull();
        MilColorF color;static_cast<CMILBrushSolid*>(brush)->GetColor(&color);color=Convert_MilColorF_scRGB_To_MilColorF_sRGB(&color);float c[]={color.r,color.g,color.b,color.a};
            RECT client={};if(!SurfaceRect(&client))return HRESULT_FROM_WIN32(GetLastError());
            MilPointAndSizeL clip={};clip.Width=client.right;clip.Height=client.bottom;
            if(!state->AliasedClip.IsNullClip()){
                CMilRectF r;state->AliasedClip.GetAsCMilRectF(&r);
                CMILSurfaceRect whole,intersection;whole.left=0;whole.top=0;whole.right=client.right;whole.bottom=client.bottom;if(!IntersectAliasedBoundsRectFWithSurfaceRect(r,whole,&intersection))return S_OK;r=CMilRectF(float(intersection.left),float(intersection.top),float(intersection.right),float(intersection.bottom),LTRB_Parameters);
                clip.X=max(0,(INT)r.left);clip.Y=max(0,(INT)r.top);
                clip.Width=max(0,min((INT)client.right,(INT)r.right)-clip.X);clip.Height=max(0,min((INT)client.bottom,(INT)r.bottom)-clip.Y);
            }
            if(!clip.Width||!clip.Height)return S_OK;
            DynArray<MilPoint2F> points;DynArray<BYTE> types;CHwRasterizer raster;QkCoverageSink sink;
            if(state->RenderState->AntiAliasMode==MilAntiAliasMode::None){QkAliasedGeometrySink aliased;CFillTessellator* tessellator=nullptr;CDispensableBuffer<QkAliasedGeometrySink::TessellatorBufferSize,1> local;hr=shape->SetupFillTessellator(matrix,&local,&tessellator);if(SUCCEEDED(hr)&&tessellator)hr=tessellator->SendGeometry(&aliased);delete tessellator;if(hr==WGXHR_EMPTYFILL)return S_OK;if(FAILED(hr))return hr;sink.vertices=std::move(aliased.vertices);}
            else{hr=raster.SetupWithClip(clip,shape,&points,&types,matrix);if(FAILED(hr))return hr;hr=raster.SendGeometry(&sink);if(hr==WGXHR_EMPTYFILL)return S_OK;if(FAILED(hr))return hr;}
            if(sink.vertices.empty())return S_OK;
            INT scissor[]={clip.X,clip.Y,clip.X+clip.Width,clip.Y+clip.Height};
            if(!portClips.empty()){
                auto r=portClips.back();scissor[0]=max(scissor[0],(INT)r.left);scissor[1]=max(scissor[1],(INT)r.top);scissor[2]=min(scissor[2],(INT)r.right);scissor[3]=min(scissor[3],(INT)r.bottom);
                if(scissor[0]>=scissor[2]||scissor[1]>=scissor[3])return S_OK;
            }
            hr=coverage(hwnd,sink.vertices.data(),static_cast<int>(sink.vertices.size()),c,scissor);
            if(SUCCEEDED(hr)){Bump(L"Quicker.NativePaths");Bump(L"Quicker.PortGeometryPaths");}
        return hr;
    }
    HRESULT DrawCoveragePath(CContextState* state,BrushContext* brushContext,IShapeData* shape,CPlainPen* pen,CBrushRealizer* stroke,CBrushRealizer* fill){
        if(!active)return E_UNEXPECTED;
        auto matrix=reinterpret_cast<const CMatrix<CoordinateSpace::Shape,CoordinateSpace::Device>*>(&state->WorldToDevice);
        HRESULT hr=PaintCoverage(state,brushContext,shape,fill,matrix);if(FAILED(hr))return hr;
        if(pen&&stroke){
            // 与 CHwSurfaceRenderTarget::DrawPath 相同：先用原 WPF pen/widener 得到设备空间外形，再填充。
            CShape widened;RECT r{};if(!SurfaceRect(&r))return E_FAIL;if(!portViewports.empty())r=portViewports.back();CMILSurfaceRect wideningBounds;wideningBounds.left=r.left;wideningBounds.top=r.top;wideningBounds.right=r.right;wideningBounds.bottom=r.bottom;
            hr=shape->WidenToShape(*pen,DEFAULT_FLATTENING_TOLERANCE,false,widened,CMILMatrix::ReinterpretBase(matrix),&wideningBounds);if(FAILED(hr))return hr;
            hr=PaintCoverage(state,brushContext,&widened,stroke,nullptr);
        }
        return hr;
    }
    STDMETHODIMP DrawPath(CContextState* state,BrushContext* brushContext,IShapeData* shape,CPlainPen* pen,CBrushRealizer* stroke,CBrushRealizer* fill) override{
        if(portGeometry)return DrawCoveragePath(state,brushContext,shape,pen,stroke,fill);
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
    STDMETHODIMP GetInvalidRegions(const MilRectF** regions,UINT* count,bool* whole) override{
        *regions=nullptr;*count=0;*whole=true;
        if(RetentionEnabled()&&!offscreen){RECT r{};if(!GetClientRect(hwnd,&r))return E_FAIL;
            if(r.right<=0||r.bottom<=0||UINT(r.right)!=validWidth||UINT(r.bottom)!=validHeight)contentsValid=false;
            *whole=!contentsValid;Bump(*whole?L"Quicker.RetainedInvalidChecks":L"Quicker.RetainedValidChecks");}
        return S_OK;
    }
    STDMETHODIMP Present() override{if(hadUnsupported){if(active){active=false;end(hwnd,false);}Bump(L"Quicker.NativeUnqualifiedFrames");FlushCounters();return E_FAIL;}if(!active)return S_OK;active=false;auto hr=end(hwnd,true);if(SUCCEEDED(hr)){Bump(L"Quicker.NativePresents");if(RetentionEnabled()&&!offscreen){RECT r{};contentsValid=GetClientRect(hwnd,&r)&&r.right>0&&r.bottom>0;validWidth=UINT(r.right);validHeight=UINT(r.bottom);}}else contentsValid=false;FlushCounters();return hr;}
    STDMETHODIMP CanAccelerateScroll(bool* result) override{*result=false;return S_OK;}
    STDMETHODIMP GetNumQueuedPresents(UINT* result) override{*result=0;return S_OK;}
    STDMETHODIMP BeginLayer(const MilRectF& boundsF,MilAntiAliasMode::Enum aa,const IShapeData* mask,const CMILMatrix* transform,FLOAT opacity,CBrushRealizer* alphaMask) override{
        if(surfaceMode&&opacity!=1.0f&&!mask&&!alphaMask){Bump(L"Quicker.SurfaceOpacityFallbacks");return WGXERR_NOTIMPLEMENTED;}
        if(!portGeometry||alphaMask||!_finite(opacity)||opacity<0||opacity>1||portClips.size()>=32)return Reject("P-G general layer");
        RECT surface{};if(!SurfaceRect(&surface))return E_FAIL;CMILSurfaceRect current;current.left=0;current.top=0;current.right=surface.right;current.bottom=surface.bottom;
        if(!portViewports.empty()){auto r=portViewports.back();current.left=r.left;current.top=r.top;current.right=r.right;current.bottom=r.bottom;}
        CMILSurfaceRect rounded;if(!IntersectAliasedBoundsRectFWithSurfaceRect(boundsF,current,&rounded)||(opacity<Get_Smallest_scRGB_Significant_for_sRGB()))rounded.SetEmpty();
        RECT viewport={rounded.left,rounded.top,rounded.right,rounded.bottom},rc=viewport;if(!portClips.empty()){auto old=portClips.back();rc.left=max(rc.left,old.left);rc.top=max(rc.top,old.top);rc.right=min(rc.right,old.right);rc.bottom=min(rc.bottom,old.bottom);}
        bool gpu=mask&&!rounded.IsEmpty()&&rc.left<rc.right&&rc.top<rc.bottom;bool hint=forceClearType;
        std::vector<QkCoverageVertex> vertices;bool complement=aa==MilAntiAliasMode::None;
        if(gpu){
            if(!surfaceMode||!layerPush)return Reject("P-M layer ABI missing");CShape shape;HRESULT hr=shape.AddShapeData(*mask,transform);if(FAILED(hr))return hr;shape.SetFillMode(mask->GetFillMode());
            if(complement){
                // 与 EndLayerInternal 相同：原 Combine(Xor) 生成有界补集，不能另写点内判断。
                CShape bound,inverse;CMilRectF rect(float(rc.left),float(rc.top),float(rc.right),float(rc.bottom),LTRB_Parameters);hr=bound.AddRect(rect);if(FAILED(hr))return hr;hr=CShapeBase::Combine(&bound,&shape,MilCombineMode::Xor,false,&inverse);if(FAILED(hr))return hr;
                QkAliasedGeometrySink sink;CFillTessellator* tessellator=nullptr;CDispensableBuffer<QkAliasedGeometrySink::TessellatorBufferSize,1> local;hr=inverse.SetupFillTessellator(nullptr,&local,&tessellator);if(SUCCEEDED(hr)&&tessellator)hr=tessellator->SendGeometry(&sink);delete tessellator;if(FAILED(hr)&&hr!=WGXHR_EMPTYFILL)return hr;vertices=std::move(sink.vertices);
            }else{
                // 保留原覆盖率、补集几何和条带/扫描线绘制顺序。
                MilPointAndSizeL clip={};clip.X=rc.left;clip.Y=rc.top;clip.Width=rc.right-rc.left;clip.Height=rc.bottom-rc.top;DynArray<MilPoint2F> points;DynArray<BYTE> types;CHwRasterizer raster;QkMaskCoverageSink sink(rc,(1.0f-opacity>=Get_Smallest_scRGB_Significant_for_sRGB()));hr=raster.SetupWithClip(clip,&shape,&points,&types,nullptr);if(SUCCEEDED(hr))hr=raster.SendGeometry(&sink);if(FAILED(hr)&&hr!=WGXHR_EMPTYFILL)return hr;hr=sink.Finish();if(FAILED(hr))return hr;vertices=std::move(sink.vertices);
            }
        }
        // 先提交 CPU 栈，内存失败不能留下已清空但无可恢复元数据的 GPU 层。
        size_t size=portClips.size();try{portClips.push_back(rc);portViewports.push_back(viewport);layerStates.push_back({gpu,hint});}catch(...){portClips.resize(size);portViewports.resize(size);layerStates.resize(size);return E_OUTOFMEMORY;}
        if(gpu){INT box[]={rc.left,rc.top,rc.right,rc.bottom};QkTraceMaskVertices(L"new",static_cast<UINT>(aa),box,opacity,vertices.data(),static_cast<UINT>(vertices.size()));auto hr=layerPush(hwnd,vertices.data(),static_cast<UINT>(vertices.size()),box,opacity,complement);if(FAILED(hr)){portClips.pop_back();portViewports.pop_back();layerStates.pop_back();Reject("P-M GPU layer begin failed");return hr;}forceClearType=false;Bump(L"Quicker.PortGeometricLayers");}
        else Bump(L"Quicker.PortRectClips");return S_OK;
    }
    STDMETHODIMP EndLayer() override{
        if(!portGeometry||portClips.empty()||layerStates.empty())return Reject("P-G layer stack underflow");auto state=layerStates.back();HRESULT hr=S_OK;if(state.gpu){hr=layerPop(hwnd);if(FAILED(hr)){char reason[128];_snprintf_s(reason,sizeof reason,_TRUNCATE,"P-M GPU layer end failed hr=%08x active=%u depth=%u",hr,active?1:0,static_cast<UINT>(layerStates.size()));Reject(reason);}}forceClearType=state.hint;portClips.pop_back();portViewports.pop_back();layerStates.pop_back();return hr;
    }
    STDMETHODIMP_(void) EndAndIgnoreAllLayers() override{
        if(layerDiscard)layerDiscard(hwnd);portClips.clear();portViewports.clear();layerStates.clear();
        // 持久缓存的非嵌套绘制上下文在这里结束，不能等到采样时才恢复父目标。
        // 窗口目标仍保持pending，交给统一Present；离屏目标没有Present。
        if(offscreen&&active&&end){auto hr=end(hwnd,false);active=false;if(FAILED(hr))Reject("P-R offscreen EndFrame failed");}
    }
    STDMETHODIMP CreateRenderTargetBitmap(UINT width,UINT height,IntermediateRTUsage usage,MilRTInitialization::Flags flags,IMILRenderTargetBitmap** result,const DynArray<bool>* =nullptr) override{
        if(!result)return E_POINTER;*result=nullptr;if(!surfaceMode||!surfaceCreate||!width||!height||usage.wrapMode!=MilBitmapWrapMode::Extend||((flags&MilRTInitialization::TypeMask)!=MilRTInitialization::Default&&(flags&MilRTInitialization::TypeMask)!=MilRTInitialization::HardwareOnly))return Reject("P-E unsupported intermediate request");
        auto child=new CQuickerNativeContractRT(nullptr);if(!child)return E_OUTOFMEMORY;
        child->offscreen=true;child->surfaceMode=true;child->hwnd=reinterpret_cast<HWND>(child);child->ownerTarget=this;AddRef();child->bounds=CMilRectF(0,0,float(width),float(height),XYWH_Parameters);child->portGeometry=portGeometry;child->portText=portText;child->coverage=coverage;child->glyph=glyph;child->surfaceCreate=surfaceCreate;child->surfaceCompose=surfaceCompose;child->surfaceCaps=surfaceCaps;child->layerPush=layerPush;child->layerPop=layerPop;child->layerDiscard=layerDiscard;
        HMODULE m=ownerTarget?ownerTarget->RootModule():module;child->begin=reinterpret_cast<BeginFn>(GetProcAddress(m,"QkSurfaceBegin"));child->end=reinterpret_cast<EndFn>(GetProcAddress(m,"QkSurfaceEnd"));auto destroy=reinterpret_cast<ReleaseFn>(GetProcAddress(m,"QkSurfaceRelease"));
        child->beginRegion=reinterpret_cast<BeginRegionFn>(GetProcAddress(m,"QkSurfaceBeginRegionV1"));
        if(!child->beginRegion||!child->begin||!child->end||!destroy){delete child;return E_NOINTERFACE;}HRESULT hr=surfaceCreate(child->hwnd,Window(),width,height);if(FAILED(hr)){delete child;return hr;}child->release=destroy;*result=static_cast<IMILRenderTargetBitmap*>(child);return S_OK;
    }
    HMODULE RootModule() const{return ownerTarget?ownerTarget->RootModule():module;}
    STDMETHODIMP GetSurfaceToken(void** token,UINT* width,UINT* height) override{
        if(!token||!width||!height)return E_POINTER;*token=nullptr;if(!offscreen)return E_NOINTERFACE;if(active){auto hr=end(hwnd,false);if(FAILED(hr))return hr;active=false;}*token=hwnd;*width=static_cast<UINT>(bounds.right);*height=static_cast<UINT>(bounds.bottom);return S_OK;
    }
    STDMETHODIMP GetSurfaceCapabilities(UINT* maxDimension,bool* quality) override{return surfaceCaps?surfaceCaps(maxDimension,quality):E_NOINTERFACE;}
    STDMETHODIMP CheckSurfaceEffect(CMilEffectDuce* effect) override{
        if(!effect||!surfaceMode)return E_INVALIDARG;if(!effect->IsOfType(TYPE_BLUREFFECT)&&!effect->IsOfType(TYPE_DROPSHADOWEFFECT))return Reject("P-E custom effect");if(effect->GetShaderRenderMode()==ShaderEffectShaderRenderMode::SoftwareOnly)return Reject("P-E explicit software effect");UINT limit=0;bool quality=false;return GetSurfaceCapabilities(&limit,&quality);
    }
    STDMETHODIMP GetBitmapSource(IWGXBitmapSource** result) override{if(!result)return E_POINTER;*result=nullptr;void* key=nullptr;UINT w=0,h=0;auto hr=GetSurfaceToken(&key,&w,&h);if(FAILED(hr))return hr;*result=static_cast<IWGXBitmapSource*>(this);AddRef();return S_OK;}
    STDMETHODIMP GetCacheableBitmapSource(IWGXBitmapSource** result) override{return GetBitmapSource(result);}
    STDMETHODIMP GetSize(UINT* width,UINT* height) override{if(!width||!height)return E_POINTER;*width=static_cast<UINT>(bounds.right);*height=static_cast<UINT>(bounds.bottom);return S_OK;}
    STDMETHODIMP GetPixelFormat(MilPixelFormat::Enum* result) override{if(!result)return E_POINTER;*result=MilPixelFormat::PBGRA32bpp;return S_OK;}
    STDMETHODIMP CopyPixels(const MILRect*,UINT,UINT,BYTE*) override{return Reject("P-E CPU texture readback");}
    STDMETHODIMP GetResolution(double* x,double* y) override{if(!x||!y)return E_POINTER;*x=*y=96;return S_OK;}
    HRESULT SurfacePacket(CContextState* state,IQuickerSurfaceSource* source,QkSurfacePacket& p,const CBaseMatrix* worldOverride=nullptr){
        p.version=3;p.alphaScale=1;HRESULT hr=source->GetSurfaceToken(&p.source,&p.width,&p.height);if(FAILED(hr))return hr;p.linear=state->RenderState->InterpolationMode!=MilBitmapInterpolationMode::NearestNeighbor||!state->WorldToDevice.IsTranslateOrScale();
        for(int i=0;i<4;i++){float x=(i==1||i==2)?1.f:0.f,y=i>=2?1.f:0.f;p.quad[i]={x,y,x,y};}
        RECT r{};if(!SurfaceRect(&r))return HRESULT_FROM_WIN32(GetLastError());p.clip[2]=r.right;p.clip[3]=r.bottom;if(!state->AliasedClip.IsNullClip()){CMilRectF c;state->AliasedClip.GetAsCMilRectF(&c);CMILSurfaceRect whole,intersection;whole.left=0;whole.top=0;whole.right=r.right;whole.bottom=r.bottom;if(!IntersectAliasedBoundsRectFWithSurfaceRect(c,whole,&intersection)){p.clip[2]=p.clip[0];return S_OK;}p.clip[0]=intersection.left;p.clip[1]=intersection.top;p.clip[2]=intersection.right;p.clip[3]=intersection.bottom;}if(!portClips.empty()){auto c=portClips.back();p.clip[0]=max(p.clip[0],(INT)c.left);p.clip[1]=max(p.clip[1],(INT)c.top);p.clip[2]=min(p.clip[2],(INT)c.right);p.clip[3]=min(p.clip[3],(INT)c.bottom);}
        CMILSurfaceRect viewport;viewport.left=0;viewport.top=0;viewport.right=r.right;viewport.bottom=r.bottom;if(!portViewports.empty()){auto v=portViewports.back();viewport.left=v.left;viewport.top=v.top;viewport.right=v.right;viewport.bottom=v.bottom;}if(!state->AliasedClip.IsNullClip()){CMilRectF clip;state->AliasedClip.GetAsCMilRectF(&clip);CMILSurfaceRect intersection;if(!IntersectAliasedBoundsRectFWithSurfaceRect(clip,viewport,&intersection)){p.clip[2]=p.clip[0];return S_OK;}viewport=intersection;}
        p.viewport[0]=viewport.left;p.viewport[1]=viewport.top;p.viewport[2]=viewport.right;p.viewport[3]=viewport.bottom;if(viewport.IsEmpty()){p.clip[2]=p.clip[0];return S_OK;}
        float iw=1.f/(viewport.right-viewport.left),ih=1.f/(viewport.bottom-viewport.top);CMILMatrix projection;projection.SetToIdentity();projection._11=2*iw;projection._22=-2*ih;projection._41=-(viewport.left*projection._11+1+iw);projection._42=-viewport.top*projection._22+1+ih;
        // 与 CMilEffectDuce::SetupVertexTransform 相同的乘法/转置顺序。
        CMILMatrix scale(float(p.width),0,0,0,0,float(p.height),0,0,0,0,1,0,0,0,0,1),transform;if(worldOverride)transform.SetToMultiplyResult(scale,*worldOverride);else transform.SetToMultiplyResult(scale,state->WorldToDevice);transform.Multiply(projection);transform=transform.transpose();memcpy(p.transform,&transform,sizeof p.transform);
        {float v[]={float(viewport.left),float(viewport.top),float(viewport.right-viewport.left),float(viewport.bottom-viewport.top),0,0};QkTraceTextState("port-effect-viewport",0,0,false,v);}
        for(UINT i=0;i<4;i++){float v[6]{};memcpy(v,p.transform[i],4*sizeof(float));QkTraceTextState("port-effect-matrix",i,0,false,v);}
        return S_OK;
    }
    HRESULT ApplySurfaceEffects(IMILEffectList* effects,float& opacity){HRESULT hr=S_OK;if(effects){UINT count=0;hr=effects->GetCount(&count);if(FAILED(hr))return hr;if(count>32)return Reject("P-E too many bitmap effects");for(UINT i=0;i<count;i++){CLSID id;UINT size=0,resources=0;hr=effects->GetCLSID(i,&id);if(FAILED(hr))return hr;hr=effects->GetParameterSize(i,&size);if(FAILED(hr))return hr;hr=effects->GetResourceCount(i,&resources);if(FAILED(hr))return hr;if(id!=CLSID_MILEffectAlphaScale||size!=sizeof(AlphaScaleParams)||resources!=0)return Reject("P-E bitmap mask or custom effect");AlphaScaleParams alpha;hr=effects->GetParameters(i,sizeof alpha,&alpha);if(FAILED(hr))return hr;if(!_finite(alpha.scale)||alpha.scale<0||alpha.scale>1)return E_INVALIDARG;opacity*=alpha.scale;}}return S_OK;}
    HRESULT PaintTexturedCoverage(CContextState* state,const IShapeData* shape,CBrushRealizer* realizer,CMILBrushBitmap* bitmap,const CMatrix<CoordinateSpace::Shape,CoordinateSpace::Device>* matrix){
        if(bitmap->GetWrapMode()!=MilBitmapWrapMode::Extend||(bitmap->HasSourceClip()&&!bitmap->SourceClipIsEntireSource()))return Reject("P-F tile or partial source clip");
        auto texture=bitmap->GetTextureNoAddRef();if(!texture)return S_OK;IQuickerSurfaceSource* source=nullptr;HRESULT hr=GetBitmapSurface(texture,&source);if(FAILED(hr))return hr;
        // 保持源引用直到 GPU 绘制命令提交；每个退出分支统一释放。
        hr=PaintTexturedSource(state,shape,realizer,bitmap,matrix,source);source->Release();return hr;
    }
    HRESULT PaintTexturedSource(CContextState* state,const IShapeData* shape,CBrushRealizer* realizer,CMILBrushBitmap* bitmap,const CMatrix<CoordinateSpace::Shape,CoordinateSpace::Device>* matrix,IQuickerSurfaceSource* source){
        CRectF<CoordinateSpace::Shape> rawBounds;HRESULT snapHr=shape->GetTightBounds(rawBounds);if(FAILED(snapHr))return snapHr;CShape snapShape;CShapeClipperForFEB corrected(shape,&rawBounds,matrix);snapHr=corrected.ApplyGuidelines(state->m_pSnappingStack,&snapShape);if(FAILED(snapHr))return snapHr;shape=corrected.GetShape();matrix=corrected.GetShapeToDeviceTransformOrNull();
        CMatrix<CoordinateSpace::RealizationSampling,CoordinateSpace::Device> forward;bitmap->GetBitmapToSampleSpaceTransform(*reinterpret_cast<const CMatrix<CoordinateSpace::BaseSampling,CoordinateSpace::Device>*>(&state->WorldToDevice),forward);
        CMatrix<CoordinateSpace::Device,CoordinateSpace::RealizationSampling> inverse;if(!inverse.Invert(forward))return S_OK;
        RECT client{};if(!SurfaceRect(&client))return E_FAIL;MilPointAndSizeL clip={};clip.Width=client.right;clip.Height=client.bottom;
        if(!state->AliasedClip.IsNullClip()){CMilRectF r;state->AliasedClip.GetAsCMilRectF(&r);CMILSurfaceRect whole,intersection;whole.left=0;whole.top=0;whole.right=client.right;whole.bottom=client.bottom;if(!IntersectAliasedBoundsRectFWithSurfaceRect(r,whole,&intersection))return S_OK;clip.X=intersection.left;clip.Y=intersection.top;clip.Width=intersection.right-intersection.left;clip.Height=intersection.bottom-intersection.top;}
        if(!clip.Width||!clip.Height)return S_OK;DynArray<MilPoint2F> points;DynArray<BYTE> types;CHwRasterizer raster;QkCoverageSink sink;HRESULT hr;
        if(state->RenderState->AntiAliasMode==MilAntiAliasMode::None){QkAliasedGeometrySink aliased;CFillTessellator* tessellator=nullptr;CDispensableBuffer<QkAliasedGeometrySink::TessellatorBufferSize,1> local;hr=shape->SetupFillTessellator(matrix,&local,&tessellator);if(SUCCEEDED(hr)&&tessellator)hr=tessellator->SendGeometry(&aliased);delete tessellator;sink.vertices=std::move(aliased.vertices);}
        else{hr=raster.SetupWithClip(clip,shape,&points,&types,matrix);if(SUCCEEDED(hr))hr=raster.SendGeometry(&sink);}
        if(hr==WGXHR_EMPTYFILL)return S_OK;if(FAILED(hr))return hr;if(sink.vertices.empty())return S_OK;
        INT scissor[]={clip.X,clip.Y,clip.X+clip.Width,clip.Y+clip.Height};if(!portClips.empty()){auto r=portClips.back();scissor[0]=max(scissor[0],(INT)r.left);scissor[1]=max(scissor[1],(INT)r.top);scissor[2]=min(scissor[2],(INT)r.right);scissor[3]=min(scissor[3],(INT)r.bottom);}if(scissor[0]>=scissor[2]||scissor[1]>=scissor[3])return S_OK;
        float alpha=realizer->GetOpacityFromRealizedBrush();IMILEffectList* effects=nullptr;hr=realizer->GetRealizedEffectsNoRef(&effects);if(FAILED(hr))return hr;hr=ApplySurfaceEffects(effects,alpha);if(FAILED(hr))return hr;
        void* token=nullptr;UINT width=0,height=0;hr=source->GetSurfaceToken(&token,&width,&height);if(FAILED(hr))return hr;float transform[]={inverse._11,inverse._21,inverse._41,inverse._12,inverse._22,inverse._42};
        typedef HRESULT (__cdecl *DrawFn)(HWND,HWND,const QkCoverageVertex*,UINT,const float*,const INT*,float,bool);auto draw=reinterpret_cast<DrawFn>(GetProcAddress(RootModule(),"QkBitmapCoverageV1"));if(!draw)return E_NOINTERFACE;
        hr=draw(hwnd,static_cast<HWND>(token),sink.vertices.data(),static_cast<UINT>(sink.vertices.size()),transform,scissor,alpha,state->RenderState->InterpolationMode!=MilBitmapInterpolationMode::NearestNeighbor);if(SUCCEEDED(hr))Bump(L"Quicker.BitmapCoverageDraws");return hr;
    }
    HRESULT PaintSurfaceBitmapBrush(CContextState* state,const IShapeData* shape,CBrushRealizer* realizer,CMILBrushBitmap* bitmap,const CMatrix<CoordinateSpace::Shape,CoordinateSpace::Device>* matrix){
        if(state->RenderState->AntiAliasMode!=MilAntiAliasMode::None||!shape->IsAxisAlignedRectangle()||bitmap->GetWrapMode()!=MilBitmapWrapMode::Extend||bitmap->HasSourceClip())return PaintTexturedCoverage(state,shape,realizer,bitmap,matrix);
        auto texture=bitmap->GetTextureNoAddRef();if(!texture)return S_OK;IQuickerSurfaceSource* source=nullptr;HRESULT hr=texture->QueryInterface(IID_IQuickerSurfaceSource,reinterpret_cast<void**>(&source));if(FAILED(hr))return PaintTexturedCoverage(state,shape,realizer,bitmap,matrix);
        CMatrix<CoordinateSpace::RealizationSampling,CoordinateSpace::Device> bitmapToDevice;bitmap->GetBitmapToSampleSpaceTransform(*reinterpret_cast<const CMatrix<CoordinateSpace::BaseSampling,CoordinateSpace::Device>*>(&state->WorldToDevice),bitmapToDevice);
        if(bitmapToDevice._11!=1||bitmapToDevice._22!=1||bitmapToDevice._12!=0||bitmapToDevice._21!=0||bitmapToDevice._41!=floorf(bitmapToDevice._41)||bitmapToDevice._42!=floorf(bitmapToDevice._42)){source->Release();return PaintTexturedCoverage(state,shape,realizer,bitmap,matrix);}
        QkSurfacePacket p{};hr=SurfacePacket(state,source,p,&bitmapToDevice);source->Release();if(FAILED(hr))return hr;CMilRectF boundsF;hr=shape->GetTightBounds(boundsF,nullptr,CMILMatrix::ReinterpretBase(matrix));if(FAILED(hr))return hr;
        if(boundsF.left<bitmapToDevice._41||boundsF.top<bitmapToDevice._42||boundsF.right>bitmapToDevice._41+p.width||boundsF.bottom>bitmapToDevice._42+p.height)return Reject("P-E extended layer bitmap");
        p.clip[0]=max(p.clip[0],(INT)ceilf(boundsF.left));p.clip[1]=max(p.clip[1],(INT)ceilf(boundsF.top));p.clip[2]=min(p.clip[2],(INT)floorf(boundsF.right));p.clip[3]=min(p.clip[3],(INT)floorf(boundsF.bottom));p.alphaScale=realizer->GetOpacityFromRealizedBrush();IMILEffectList* effects=nullptr;hr=realizer->GetRealizedEffectsNoRef(&effects);if(FAILED(hr))return hr;hr=ApplySurfaceEffects(effects,p.alphaScale);if(FAILED(hr))return hr;{float v[]={p.alphaScale,bitmap->GetOpacity(),0,0,0,0};QkTraceTextState("port-layer-opacity",0,0,false,v);}if(!_finite(p.alphaScale)||p.alphaScale<0||p.alphaScale>1)return E_INVALIDARG;if(p.clip[0]>=p.clip[2]||p.clip[1]>=p.clip[3])return S_OK;
        hr=surfaceCompose(hwnd,&p);if(SUCCEEDED(hr))Bump(L"Quicker.SurfaceBitmapBrushes");return hr;
    }
    HRESULT GetBitmapSurface(IWGXBitmapSource* bitmap,IQuickerSurfaceSource** result){
        *result=nullptr;HRESULT hr=bitmap->QueryInterface(IID_IQuickerSurfaceSource,reinterpret_cast<void**>(result));if(SUCCEEDED(hr))return S_OK;
        UINT width=0,height=0;MilPixelFormat::Enum format;hr=bitmap->GetSize(&width,&height);if(FAILED(hr))return hr;hr=bitmap->GetPixelFormat(&format);if(FAILED(hr))return hr;
        if((format!=MilPixelFormat::PBGRA32bpp&&format!=MilPixelFormat::BGRA32bpp&&format!=MilPixelFormat::BGR32bpp)||!width||!height||width>4096||height>4096||UINT64(width)*height*4>64*1024*1024){char detail[128];_snprintf_s(detail,sizeof detail,_TRUNCATE,"P-F unsupported CPU bitmap format=%u width=%u height=%u",static_cast<UINT>(format),width,height);return Reject(detail);}
        typedef HRESULT (__cdecl *UploadFn)(HWND,UINT,UINT,UINT,UINT,const BYTE*);auto upload=reinterpret_cast<UploadFn>(GetProcAddress(RootModule(),"QkSurfaceUploadV1"));if(!upload)return E_NOINTERFACE;
        // 初版逐次上传，后续按可变 bitmap 的 dirty/invalidation 语义建缓存，不能用裸指针作永久身份。
        std::vector<BYTE> pixels;try{pixels.resize(size_t(width)*height*4);}catch(...){return E_OUTOFMEMORY;}
        if(format==MilPixelFormat::PBGRA32bpp)hr=bitmap->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data());
        else{
            // 沿用 hwbitmapcolorsource.cpp 的 WIC 转换入口与参数，不另写预乘/舍入规则。
            IWICBitmapSource* wrapper=nullptr;IWICImagingFactory* factory=nullptr;IWICFormatConverter* converter=nullptr;
            hr=WrapInClosestBitmapInterface(bitmap,&wrapper);if(SUCCEEDED(hr))hr=WICCreateImagingFactory_Proxy(WINCODEC_SDK_VERSION_WPF,&factory);if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
            if(SUCCEEDED(hr))hr=converter->Initialize(wrapper,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0f,WICBitmapPaletteTypeCustom);
            if(SUCCEEDED(hr))hr=converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data());if(converter)converter->Release();if(factory)factory->Release();if(wrapper)wrapper->Release();
        }
        if(FAILED(hr))return hr;
        IntermediateRTUsage usage={};usage.wrapMode=MilBitmapWrapMode::Extend;IMILRenderTargetBitmap* target=nullptr;
        hr=CreateRenderTargetBitmap(width,height,usage,MilRTInitialization::HardwareOnly,&target);if(FAILED(hr))return hr;
        hr=target->QueryInterface(IID_IQuickerSurfaceSource,reinterpret_cast<void**>(result));target->Release();if(FAILED(hr))return hr;
        void* token=nullptr;UINT w=0,h=0;hr=(*result)->GetSurfaceToken(&token,&w,&h);if(SUCCEEDED(hr))hr=upload(static_cast<HWND>(token),width,height,width*4,static_cast<UINT>(pixels.size()),pixels.data());
        if(FAILED(hr)){(*result)->Release();*result=nullptr;}else Bump(L"Quicker.CpuBitmapUploads");return hr;
    }
    STDMETHODIMP DrawBitmap(CContextState* state,IWGXBitmapSource* bitmap,IMILEffectList* effects) override{
        if(!active||!surfaceMode||!bitmap)return Reject("P-E bitmap source or effects");IQuickerSurfaceSource* source=nullptr;HRESULT hr=GetBitmapSurface(bitmap,&source);if(FAILED(hr))return hr;QkSurfacePacket p{};hr=SurfacePacket(state,source,p);if(SUCCEEDED(hr))hr=ApplySurfaceEffects(effects,p.alphaScale);if(SUCCEEDED(hr)&&p.clip[0]<p.clip[2]&&p.clip[1]<p.clip[3])hr=surfaceCompose(hwnd,&p);source->Release();return hr;
    }
    STDMETHODIMP DrawMesh3D(CContextState*,BrushContext*,CMILMesh3D*,CMILShader*,IMILEffectList*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP DrawInfinitePath(CContextState*,BrushContext*,CBrushRealizer*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP SetClearTypeHint(bool force) override{forceClearType=force;return S_OK;}
    STDMETHODIMP DrawGlyphs(DrawGlyphsParameters& pars) override{
        if(!active||!portText||!glyph)return Reject("P-T disabled");
        auto hr=pars.pBrushRealizer->EnsureRealization(CMILResourceCache::SwRealizationCacheIndex,DisplayId::None,pars.pBrushContext,pars.pContextState,static_cast<IRenderTargetInternal*>(this));if(FAILED(hr))return hr;
        if(!pars.pBrushRealizer->GetRealizedBrushNoRef(false))return S_OK;
        QkGlyphPacket packet{};QkPortGlyphPainter painter;bool visible=false;hr=painter.Build(pars,glyphMemory,forceClearType,packet,visible);if(FAILED(hr))return Reject("P-T brush or realization");if(!visible)return S_OK;
        RECT client{};if(!SurfaceRect(&client))return HRESULT_FROM_WIN32(GetLastError());packet.clip[2]=client.right;packet.clip[3]=client.bottom;
        if(!pars.pContextState->AliasedClip.IsNullClip()){
            CMilRectF clip;pars.pContextState->AliasedClip.GetAsCMilRectF(&clip);CMILSurfaceRect whole,intersection;whole.left=0;whole.top=0;whole.right=client.right;whole.bottom=client.bottom;if(!IntersectAliasedBoundsRectFWithSurfaceRect(clip,whole,&intersection))return S_OK;packet.clip[0]=intersection.left;packet.clip[1]=intersection.top;packet.clip[2]=intersection.right;packet.clip[3]=intersection.bottom;
        }
        if(!portClips.empty()){auto clip=portClips.back();packet.clip[0]=max(packet.clip[0],(INT)clip.left);packet.clip[1]=max(packet.clip[1],(INT)clip.top);packet.clip[2]=min(packet.clip[2],(INT)clip.right);packet.clip[3]=min(packet.clip[3],(INT)clip.bottom);}
        CMILSurfaceRect surface;surface.left=0;surface.top=0;surface.right=client.right;surface.bottom=client.bottom;
        if(!portViewports.empty()){auto v=portViewports.back();surface.left=v.left;surface.top=v.top;surface.right=v.right;surface.bottom=v.bottom;}
        if(!pars.pContextState->AliasedClip.IsNullClip()){CMilRectF v;pars.pContextState->AliasedClip.GetAsCMilRectF(&v);CMILSurfaceRect intersection;if(!IntersectAliasedBoundsRectFWithSurfaceRect(v,surface,&intersection))return S_OK;surface=intersection;}
        packet.viewport[0]=surface.left;packet.viewport[1]=surface.top;packet.viewport[2]=surface.right;packet.viewport[3]=surface.bottom;
        {float v[]={float(surface.left),float(surface.top),float(surface.right-surface.left),float(surface.bottom-surface.top),0,0};QkTraceTextState("port-viewport",0,0,false,v);}
        if(packet.clip[0]>=packet.clip[2]||packet.clip[1]>=packet.clip[3])return S_OK;
        hr=glyph(hwnd,&packet);if(SUCCEEDED(hr))Bump(L"Quicker.PortGlyphCalls");return hr;
    }
    STDMETHODIMP DrawVideo(CContextState*,IAVSurfaceRenderer*,IWGXBitmapSource*,IMILEffectList*) override{return Reject(__FUNCTION__);}
    STDMETHODIMP ComposeEffect(CContextState* state,CMILMatrix* scale,CMilEffectDuce* effect,UINT width,UINT height,IMILRenderTargetBitmap* input) override{
        if(!active||!surfaceMode||!scale||!input)return Reject("P-E effect target");HRESULT hr=CheckSurfaceEffect(effect);if(FAILED(hr))return hr;IQuickerSurfaceSource* source=nullptr;hr=input->QueryInterface(IID_IQuickerSurfaceSource,reinterpret_cast<void**>(&source));if(FAILED(hr))return Reject("P-E non-native effect input");QkSurfacePacket p{};hr=SurfacePacket(state,source,p);source->Release();if(FAILED(hr))return hr;if(width!=p.width||height!=p.height)return E_INVALIDARG;UINT limit=0;bool fp128=false;hr=GetSurfaceCapabilities(&limit,&fp128);if(FAILED(hr))return hr;
        MilKernelType::Enum kernel=MilKernelType::Gaussian;
        if(effect->IsOfType(TYPE_BLUREFFECT)){auto blur=static_cast<CMilBlurEffectDuce*>(effect);p.kind=1;blur->GetScaledRadius(scale,&p.radius);kernel=blur->m_data.m_KernelType;p.quality=blur->m_data.m_RenderingBias==MilEffectRenderingBias::Quality&&fp128;}
        else{auto shadow=static_cast<CMilDropShadowEffectDuce*>(effect);p.kind=2;CMilBlurEffectDuce::ApplyRadiusScaling(scale,static_cast<UINT>(shadow->GetBlurRadius()),&p.radius);p.opacity=static_cast<float>(shadow->GetOpacity());auto color=shadow->GetColor();p.color[0]=color.r;p.color[1]=color.g;p.color[2]=color.b;p.color[3]=color.a;shadow->CalculateOffset(true,scale,&p.offset[0],&p.offset[1]);p.quality=shadow->m_data.m_RenderingBias==MilEffectRenderingBias::Quality&&fp128;}
        {float v[]={float(p.width),float(p.height),p.offset[0],p.offset[1],p.opacity,float(p.kind)};QkTraceTextState("port-effect",p.radius,p.quality,false,v);}
        if(p.radius>100)return E_INVALIDARG;if(p.radius){float* weights=p.weights;CMilBlurEffectDuce::CalculateSamplingWeights(p.radius,&weights,kernel);}else p.weights[0]=1;
        if(p.clip[0]>=p.clip[2]||p.clip[1]>=p.clip[3])return S_OK;hr=surfaceCompose(hwnd,&p);if(SUCCEEDED(hr))Bump(L"Quicker.PortEffects");return hr;
    }
    STDMETHODIMP Begin3D(const MilRectF&,MilAntiAliasMode::Enum,bool,FLOAT) override{return Reject(__FUNCTION__);}
    STDMETHODIMP End3D() override{return Reject(__FUNCTION__);}
};
