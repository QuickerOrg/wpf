// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.


//-----------------------------------------------------------------------------
//

//
//  Description:
//      Contains CD3DSurface implementation
//
//      Provides basic abstraction of a D3D surface and tracks it as a D3D
//      resource.
//

#include "precomp.hpp"

MtDefine(CD3DSurface, MILRender, "CD3DSurface");
MtDefine(D3DResource_Surface, MILHwMetrics, "Approximate surface sizes");

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::Create
//
//  Synopsis:  Create a CD3DSurface object to wrap a D3D texture
//
//-------------------------------------------------------------------------
HRESULT 
CD3DSurface::Create(
    __inout_ecount(1) CD3DResourceManager *pResourceManager,
    __inout_ecount(1) QkSurfaceHandle *pD3DSurface,
    __deref_out_ecount(1) CD3DSurface **ppSurface
    )
{
#if QUICKER_WPF_D3D11_FACADE
    *ppSurface=nullptr;
    if(!pD3DSurface) return E_INVALIDARG;
    CD3DSurface *s=new CD3DSurface(pD3DSurface); if(!s) return E_OUTOFMEMORY;
    s->AddRef(); HRESULT hr=s->Init(pResourceManager);
    if(FAILED(hr)) s->Release(); else *ppSurface=s;
    return hr;
#else

    HRESULT hr = S_OK;

    *ppSurface = NULL;

    //
    // Create the D3D surface wrapper
    //

    *ppSurface = new CD3DSurface(pD3DSurface);
    IFCOOM(*ppSurface);
    (*ppSurface)->AddRef(); // CD3DSurface::ctor sets ref count == 0

    //
    // Call init
    //

    IFC((*ppSurface)->Init(
        pResourceManager
        ));

Cleanup:
    if (FAILED(hr))
    {
        ReleaseInterface(*ppSurface);
    }
    RRETURN(hr);

#endif
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::CD3DSurface
//
//  Synopsis:  ctor
//
//-------------------------------------------------------------------------
CD3DSurface::CD3DSurface(
    __inout_ecount(1) QkSurfaceHandle * const pD3DSurface
    )
    : m_pD3DSurface(pD3DSurface)
{
#if QUICKER_WPF_D3D11_FACADE
    if (m_pD3DSurface) m_pD3DSurface->AddRef();
#else
    m_pD3DSurface->AddRef();
#endif
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::~CD3DSurface
//
//  Synopsis:  dtor
//
//-------------------------------------------------------------------------
CD3DSurface::~CD3DSurface()
{
    ReleaseInterfaceNoNULL(m_pD3DSurface);
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::Init
//
//  Synopsis:  Inits the surface wrapper
//
//-------------------------------------------------------------------------
HRESULT 
CD3DSurface::Init(
    __inout_ecount(1) CD3DResourceManager *pResourceManager
    )
{
    HRESULT hr = S_OK;
    UINT uResourceSize;

    //
    // Compute the size of the resource
    //

    IFC(m_pD3DSurface->GetDesc(&m_d3dsd));
#if QUICKER_WPF_D3D11_FACADE
    m_facadeTexture=m_pD3DSurface->texture; m_facadeRTV=m_pD3DSurface->rtv; m_facadeGeneration=m_pD3DSurface->generation;
#endif

    if (m_d3dsd.Pool == D3DPOOL_SYSTEMMEM)
    {
        uResourceSize = 0;
    }
    else
    {
        UINT uSamplesPerPixel = 1;
    
        if (m_d3dsd.MultiSampleType >= D3DMULTISAMPLE_2_SAMPLES)
        {
            C_ASSERT(D3DMULTISAMPLE_2_SAMPLES == 2);
            uSamplesPerPixel = static_cast<UINT>(m_d3dsd.MultiSampleType);
        }
    
        uResourceSize = D3DFormatSize(m_d3dsd.Format) * m_d3dsd.Width * m_d3dsd.Height * uSamplesPerPixel;
        Assert(uResourceSize > 0);
    }

    //
    // Init the base class
    //

    CD3DResource::Init(pResourceManager, uResourceSize);

Cleanup:
    RRETURN(hr);
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::ReleaseD3DResources
//
//  Synopsis:  Release the surface.
//
//             This method may only be called by CD3DResourceManager because
//             there are various restrictions around when a call to
//             ReleaseD3DResources is okay.
//
//-------------------------------------------------------------------------
void 
CD3DSurface::ReleaseD3DResources()
{
    // This resource should have been marked invalid already or at least be out
    // of use.
    Assert(!m_fResourceValid || (m_cRef == 0));
    Assert(IsValid() == m_fResourceValid);

    if (m_d3dsd.Usage & D3DUSAGE_RENDERTARGET)
    {
        Device().ReleaseUseOfRenderTarget(this);
    }

    if (m_d3dsd.Usage & D3DUSAGE_DEPTHSTENCIL)
    {
        Device().ReleaseUseOfDepthStencilSurface(this);
    }

#if QUICKER_WPF_D3D11_FACADE
    m_facadeGDI.Reset(); m_facadeRTV.Reset(); m_facadeTexture.Reset();
#endif
    // This context is protected so it is safe to release the D3D resource
    ReleaseInterface((*const_cast<QkSurfaceHandle **>(&m_pD3DSurface)));

    return;
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::GetSurfaceSize
//
//  Synopsis:  Return the dimensions of the surface.
//
//-------------------------------------------------------------------------
void
CD3DSurface::GetSurfaceSize(
    __out_ecount(1) UINT *puWidth,
    __out_ecount(1) UINT *puHeight
    ) const
{
    Assert(IsValid());

    *puWidth = m_d3dsd.Width;
    *puHeight = m_d3dsd.Height;
}

//+------------------------------------------------------------------------
//
//  Member:    CD3DSurface::GetDC
//
//  Synopsis:  Retrieve a DC wrapping this surface
//
//-------------------------------------------------------------------------
HRESULT 
CD3DSurface::GetDC(
    __out_ecount(1) HDC *phdc
    )
{
#if QUICKER_WPF_D3D11_FACADE
    *phdc=nullptr; auto state=Device().Facade();
    QkFacadeCount(QkFacadeCounter::GetDC);
    state->context->OMSetRenderTargets(0,nullptr,nullptr);
    return m_facadeGDI->GetDC(FALSE,phdc);
#else

    HRESULT hr = S_OK;

    HDC hBackBuffer = NULL;

    *phdc = NULL;

    MIL_THR(m_pD3DSurface->GetDC(
        &hBackBuffer
        ));

    //
    // D3D isn't supposed to return E_FAIL to us.  We want to see if we can 
    // reinterpret it.
    //
    if (hr == E_FAIL)
    {
        //
        // Check to see if we're close to being out of available GDI handles.
        // If we are we'll evaluate that as E_OUTOFMEMORY, otherwise we'll
        // return D3DERR_DRIVERINTERNALERROR.
        //
        IFC(CheckGUIHandleQuota(
            GR_GDIOBJECTS,
            E_OUTOFMEMORY,
            D3DERR_DRIVERINTERNALERROR
            ));
    }

    //
    // If we've succeeded assign the Handle to return.
    //
    if (SUCCEEDED(hr))
    {
        *phdc = hBackBuffer;
        hBackBuffer = NULL;
    }

Cleanup:
    //
    // If we have a backbuffer handle at this point it means 
    // something failed.
    //
    if (hBackBuffer)
    {
        IGNORE_HR(m_pD3DSurface->ReleaseDC(hBackBuffer));
    }

    RRETURN(hr);

#endif
}

//+----------------------------------------------------------------------------
//
//  Member:    CD3DSurface::ReadIntoSysMemBuffer
//
//  Synopsis:  Reads the surface's contents in the specified source rectangle
//             and writes it into the provided buffer.
//
//             If cClipRects is non-zero, then writes are restricted to the
//             intersection of rcSource and rectangles in rgClipRects.  For
//             best performance clipping rectangles should not overlap one
//             another.
//
//             Note: the output buffer might be sparsely filled if nStrideOut
//                   is larger than the stride of the source in the given
//                   rectangle.
//
//             IMPORTANT: HwUtil.cpp's ReadRenderTargetIntoSysMemBuffer borrows 
//             heavily from this so it should be kept in sync with this 
//             implementation
//             
//-----------------------------------------------------------------------------
HRESULT
CD3DSurface::ReadIntoSysMemBuffer(
    __in_ecount(1) const CMilRectU &rcSource,
    UINT cClipRects,
    __in_ecount_opt(cClipRects) const CMilRectU *rgClipRects,
    MilPixelFormat::Enum fmtOut,
    UINT nStrideOut,
    DBG_ANALYSIS_PARAM_COMMA(UINT cbBufferOut)
    __out_bcount_full(cbBufferOut) BYTE *pbBufferOut
    )
{
#if QUICKER_WPF_D3D11_FACADE
    return QkFacadeUnimpl("CD3DSurface::ReadIntoSysMemBuffer");
#else

    HRESULT hr = S_OK;
    QkSurfaceHandle *pD3DLockableSurface = NULL;
    QkSurfaceHandle *pTempSurface = NULL;
    
    bool fNeedToManuallyCopyBits = true;
    UINT nStrideCopy;

    UINT const uSourceWidth = rcSource.Width();
    UINT const uSourceHeight = rcSource.Height();

    RECT const rcDest = { 0, 0, uSourceWidth, uSourceHeight };

    BYTE const BitsPerPixel = GetPixelFormatSize(fmtOut);

    if (BitsPerPixel % BITS_PER_BYTE)
    {
        TraceTag((tagMILWarning,
                  "Call to CHwDeviceBitmapColorSource::CopyPixels requested fraction byte copy"));
        IFC(WGXERR_INVALIDPARAMETER);
    }

    IFC(HrCalcByteAlignedScanlineStride(uSourceWidth, BitsPerPixel, nStrideCopy));

#if DBG_ANALYSIS
    Assert(nStrideCopy <= nStrideOut);
    Assert(SUCCEEDED(HrCheckBufferSize(fmtOut, nStrideOut, uSourceWidth, uSourceHeight, cbBufferOut)));
#endif

    Assert(Device().IsInAUseContext());

    D3DFORMAT d3dfmtOut = PixelFormatToD3DFormat(fmtOut);

    if ((m_d3dsd.Pool == D3DPOOL_MANAGED) || (m_d3dsd.Pool == D3DPOOL_SYSTEMMEM))
    {
        if (d3dfmtOut != m_d3dsd.Format)
        {
            IFC(WGXERR_INVALIDCALL);
        }

        SetInterface(pD3DLockableSurface, ID3DSurface());
    }
    else
    {
        //
        // Create a lockable copy or wrapper around bit buffer and copy
        //

#if DBG
        {
            // Double check that we have the right device
            IDirect3DDevice9 *pID3DDevice = NULL;
            if (SUCCEEDED(ID3DSurface()->GetDevice(&pID3DDevice)))
            {
                Assert(pID3DDevice == Device().DbgGetID3DDevice9());
            }
            ReleaseInterface(pID3DDevice);
        }
#endif
        void* pvSysMemPixels = NULL;

        //
        // Shared surf create should take stride
        // CreateSysMemUpdateSurface with WDDM can provide a wrapper around a
        // system memory buffer owned by this module.  But surface width in
        // bytes must equal the stride.  Since GetRenderTargetData also has
        // surface size restrictions and nothing can be done about the out
        // buffer properties, check that out buffer meets requirements before
        // creating the wrapper.  If the check fails (or WDDM isn't available)
        // have D3D allocate system memory and do an extra system memory to
        // system memory transfer.
        //
        // Independent of the above, if there are clip rects, then handle
        // clipping with CPU copies rather than attempting several
        // GetRenderTargetData calls which probably still require a CPU
        // transfer.
        //
        if (   Device().IsLDDMDevice()
            && nStrideOut == nStrideCopy
            && cClipRects == 0)
        {
            fNeedToManuallyCopyBits = false;
            pvSysMemPixels = pbBufferOut;
        }        

        IFC(Device().CreateSysMemUpdateSurface(
            uSourceWidth, 
            uSourceHeight, 
            d3dfmtOut, 
            pvSysMemPixels, 
            &pD3DLockableSurface));

        //
        // The target of GetRenderTargetData has to be the same size and format
        // as the source.  If we need less that the full rendertarget or one of
        // a different format, we need to create a temporary rendertarget which
        // is the right size and format, copy into that, and then readback from
        // there.  If we need the whole thing and format matches, just use the
        // normal rendertarget.
        //
        if (   uSourceWidth != m_d3dsd.Width
            || uSourceHeight != m_d3dsd.Height
            || d3dfmtOut != m_d3dsd.Format)
        {
            IFC(Device().CheckRenderTargetFormat(m_d3dsd.Format, /* pphrTestGetDC = */ NULL));

            IFC(Device().CreateRenderTargetUntracked(
                uSourceWidth,
                uSourceHeight,
                m_d3dsd.Format,
                m_d3dsd.MultiSampleType,
                m_d3dsd.MultiSampleQuality,
                FALSE,
                &pTempSurface
                ));

            IFC(Device().StretchRect(
                this,
                reinterpret_cast<CMilRectL const *>(&rcSource),
                pTempSurface, 
                &rcDest,
                D3DTEXF_NONE
                ));
        }
        
        IFC(Device().GetRenderTargetData(
            pTempSurface ? pTempSurface : this->ID3DSurface(), 
            pD3DLockableSurface
            ));
    }

    //
    // Manually lock the surface and copy the bits to the destination if we
    // weren't able to get it done automatically by GetRenderTargetData.
    //
    if (fNeedToManuallyCopyBits)
    {
        D3DLOCKED_RECT d3dlr;
        
        IFC(pD3DLockableSurface->LockRect(&d3dlr, &rcDest, D3DLOCK_READONLY));

        UINT const BytesPerPixel = BitsPerPixel / BITS_PER_BYTE;

    #if DBG_ANALYSIS
        BYTE *pbAnalysisBufferOutOrig = pbBufferOut;
    #endif

        if (   cClipRects == 0
            || rgClipRects == NULL)
        {
            cClipRects = 1;
            rgClipRects = &rcSource;
        }

        do
        {
            CMilRectU rcCopy = *rgClipRects;
            if (rcCopy.Intersect(rcSource))
            {
                // Adjust pbBufferOut and locked D3D buffer pointer by amount
                // inset by intersection with source rect.  None of this math
                // should overflow since rcCopy is restricted to rcSource which
                // has already been validated as a good bounding rectangle.
                UINT cbBufferInset, cbLeftInset;

                IFC(MultiplyUINT(BytesPerPixel, rcCopy.left - rcSource.left, OUT cbLeftInset));

                IFC(MultiplyUINT(nStrideOut, rcCopy.top - rcSource.top, OUT cbBufferInset));
                IFC(AddUINT(cbBufferInset, cbLeftInset, OUT cbBufferInset));
                BYTE *pbOutBuffer = pbBufferOut + cbBufferInset;

                IFC(MultiplyUINT(d3dlr.Pitch, rcCopy.top - rcSource.top, OUT cbBufferInset));
                IFC(AddUINT(cbBufferInset, cbLeftInset, OUT cbBufferInset));
                BYTE *pbInBuffer = static_cast<BYTE *>(d3dlr.pBits) + cbBufferInset;

                IFC(MultiplyUINT(BytesPerPixel, rcCopy.Width(), OUT nStrideCopy));

                for (UINT i = rcCopy.top; i < rcCopy.bottom; i++)
                {
                    Assert(pbOutBuffer + nStrideCopy <= pbAnalysisBufferOutOrig + cbBufferOut);

                    GpMemcpy(pbOutBuffer, pbInBuffer, nStrideCopy);
                    pbOutBuffer += nStrideOut;
                    pbInBuffer += d3dlr.Pitch;
                }
            }
        } while (rgClipRects++, --cClipRects > 0);

        IGNORE_HR(pD3DLockableSurface->UnlockRect());
    }
    
Cleanup:
    ReleaseInterface(pD3DLockableSurface);
    ReleaseInterface(pTempSurface);
    
    RRETURN(hr);

#endif
}





#if QUICKER_WPF_D3D11_FACADE
HRESULT CD3DSurface::CreateFacade(CD3DResourceManager *manager,QkD3D11State *state,
    UINT width,UINT height,D3DFORMAT format,CD3DSurface **out,ID3D11Texture2D *backBuffer)
{
    *out=nullptr;
    if(format!=D3DFMT_A8R8G8B8 && format!=D3DFMT_X8R8G8B8) return QkFacadeUnimpl("CreateFacadeSurface.format");
    const UINT maxSize = state->featureLevel >= D3D_FEATURE_LEVEL_11_0 ? 16384U : 8192U;
    if(!width || !height || width>maxSize || height>maxSize || static_cast<ULONGLONG>(width)*height*4>UINT_MAX) return E_INVALIDARG;
    ComPtr<QkSurfaceHandle> handle; handle.Attach(new QkSurfaceHandle);
    if(!handle) return E_OUTOFMEMORY;
    CD3DSurface *s=new CD3DSurface(handle.Get()); if(!s) return E_OUTOFMEMORY;
    s->AddRef();
    D3D11_TEXTURE2D_DESC td={}; td.Width=width; td.Height=height; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags=D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE; td.MiscFlags=D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    HRESULT hr=S_OK;
    if(backBuffer) s->m_facadeTexture=backBuffer;
    else hr=state->device->CreateTexture2D(&td,nullptr,&s->m_facadeTexture);
    if(SUCCEEDED(hr)) QkAccount(s->m_facadeTexture.Get(),backBuffer?"DXGI-backbuffer":"GDI-compatible",static_cast<ULONGLONG>(width)*height*4);
    if(SUCCEEDED(hr)) hr=state->device->CreateRenderTargetView(s->m_facadeTexture.Get(),nullptr,&s->m_facadeRTV);
    if(SUCCEEDED(hr)) QkAccount(s->m_facadeRTV.Get(),"RTV");
    if(SUCCEEDED(hr) && format==D3DFMT_X8R8G8B8) {
        // D3D9 XRGB 读取 Alpha 为 1，且任何绘图均不能改变它。
        const float opaqueBlack[4]={0,0,0,1}; state->context->ClearRenderTargetView(s->m_facadeRTV.Get(),opaqueBlack);
    }
    if(SUCCEEDED(hr)) hr=s->m_facadeTexture.As(&s->m_facadeGDI);
    if(FAILED(hr)){s->Release();return hr;}
    ZeroMemory(&s->m_d3dsd,sizeof(s->m_d3dsd)); s->m_d3dsd.Width=width; s->m_d3dsd.Height=height;
    s->m_d3dsd.Format=format; s->m_d3dsd.Type=D3DRTYPE_SURFACE; s->m_d3dsd.Usage=D3DUSAGE_RENDERTARGET;
    s->m_d3dsd.Pool=D3DPOOL_DEFAULT; s->m_d3dsd.MultiSampleType=D3DMULTISAMPLE_NONE;
    s->m_facadeGeneration=state->generation;
    handle->texture=s->m_facadeTexture; handle->rtv=s->m_facadeRTV; handle->context=state->context;
    handle->desc=s->m_d3dsd; handle->generation=state->generation;
    hr=state->device->CreateShaderResourceView(handle->texture.Get(),nullptr,&handle->srv);
    if(SUCCEEDED(hr)) QkAccount(handle->srv.Get(),"SRV");
    if(FAILED(hr)) { s->Release(); return hr; }
    QkFacadeTrace("QK-FACADE create render-target surface\n");
    s->CD3DResource::Init(manager,width*height*4); *out=s; return S_OK;
}
HRESULT CD3DSurface::ReleaseFacadeDC()
{
    HRESULT hr=m_facadeGDI->ReleaseDC(nullptr);
    auto state=Device().Facade();
    state->context->OMSetRenderTargets(1,state->target.GetAddressOf(),nullptr);
    // 执行层每次 Draw 重绑完整 GPU 状态，GetDC 后没有有效 GPU 状态缓存。
    return hr;
}
#endif
