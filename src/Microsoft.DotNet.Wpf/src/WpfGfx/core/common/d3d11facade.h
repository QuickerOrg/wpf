#pragma once

#if QUICKER_WPF_D3D11_FACADE
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <map>
#include <vector>
#include <string>
#include <d3dcompiler.h>

// WPF 内部执行对象；不实现任何 D3D9 COM 接口。
HRESULT QkFacadeUnimpl(const char *name);
void QkFacadeTrace(const char *line);
// 默认关闭的呈现边界计时；只测原生调用，不改 WPF 调度算法。
LONGLONG QkFacadeClock();
void QkFacadeTiming(const char *name, LONGLONG start, HRESULT result, UINT detail = 0);
// S5h 聚合计数默认关闭；启用时只做原子累加，由实验夹具在计时边界外读取，不逐帧写文件。
#define QK_FACADE_COUNTER_LIST(X) \
    X(PresentAttempt,"present.attempt") X(PresentFlip,"present.flip") X(PresentUlw,"present.ulw") X(PresentGpuGdi,"present.gpu-gdi") \
    X(GetDC,"get-dc") X(Readback,"readback") X(ReadbackBytes,"readback.bytes") \
    X(SwapChainCreate,"swapchain.create") X(SwapChainRelease,"swapchain.release") X(SwapChainResizeRequest,"swapchain.resize-request") X(SwapChainResize,"swapchain.resize") X(SwapChainReuse,"swapchain.same-size-reuse") \
    X(ResourceCreate,"resource.create") X(ResourceRelease,"resource.release") \
    X(TextureCreate,"texture.create") X(TextureRelease,"texture.release") X(ShaderCreate,"shader.create") X(ShaderRelease,"shader.release") \
    X(StateCreate,"state.create") X(StateRelease,"state.release") X(BufferCreate,"buffer.create") X(BufferRelease,"buffer.release") \
    X(QueryCreate,"query.create") X(QueryRelease,"query.release") X(ViewCreate,"view.create") X(ViewRelease,"view.release") \
    X(WaitForVBlank,"wait-for-vblank") X(OutputLookup,"wait-for-vblank.output-lookup") \
    X(ShaderConvert,"shader.convert") X(ShaderCompile,"shader.compile") X(ShaderCacheHit,"shader.cache-hit") X(ShaderCacheMiss,"shader.cache-miss") \
    X(FixedSourceBuild,"shader.fixed-source-build") X(FixedVertexCacheHit,"shader.fixed-vs-cache-hit")
enum class QkFacadeCounter {
#define QK_COUNTER_ENUM(name, label) name,
    QK_FACADE_COUNTER_LIST(QK_COUNTER_ENUM)
#undef QK_COUNTER_ENUM
    Count
};
bool QkFacadeCountersEnabled();
void QkFacadeCount(QkFacadeCounter counter, ULONGLONG amount = 1);
// 默认关闭；私有数据令牌跟随实际 D3D 对象销毁，统计逻辑字节而非驱动物理分配。
void QkAccount(ID3D11DeviceChild *object, const char *kind, ULONGLONG bytes = 0);
void QkAccountEvent(const char *event);
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

// 纹理与 mip 视图共同持有资源；CPU 锁定镜像仅用于位图/渐变上传。
class QkTextureHandle;
class QkSurfaceHandle
{
    LONG m_refs = 1;
public:
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging;
    D3DSURFACE_DESC desc = {};
    UINT mip = 0;
    ULONGLONG generation = 0;
    std::vector<BYTE> pixels;
    std::vector<BYTE> uploadPixels;
    BYTE *reference = nullptr;
    bool locked = false;
    DWORD lockFlags = 0;
    RECT lockRect = {};
    virtual ~QkSurfaceHandle() = default;
    ULONG AddRef() { return InterlockedIncrement(&m_refs); }
    ULONG Release() { LONG n=InterlockedDecrement(&m_refs); if(!n) delete this; return n; }
    HRESULT GetDesc(D3DSURFACE_DESC *out) { *out=desc; return S_OK; }
    HRESULT LockRect(D3DLOCKED_RECT *out, const RECT *rect, DWORD flags);
    HRESULT UnlockRect();
    HRESULT UploadRect(const RECT &rect,const BYTE *data,UINT pitch);
    HRESULT GetDC(HDC *) { return QkFacadeUnimpl("Surface.GetDC"); }
    HRESULT ReleaseDC(HDC) { return QkFacadeUnimpl("Surface.ReleaseDC"); }
};
class QkTextureHandle
{
    LONG m_refs = 1;
public:
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11DeviceContext> context;
    std::vector<QkSurfaceHandle *> levels;
    ULONGLONG generation = 0;
    ~QkTextureHandle() { for(auto p:levels) p->Release(); }
    ULONG AddRef() { return InterlockedIncrement(&m_refs); }
    ULONG Release() { LONG n=InterlockedDecrement(&m_refs); if(!n) delete this; return n; }
    UINT GetLevelCount() const { return static_cast<UINT>(levels.size()); }
    HRESULT GetLevelDesc(UINT n,D3DSURFACE_DESC *out) { if(n>=levels.size()) return E_INVALIDARG; *out=levels[n]->desc; return S_OK; }
    HRESULT GetSurfaceLevel(UINT n,QkSurfaceHandle **out) { *out=nullptr; if(n>=levels.size()) return E_INVALIDARG; *out=levels[n]; (*out)->AddRef(); return S_OK; }
    HRESULT LockRect(UINT n,D3DLOCKED_RECT *out,const RECT *rect,DWORD flags) { return n<levels.size()?levels[n]->LockRect(out,rect,flags):E_INVALIDARG; }
    HRESULT UnlockRect(UINT n) { return n<levels.size()?levels[n]->UnlockRect():E_INVALIDARG; }
    HRESULT AddDirtyRect(const RECT *);
    void GenerateMipSubLevels();
};
using QkBaseTextureHandle = QkTextureHandle;
class QkFacadeShader
{
    LONG m_refs = 1;
public:
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3DBlob> code;
    UINT sampledMask = 0;
    UINT builtinResource = 0;
    std::map<DWORD,ComPtr<ID3D11InputLayout>> layouts;
    ULONGLONG generation = 0;
    ULONG AddRef() { return InterlockedIncrement(&m_refs); }
    ULONG Release() { LONG n=InterlockedDecrement(&m_refs); if(!n) delete this; return n; }
};
using QkVertexShaderHandle = QkFacadeShader;
using QkPixelShaderHandle = QkFacadeShader;

class QkD3D11State;
using QkStateHandle = QkD3D11State;

class QkD3D11State
{
    LONG m_refs = 1;
    HMONITOR m_monitor = nullptr;
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
    ComPtr<ID3D11Query> m_sparePresentQuery;
    D3DVIEWPORT9 m_viewport = {};
    RECT m_scissor = {};
    std::map<unsigned long long, ComPtr<ID3D11BlendState>> m_blends;
    ComPtr<ID3D11RasterizerState> m_raster[2];
    ComPtr<ID3D11Buffer> m_vsConstants, m_psConstants;
    float m_vsData[256][4] = {}, m_psData[224][4] = {};
    bool m_loggedFirstPresent = false;
    bool m_vsDirty = true, m_psDirty = true, m_sizeValid = false;
    float m_sizeData[4] = {};
    ComPtr<QkFacadeShader> m_boundVS, m_boundPS;
    ComPtr<QkTextureHandle> m_textures[8];
    std::map<std::string,ComPtr<QkFacadeShader>> m_shaderCache;
    // 显式 PS 配固定功能 VS 时，VS 源只依赖纹理坐标数量，最多九个条目；布局仍按原 FVF 单独缓存。
    std::map<UINT,ComPtr<QkFacadeShader>> m_fixedVertexShaders;
    std::map<std::string,ComPtr<ID3D11SamplerState>> m_samplerCache;
    HRESULT Apply();
    HRESULT ApplyShaders();
    HRESULT InputLayout(QkFacadeShader *shader, ID3D11InputLayout **out);

    HRESULT Upload(ComPtr<ID3D11Buffer> &buffer, UINT &capacity, UINT bind, const void *data, UINT bytes);
public:
    ComPtr<ID3D11Device> device;
    HRESULT WaitForVBlank();
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> target;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;
    ULONGLONG generation = 0;
    DXGI_ADAPTER_DESC adapter = {};
    UINT width = 0, height = 0;
    static HRESULT Create(HMONITOR monitor, QkD3D11State **out);
    HRESULT Initialize();
    ~QkD3D11State();
    // 全部共享设备入口串行；各显示器保留独立上下文状态和原 WPF 包装语义。
    static void EnterShared();
    static void LeaveShared();
    void Activate();
    static void EnterState(QkD3D11State *state);
    static void LeaveState();
    static void TrimIfIdle();
    static void RetireMediaTexture(QkTextureHandle *texture);
    ComPtr<ID3DDeviceContextState> savedContext;
    ComPtr<ID3D11DeviceContext1> context1;
    HRESULT CreateTexture(const D3DSURFACE_DESC *desc, UINT levels, QkTextureHandle **out);
    HRESULT Compile(const char *source, UINT bytes, bool vertex, QkFacadeShader **out);
    HRESULT Copy(QkSurfaceHandle *src,const RECT *srcRect,QkSurfaceHandle *dst,const RECT *dstRect,D3DTEXTUREFILTERTYPE filter, bool presentClipping = false);

    // 失效标记与共享设备索引都由共享锁保护；实验注入不调用系统/驱动重置。
    HRESULT removedReason = S_OK;
    HRESULT DeviceStatus() const;
    void ExperimentRemoval();
    HRESULT PresentBlit(ID3D11Texture2D *source, ID3D11Texture2D *destination, const RECT &src, const RECT &dst, bool validate = false);
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
    HRESULT BindTarget(ID3D11RenderTargetView *rtv, UINT w, UINT h, bool opaque = false);
    bool targetOpaque = false;
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
    HRESULT SetTexture(DWORD stage, QkBaseTextureHandle *texture);
    HRESULT SetFVF(DWORD fvf) { m_fvf = fvf; return S_OK; }
    HRESULT SetVertexShader(QkVertexShaderHandle *shader);
    HRESULT SetPixelShader(QkPixelShaderHandle *shader);
    HRESULT SetDepthStencilSurface(QkSurfaceHandle *surface);
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
using QkTextureHandle = IDirect3DTexture9;
using QkBaseTextureHandle = IDirect3DBaseTexture9;
using QkSurfaceHandle = IDirect3DSurface9;
using QkVertexShaderHandle = IDirect3DVertexShader9;
using QkPixelShaderHandle = IDirect3DPixelShader9;
using QkStateHandle = IDirect3DDevice9;
using QkVertexBufferHandle = IDirect3DVertexBuffer9;
using QkIndexBufferHandle = IDirect3DIndexBuffer9;
#endif

#if QUICKER_WPF_D3D11_FACADE
// 管理器锁之前先持共享锁，避免显示变更/设备创建与渲染失效通知锁序相反。
struct QkFacadeSharedGuard {
    QkFacadeSharedGuard() { QkD3D11State::EnterShared(); }
    ~QkFacadeSharedGuard() { QkD3D11State::LeaveShared(); }
};
#endif
