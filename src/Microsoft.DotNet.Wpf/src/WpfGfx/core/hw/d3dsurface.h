// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.


//-----------------------------------------------------------------------------
//

//
//  Description:
//      Contains D3D surface wrapper class declaration
//

MtExtern(CD3DSurface);
MtExtern(D3DResource_Surface);

class CD3DSurface : public CD3DResource
{
private:

    DECLARE_METERHEAP_ALLOC(ProcessHeap, Mt(CD3DSurface));

public:
#if QUICKER_WPF_D3D11_FACADE
    ComPtr<ID3D11Texture2D> m_facadeTexture;
    ComPtr<ID3D11RenderTargetView> m_facadeRTV;
    ComPtr<IDXGISurface1> m_facadeGDI;
    ULONGLONG m_facadeGeneration = 0;
    static HRESULT CreateFacade(CD3DResourceManager *manager, QkD3D11State *state,
        UINT width, UINT height, D3DFORMAT format, CD3DSurface **out, ID3D11Texture2D *backBuffer=nullptr);
    HRESULT ReleaseFacadeDC();
#endif
    static HRESULT Create(
        __inout_ecount(1) CD3DResourceManager *pResourceManager,
        __inout_ecount(1) QkSurfaceHandle *pD3DSurface,
        __deref_out_ecount(1) CD3DSurface **ppSurface
        );

    D3DSURFACE_DESC const &Desc() const { return m_d3dsd; }

    //
    // IDirect3DSurface methods
    //

    HRESULT LockRect(
        __out_ecount(1) D3DLOCKED_RECT *pLockedRect,
        __in_ecount(1) CONST RECT *pRect,
        DWORD Flags
        )
    {
#if QUICKER_WPF_D3D11_FACADE
        return m_pD3DSurface?m_pD3DSurface->LockRect(pLockedRect,pRect,Flags):QkFacadeUnimpl("Surface.LockStandalone");
#else
        return ID3DSurface()->LockRect(pLockedRect, pRect, Flags);
#endif
    }

    HRESULT UnlockRect(
        )
    {
#if QUICKER_WPF_D3D11_FACADE
        return m_pD3DSurface?m_pD3DSurface->UnlockRect():E_UNEXPECTED;
#else
        return ID3DSurface()->UnlockRect();
#endif
    }


    __out_ecount_opt(1) QkSurfaceHandle * GetD3DSurfaceNoAddRef() const
    {
        return m_pD3DSurface;
    }

    __out_ecount(1) QkSurfaceHandle * ID3DSurface() const
    {
        Assert(m_pD3DSurface);
        return m_pD3DSurface;
    }

    void GetSurfaceSize(
        __out_ecount(1) UINT *puWidth,
        __out_ecount(1) UINT *puHeight
        ) const;

    HRESULT GetDC(
        __out_ecount(1) HDC *phdc
        );

    HRESULT ReleaseDC(
        HDC hdc
        )
    {
#if QUICKER_WPF_D3D11_FACADE
        return ReleaseFacadeDC();
#else
        return ID3DSurface()->ReleaseDC(hdc);
#endif
    }

    HRESULT ReadIntoSysMemBuffer(
        __in_ecount(1) const CMilRectU &rcSource,
        UINT cClipRects,
        __in_ecount_opt(cClipRects) const CMilRectU *rgClipRects,
        MilPixelFormat::Enum fmtBitmapOut,
        UINT nStrideOut,
        DBG_ANALYSIS_PARAM_COMMA(UINT cbBufferOut)
        __out_bcount_full(cbBufferOut) BYTE *pbBufferOut
        );

protected:
    CD3DSurface(__inout_ecount(1) QkSurfaceHandle * pD3DSurface);
    ~CD3DSurface();

    HRESULT Init(
        __inout_ecount(1) CD3DResourceManager *pResourceManager
        );

#if PERFMETER
    virtual PERFMETERTAG GetPerfMeterTag() const
    {
        return Mt(D3DResource_Surface);
    }
#endif

private:

    // 
    // CD3DResource methods
    //

    virtual override void ReleaseD3DResources();

protected:

    // Pointer to the actual D3D resource.
    //  The pointer is constant to help enforce the modification restrictions
    //  of CD3DResource objects.
    QkSurfaceHandle * const m_pD3DSurface;

    D3DSURFACE_DESC m_d3dsd;
};


