#pragma once

#if QUICKER_WPF_D3D11_FACADE
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <map>
#include <vector>

// WPF 内部执行对象；不实现任何 D3D9 COM 接口。
HRESULT QkFacadeUnimpl(const char *name);
void QkFacadeTrace(const char *line);
using Microsoft::WRL::ComPtr;

class QkFacadeBuffer
{
    LONG m_refs = 1;
    bool m_mapped = false;
public:
    virtual ~QkFacadeBuffer() = default;
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11DeviceContext> context;
    UINT size = 0;
    ULONGLONG generation = 0;
    ULONG AddRef() { return InterlockedIncrement(&m_refs); }
    ULONG Release() { LONG n = InterlockedDecrement(&m_refs); if (!n) delete this; return n; }
    HRESULT Lock(UINT offset, UINT count, void **data, DWORD flags);
    HRESULT Unlock();
};
template<int Tag> class QkTypedBuffer : public QkFacadeBuffer {};
using QkVertexBufferHandle = QkTypedBuffer<0>;
using QkIndexBufferHandle = QkTypedBuffer<1>;

class QkD3D11State;
using QkStateHandle = QkD3D11State;

class QkD3D11State
{
    LONG m_refs = 1;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11InputLayout> m_layoutXYZ, m_layoutXYZRHW;
    ComPtr<ID3D11Buffer> m_constants, m_uploadVB, m_uploadIB;
    UINT m_vbCapacity = 0, m_ibCapacity = 0;
    ComPtr<ID3D11Buffer> m_boundVB, m_boundIB;
    UINT m_stride = 0, m_offset = 0;
    DWORD m_fvf = 0;
    DWORD m_states[210] = {};
    DWORD m_stages[8][33] = {};
    DWORD m_samplers[16][14] = {};
    D3DMATERIAL9 m_material = {};
    std::map<D3DTRANSFORMSTATETYPE, CBaseMatrix> m_transforms;
    std::vector<ComPtr<ID3D11Query>> m_presentQueries;
    D3DVIEWPORT9 m_viewport = {};
    RECT m_scissor = {};
    std::map<unsigned long long, ComPtr<ID3D11BlendState>> m_blends;
    ComPtr<ID3D11RasterizerState> m_raster[2];
    HRESULT Apply();
    HRESULT Upload(ComPtr<ID3D11Buffer> &buffer, UINT &capacity, UINT bind, const void *data, UINT bytes);
public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> target;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
    ULONGLONG generation = 0;
    DXGI_ADAPTER_DESC adapter = {};
    UINT width = 0, height = 0;
    static HRESULT Create(HMONITOR monitor, QkD3D11State **out);
    HRESULT Initialize();
    HRESULT DeviceStatus() const
    {
        return SUCCEEDED(device->GetDeviceRemovedReason()) ? S_OK : WGXERR_DISPLAYSTATEINVALID;
    }
    ULONG AddRef() { return InterlockedIncrement(&m_refs); }
    ULONG Release() { LONG n = InterlockedDecrement(&m_refs); if (!n) delete this; return n; }
    void ReadCaps(D3DCAPS9 *caps, UINT *memory) const;
    HRESULT CreateBuffer(UINT bytes, UINT bind, QkFacadeBuffer **out);
    template<class T> HRESULT CreateBuffer(UINT bytes, UINT bind, T **out)
    {
        *out = nullptr;
        QkFacadeBuffer *base = nullptr;
        HRESULT hr = CreateBuffer(bytes, bind, &base);
        if (FAILED(hr)) return hr;
        T *typed = new T;
        if (!typed) { base->Release(); return E_OUTOFMEMORY; }
        typed->buffer = base->buffer; typed->context = base->context; typed->size = base->size; typed->generation = base->generation;
        base->Release(); *out = typed; return S_OK;
    }
    HRESULT BindTarget(ID3D11RenderTargetView *rtv, UINT w, UINT h);
    HRESULT RecordPresent();
    HRESULT GetNumQueuedPresents(UINT *count);
    HRESULT Draw(D3DPRIMITIVETYPE type, UINT vertices, UINT start);
    HRESULT DrawIndexed(UINT vertices, UINT startIndex, INT baseVertex);
    HRESULT DrawUP(D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride);
    HRESULT DrawIndexedUP(UINT vertices, UINT primitives, const WORD *indices, const void *data, UINT stride);
    HRESULT Clear(DWORD flags, DWORD color, UINT count, const D3DRECT *rects);
    HRESULT SetRenderState(D3DRENDERSTATETYPE type, DWORD value);
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value);
    HRESULT SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value);
    HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture);
    HRESULT SetFVF(DWORD fvf) { m_fvf = fvf; return S_OK; }
    HRESULT SetVertexShader(IDirect3DVertexShader9 *shader);
    HRESULT SetPixelShader(IDirect3DPixelShader9 *shader);
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *surface);
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE type, const void *matrix);
    HRESULT SetVertexShaderConstantF(UINT start, const float *data, UINT count);
    HRESULT SetPixelShaderConstantF(UINT start, const float *data, UINT count);
    HRESULT SetPixelShaderConstantI(UINT start, const int *data, UINT count);
    HRESULT SetPixelShaderConstantB(UINT start, const BOOL *data, UINT count);
    HRESULT SetScissorRect(const RECT *rect) { m_scissor = *rect; return S_OK; }
    HRESULT SetViewport(const D3DVIEWPORT9 *vp) { m_viewport = *vp; return S_OK; }
    HRESULT SetStreamSource(UINT stream, QkVertexBufferHandle *buffer, UINT offset, UINT stride);
    HRESULT SetIndices(QkIndexBufferHandle *buffer);
    HRESULT SetMaterial(const D3DMATERIAL9 *material);
};

HRESULT QkReadFacadeCaps(HMONITOR monitor, D3DCAPS9 *caps, UINT *memory, LUID *luid);
#else
using QkStateHandle = IDirect3DDevice9;
using QkVertexBufferHandle = IDirect3DVertexBuffer9;
using QkIndexBufferHandle = IDirect3DIndexBuffer9;
#endif
