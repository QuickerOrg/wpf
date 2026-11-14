// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.


//-----------------------------------------------------------------------------
//

//
//  Description:
//      Contains CD3DSwapChain implementation
//
//      Abstracts the core D3D swapchain.  The main reason to wrap this d3d
//      object is so that we can respond to mode changes, i.e.,   respond to
//      D3DERR_DEVICELOST on the Present.
//

#include "precomp.hpp"

MtDefine(CD3DSwapChain, MILRender, "CD3DSwapChain");
MtDefine(D3DResource_SwapChain, MILHwMetrics, "Approximate swap chain size");

MtExtern(CD3DSwapChainWithSwDC);

//+----------------------------------------------------------------------------
//
//  Member:
//      CD3DSwapChain::Create
//
//  Synopsis:
//      Create the CD3DSwapChain wrapper from an IDirect3DSwapChain9
//
//  Notes:
//      This class behaves very differently depending on whether a present
//      context is passed in. If a present context is supplied, the swap chain
//      will implement GetDC by copying the backbuffer to a software GDI DIB
//      section. Even if GetDC is never called, this DIB section would be
//      created.
//

HRESULT 
CD3DSwapChain::Create(
    __inout_ecount(1) CD3DResourceManager *pResourceManager,
    __inout_ecount(1) IDirect3DSwapChain9 *pID3DSwapChain,
    UINT BackBufferCount,
    __in_ecount_opt(1) CMILDeviceContext const *pPresentContext,    
    __deref_out_ecount(1) CD3DSwapChain **ppSwapChain
    )
{
#if QUICKER_WPF_D3D11_FACADE
    *ppSwapChain = nullptr; return QkFacadeUnimpl("CD3DSwapChain::Create.legacy");
#else

    HRESULT hr = S_OK;

    *ppSwapChain = NULL;

    //
    // Look up back buffer count if not given
    //      code is also in CD3DSwapChainWithSwDC  
    //

    if (BackBufferCount == 0)
    {
        D3DPRESENT_PARAMETERS d3dpp;

        IFC(pID3DSwapChain->GetPresentParameters(&d3dpp));

        BackBufferCount = d3dpp.BackBufferCount;
    }

    //
    // Create the swap chain wrapper
    //

    if (pPresentContext)
    {
        Assert(!pPresentContext->PresentWithHAL());

        HDC hdcPresentVia = NULL;

        IFC(pPresentContext->CreateCompatibleDC(&hdcPresentVia));

        *ppSwapChain = new(BackBufferCount) CD3DSwapChainWithSwDC(
            hdcPresentVia,
            BackBufferCount,
            pID3DSwapChain
            );

        //
        // If the swapchain was not successfully allocated, then it has not
        // taken ownership of the DC and the DC must now be deleted.  Otherwise
        // let the swapchain own it.
        //
        if (!*ppSwapChain)
        {
            DeleteDC(hdcPresentVia);
        }
    }
    else
    {
        *ppSwapChain = new(BackBufferCount) CD3DSwapChain(
            pID3DSwapChain,
            BackBufferCount,
            NULL // prgBackBuffers
            );
    }
    IFCOOM(*ppSwapChain);
    (*ppSwapChain)->AddRef(); // CD3DSwapChain::ctor sets ref count == 0

    //
    // Call init
    //

    IFC((*ppSwapChain)->Init(pResourceManager));

Cleanup:
    if (FAILED(hr))
    {
        ReleaseInterface(*ppSwapChain);
    }
    RRETURN(hr);

#endif
}

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::operator new
//
//  Synopsis:  Allocate memory for a CD3DSwapChain with cBackBuffers number of
//             back buffer surface.  Extra space is allocated for back buffer
//             pointer array. 
//
//-----------------------------------------------------------------------------

__allocator __bcount_opt(cb + cBackBuffers*sizeof(PVOID) /* Prefast can't deal with type-modifiers, so using PVOID */)
void * __cdecl CD3DSwapChain::operator new(
    size_t cb,
    size_t cBackBuffers
    )
{
    void *pNew = NULL;

    // Calculate extra space for CD3DSurface array (m_rgBackBuffers). The order
    // of memory layout is this followed by back buffer array.
    size_t cbExtraData = cBackBuffers * sizeof(CD3DSurface *);

    // Check for overflow
    if (cbExtraData > cBackBuffers)
    {
        cb += cbExtraData;

        // Check for overflow
        if (cb >= cbExtraData)
        {
            pNew = WPFAlloc(ProcessHeap,
                                 Mt(CD3DSwapChain),
                                 cb);
        }
    }

    return pNew;
}

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::CD3DSwapChain
//
//  Synopsis:  ctor
//
//-----------------------------------------------------------------------------

#pragma warning( push )
// Allow use of this in constructor
#pragma warning( disable : 4355 )

CD3DSwapChain::CD3DSwapChain(
    __inout_ecount(1) IDirect3DSwapChain9 *pD3DSwapChain,
    __in_range(>, 0) __out_range(==, this->m_cBackBuffers) UINT cBackBuffers,
    __out_ecount_full_opt(cBackBuffers) CD3DSurface * * const prgBackBuffers
        // required for subclasses
    )
    : m_pD3DSwapChain(pD3DSwapChain),
      m_pD3DSwapChainEx(NULL),
      m_cBackBuffers(cBackBuffers),
      m_rgBackBuffers(
            prgBackBuffers
          ? prgBackBuffers
          : reinterpret_cast<CD3DSurface **>(reinterpret_cast<BYTE *>(this) + sizeof(*this))
            )
{
    RtlZeroMemory(m_rgBackBuffers, m_cBackBuffers * sizeof(*m_rgBackBuffers));
#if QUICKER_WPF_D3D11_FACADE
    if (m_pD3DSwapChain) m_pD3DSwapChain->AddRef();
#else
    m_pD3DSwapChain->AddRef();
#endif
    
#if QUICKER_WPF_D3D11_FACADE
    if (m_pD3DSwapChain)
#endif
    IGNORE_HR(m_pD3DSwapChain->QueryInterface(IID_IDirect3DSwapChain9Ex,
                                              reinterpret_cast<void **>(&m_pD3DSwapChainEx)));

}

#pragma warning( pop )

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::~CD3DSwapChain
//
//  Synopsis:  dtor
//
//-----------------------------------------------------------------------------
CD3DSwapChain::~CD3DSwapChain()
{
#if QUICKER_WPF_D3D11_FACADE
    if(m_facadeSwapChain) QkFacadeCount(QkFacadeCounter::SwapChainRelease);
#endif
    for (UINT i = 0; i < m_cBackBuffers; i++)
    {
        ReleaseInterfaceNoNULL(m_rgBackBuffers[i]);
    }

    ReleaseInterfaceNoNULL(m_pD3DSwapChain);
    ReleaseInterfaceNoNULL(m_pD3DSwapChainEx);
}

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::Init
//
//  Synopsis:  Inits the swap chain
//
//-----------------------------------------------------------------------------
HRESULT 
CD3DSwapChain::Init(
    __inout_ecount(1) CD3DResourceManager *pResourceManager
    )
{
#if QUICKER_WPF_D3D11_FACADE
    return QkFacadeUnimpl("CD3DSwapChain::Init");
#else
    HRESULT hr = S_OK;

    Assert(pResourceManager);

    //
    // Init the base class
    //

    CD3DResource::Init(pResourceManager, 0);

    //
    // Store array of back buffers
    //

    for (UINT i = 0; i < m_cBackBuffers; i++)
    {
        QkSurfaceHandle *pD3DBackBuffer = NULL;

        IFC(m_pD3DSwapChain->GetBackBuffer(i,  D3DBACKBUFFER_TYPE_MONO, &pD3DBackBuffer));

        MIL_THR(CD3DSurface::Create(pResourceManager, pD3DBackBuffer, &m_rgBackBuffers[i]));

        ReleaseInterfaceNoNULL(pD3DBackBuffer);

        if (FAILED(hr))
        {
            goto Cleanup;
        }
    }

Cleanup:
    RRETURN(hr);
#endif
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::GetBackBuffer
//
//  Synopsis:  Get indexed back buffer from array
//
//-------------------------------------------------------------------------
HRESULT 
CD3DSwapChain::GetBackBuffer(
    __in_range(<, this->m_cBackBuffers) UINT iBackBuffer,
    __deref_out_ecount(1) CD3DSurface **ppBackBuffer
    ) const
{
    HRESULT hr = S_OK;

    Assert(IsValid());

    if (iBackBuffer >= m_cBackBuffers)
    {
        IFC(WGXERR_INVALIDPARAMETER);
    }

    *ppBackBuffer = m_rgBackBuffers[iBackBuffer];
    (*ppBackBuffer)->AddRef();

Cleanup:
    RRETURN(hr);
}

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSwapChain::ReleaseD3DResources
//
//  Synopsis:  Release the swap chain.  Note that all callers of public
//             methods should have called IsValid before using the swapchain,
//             so it should be ok to blow away our internal swap chain here.
//
//             This method may only be called by CD3DResourceManager because
//             there are restrictions around when a call to ReleaseD3DResources
//             is okay.
//
//-----------------------------------------------------------------------------
void 
CD3DSwapChain::ReleaseD3DResources()
{
    // This resource should have been marked invalid already or at least be out
    // of use.
    Assert(!m_fResourceValid || (m_cRef == 0));
    Assert(IsValid() == m_fResourceValid);

    // This context is protected so it is safe to release the D3D resource
    ReleaseInterface((*const_cast<IDirect3DSwapChain9 **>(&m_pD3DSwapChain)));
    ReleaseInterface((*const_cast<IDirect3DSwapChain9Ex **>(&m_pD3DSwapChainEx)));

#if QUICKER_WPF_D3D11_FACADE
    m_facadePresentHistory.Reset();
    m_facadeSwapChain.Reset();
#endif
    // Also release reference to wrapper resources for each of the back buffers.
    for (UINT i = 0; i < m_cBackBuffers; i++)
    {
        ReleaseInterface(m_rgBackBuffers[i]);
    }

    return;
}

//+------------------------------------------------------------------------
//
//  Member:
//      CD3DSwapChain::GetDC
// 
//  Synopsis:
//      Gets the DC for the specified backbuffer
//

HRESULT
CD3DSwapChain::GetDC(
    __in_range(<, this->m_cBackBuffers) UINT iBackBuffer,
    __in_ecount(1) const CMilRectU& rcDirty,
    __deref_out HDC *phdcBackBuffer
    ) const
{
    HRESULT hr = S_OK;

    UNREFERENCED_PARAMETER(rcDirty);

    Assert(iBackBuffer < m_cBackBuffers);

    IFC(m_rgBackBuffers[iBackBuffer]->GetDC(
        phdcBackBuffer
        ));

Cleanup:
    RRETURN(hr);
}


//+----------------------------------------------------------------------------
//
//  Member:
//      CD3DSwapChain::ReleaseDC
//
//  Synopsis:
//      Releases the DC returned by GetDC if necessary
//

HRESULT
CD3DSwapChain::ReleaseDC(
    __in_range(<, this->m_cBackBuffers) UINT iBackBuffer,
    __in HDC hdcBackBuffer
    ) const
{
    RRETURN(m_rgBackBuffers[iBackBuffer]->ReleaseDC(
        hdcBackBuffer
        ));
}





#if QUICKER_WPF_D3D11_FACADE
// 仅本进程的确定性故障注入，用于复验健康设备的呈现恢复分支；不影响系统设备。
static bool QkPresentDenial(const WCHAR *phase)
{
    WCHAR value[16] = {};
    return GetEnvironmentVariableW(L"QUICKER_FACADE_EXPERIMENT_PRESENT_DENIAL", value, ARRAYSIZE(value))
        && _wcsicmp(value, phase) == 0;
}
HRESULT CD3DSwapChain::CreateFacade(CD3DResourceManager *manager,QkD3D11State *state,UINT width,UINT height,D3DFORMAT format,CD3DSwapChain **out,HWND opaqueHwnd)
{
    *out=nullptr; CD3DSwapChain *s=new(1) CD3DSwapChain(nullptr,1,nullptr); if(!s) return E_OUTOFMEMORY;
    s->AddRef(); s->CD3DResource::Init(manager,0);
    HRESULT hr=S_OK;
    s->m_facadeState=state;
    if(opaqueHwnd) {
        ComPtr<IDXGIDevice> device; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        hr=state->device.As(&device);
        if(SUCCEEDED(hr)) hr=device->GetAdapter(&adapter);
        if(SUCCEEDED(hr)) hr=adapter->GetParent(IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC1 desc={}; desc.Width=width; desc.Height=height;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.Scaling=DXGI_SCALING_STRETCH; desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        // WPF 保留目标独立于轮换的 DXGI 后缓冲，局部更新只做 GPU 拷贝。
        if(SUCCEEDED(hr)) hr=QkPresentDenial(L"Create") ? E_ACCESSDENIED : factory->CreateSwapChainForHwnd(state->device.Get(),opaqueHwnd,&desc,nullptr,nullptr,&s->m_facadeSwapChain);
        if(s->m_facadeSwapChain) QkFacadeCount(QkFacadeCounter::SwapChainCreate);
        if(SUCCEEDED(hr)) hr=factory->MakeWindowAssociation(opaqueHwnd,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        if(hr==E_ACCESSDENIED && SUCCEEDED(state->DeviceStatus())) {
            // 旧 flip 的延迟销毁或多个显示目标关联同 HWND 时，不能让整窗转成 CPU 光栅化。
            if(s->m_facadeSwapChain) QkFacadeCount(QkFacadeCounter::SwapChainRelease);
            s->m_facadeSwapChain.Reset(); state->context->Flush();
            s->m_facadeGdiPresent=true;
            QkFacadeTrace("QK-FACADE fallback=GPU-GDI reason=flip-access-denied\n");
            hr=S_OK;
        }
        char line[240]; StringCchPrintfA(line,240,"QK-FACADE DXGI flip hwnd=%p size=%ux%u hr=0x%08x\n",opaqueHwnd,width,height,hr); QkFacadeTrace(line);
    }
    // 物理纹理均为 BGRA8，但逻辑 XRGB/ARGB 语义必须保留，否则原文字路径会错误禁用 ClearType。
    if(SUCCEEDED(hr)) hr=CD3DSurface::CreateFacade(manager,state,width,height,format,&s->m_rgBackBuffers[0]);
    if(FAILED(hr)){s->Release();return hr;}
    *out=s; return S_OK;
}
HRESULT CD3DSwapChain::ResizeFacade(CD3DResourceManager *manager,UINT width,UINT height)
{
    QkFacadeCount(QkFacadeCounter::SwapChainResizeRequest);
    if(!m_facadeResizeRequired && !QkPresentDenial(L"Resize") && m_rgBackBuffers[0] &&
        m_rgBackBuffers[0]->Desc().Width==width && m_rgBackBuffers[0]->Desc().Height==height &&
        SUCCEEDED(m_facadeState->DeviceStatus())) {
        // 同尺寸更新仍按原约定使内容失效、清空显示历史并完整呈现；仅保留交换链、逻辑纹理和视图。
        // 非法 Present 的恢复及显式故障注入必须继续走原 resize/recreate 分支。
        m_facadePresentHistory.Reset();
        m_facadeFirstPresent=true;
        if(m_rgBackBuffers[0]->Desc().Format==D3DFMT_X8R8G8B8) {
            const float opaqueBlack[4]={0,0,0,1};
            m_facadeState->context->ClearRenderTargetView(m_rgBackBuffers[0]->m_facadeRTV.Get(),opaqueBlack);
            HRESULT status=m_facadeState->DeviceStatus();
            auto state=m_rgBackBuffers[0]->GetD3DSurfaceNoAddRef()->opaqueState;
            if(state) state->alphaKnownOpaque=SUCCEEDED(status);
            if(FAILED(status)) return status;
        }
        QkFacadeCount(QkFacadeCounter::SwapChainReuse);
        return S_OK;
    }
    // 原 UpdateFlippingChain 已释放目标表面；没有任何跨帧 DXGI buffer/view 引用。
    m_facadeState->context->OMSetRenderTargets(0,nullptr,nullptr);
    LONGLONG resizeStarted=QkFacadeClock();
    // 已恢复为 GPU→GDI 的目标继续复用该呈现方式，resize/最小化恢复不再反复争用 HWND 的 flip 关联。
    HRESULT hr=S_OK;
    if(!m_facadeGdiPresent) {
        if(QkPresentDenial(L"Resize")) hr=E_ACCESSDENIED;
        else { QkFacadeCount(QkFacadeCounter::SwapChainResize); hr=m_facadeSwapChain->ResizeBuffers(2,width,height,DXGI_FORMAT_UNKNOWN,0); }
    }
    QkFacadeTiming("ResizeBuffers",resizeStarted,hr,width);
    if(FAILED(hr) && FAILED(m_facadeState->DeviceStatus())) return WGXERR_DISPLAYSTATEINVALID;
    if(hr==E_ACCESSDENIED) {
        // HWND 透明/不透明切换后，旧 flip 绑定可能已失效；健康设备只做一次解除绑定并重建。
        // 不能把该局部交换链错误直接交给 WPF，导致整个 HWND 静默退回软件。
        DXGI_SWAP_CHAIN_DESC oldDesc={};
        HRESULT query=m_facadeSwapChain->GetDesc(&oldDesc);
        if(SUCCEEDED(query) && IsWindow(oldDesc.OutputWindow)) {
            DetachFacade();
            ComPtr<IDXGIDevice> device; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
            hr=m_facadeState->device.As(&device);
            if(SUCCEEDED(hr)) hr=device->GetAdapter(&adapter);
            if(SUCCEEDED(hr)) hr=adapter->GetParent(IID_PPV_ARGS(&factory));
            DXGI_SWAP_CHAIN_DESC1 desc={}; desc.Width=width; desc.Height=height;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
            desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
            desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;desc.Scaling=DXGI_SCALING_STRETCH;desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
            LONGLONG recreateStarted=QkFacadeClock();
            if(SUCCEEDED(hr)) hr=QkPresentDenial(L"Resize") ? E_ACCESSDENIED : factory->CreateSwapChainForHwnd(m_facadeState->device.Get(),oldDesc.OutputWindow,&desc,nullptr,nullptr,&m_facadeSwapChain);
            if(m_facadeSwapChain) QkFacadeCount(QkFacadeCounter::SwapChainCreate);
            if(SUCCEEDED(hr)) hr=factory->MakeWindowAssociation(oldDesc.OutputWindow,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
            QkFacadeTiming("ResizeRecreate",recreateStarted,hr,width);
            char line[192]; StringCchPrintfA(line,ARRAYSIZE(line),"QK-FACADE resize-recreate hwnd=%p hr=0x%08x\n",oldDesc.OutputWindow,hr);QkFacadeTrace(line);
        }
    }
    if(hr==E_ACCESSDENIED && SUCCEEDED(m_facadeState->DeviceStatus())) {
        DetachFacade(); m_facadeGdiPresent=true;
        QkFacadeTrace("QK-FACADE fallback=GPU-GDI reason=resize-access-denied\n");
        hr=S_OK;
    }
    if(FAILED(hr)) return hr;
    m_facadePresentHistory.Reset();
    D3DFORMAT format=m_rgBackBuffers[0]->Desc().Format;
    ReleaseInterface(m_rgBackBuffers[0]);
    hr=CD3DSurface::CreateFacade(manager,m_facadeState.Get(),width,height,format,&m_rgBackBuffers[0]);
    m_facadeFirstPresent=true;
    if(SUCCEEDED(hr)) m_facadeResizeRequired=false;
    return hr;
}
#endif
