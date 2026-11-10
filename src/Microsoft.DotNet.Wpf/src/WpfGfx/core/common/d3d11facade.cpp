#include "precomp.hpp"

#if QUICKER_WPF_D3D11_FACADE
#include <d3dcompiler.h>
#include <set>
#include <string>
#include <regex>
#include <sstream>

void QkFacadeTrace(const char *line)
{
    OutputDebugStringA(line);
    WCHAR path[32768] = {};
    DWORD n = GetEnvironmentVariableW(L"QUICKER_FACADE_LOG", path, ARRAYSIZE(path));
    if (!n || n >= ARRAYSIZE(path)) return;
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
    CloseHandle(file);
}

HRESULT QkFacadeUnimpl(const char *name)
{
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::set<std::string> reported;
    AcquireSRWLockExclusive(&lock);
    if (reported.insert(name).second)
    {
        std::string line = std::string("QK-FACADE-UNIMPL ") + name + "\n";
        QkFacadeTrace(line.c_str());
    }
    ReleaseSRWLockExclusive(&lock);
    return E_NOTIMPL;
}

HRESULT QkD3D11State::Create(HMONITOR monitor, QkD3D11State **out)
{
    *out = nullptr;
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIAdapter1> selected;
    for (UINT a = 0; ; ++a)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(a, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return hr;
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        for (UINT o = 0; ; ++o)
        {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od;
            if (output && SUCCEEDED(output->GetDesc(&od)) && od.Monitor == monitor)
            { selected = adapter; break; }
        }
        if (selected) break;
    }
    if (!selected) return WGXERR_NO_HARDWARE_DEVICE;
    QkD3D11State *state = new QkD3D11State;
    if (!state) return E_OUTOFMEMORY;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    hr = D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &state->device, &state->featureLevel, &state->context);
    if (SUCCEEDED(hr)) hr = selected->GetDesc(&state->adapter);
    UINT support = 0;
    if (SUCCEEDED(hr)) hr = state->device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
    if (SUCCEEDED(hr) && (support & required) != required) hr = WGXERR_INSUFFICIENT_GPU_CAPS;
    if (FAILED(hr)) { state->Release(); return hr; }
    static volatile LONG64 nextGeneration = 0;
    state->generation = InterlockedIncrement64(&nextGeneration);
    state->m_monitor=monitor;
    *out = state;
    return S_OK;
}

void QkD3D11State::ReadCaps(D3DCAPS9 *caps, UINT *memory) const
{
    ZeroMemory(caps, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->Caps2 = D3DCAPS2_CANSHARERESOURCE | D3DCAPS2_CANAUTOGENMIPMAP;
    caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION;
    // 这是原算法的能力镜像；真正创建的 shader 固定为 SM4，绝不提交旧 token。
    caps->PixelShaderVersion = D3DPS_VERSION(2, 0);
    caps->VertexShaderVersion = D3DVS_VERSION(2, 0);
    caps->MaxTextureWidth = caps->MaxTextureHeight = featureLevel >= D3D_FEATURE_LEVEL_11_0 ? 16384 : 8192;
    caps->MaxStreams = 1;
    caps->MaxTextureBlendStages = caps->MaxSimultaneousTextures = 8;
    caps->MaxPrimitiveCount = 0xFFFFF;
    caps->MaxVertexIndex = 0xFFFFFF;
    caps->MaxAnisotropy = 1;
    caps->RasterCaps = D3DPRASTERCAPS_SCISSORTEST;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_COLORWRITEENABLE;
    caps->SrcBlendCaps = caps->DestBlendCaps = D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_ZERO |
        D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_BLENDFACTOR;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT |
        D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_BORDER;
    caps->TextureOpCaps = D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_MODULATE;
    *memory = static_cast<UINT>(min(static_cast<SIZE_T>(UINT_MAX), adapter.DedicatedVideoMemory + adapter.SharedSystemMemory));
}

HRESULT QkReadFacadeCaps(HMONITOR monitor, D3DCAPS9 *caps, UINT *memory, LUID *luid)
{
    struct Caps { D3DCAPS9 caps; UINT memory; LUID luid; };
    static SRWLOCK lock=SRWLOCK_INIT;
    static std::map<ULONGLONG,Caps> cached;
    static ComPtr<IDXGIFactory1> factory;
    AcquireSRWLockExclusive(&lock);
    if(!factory || !factory->IsCurrent()) {
        cached.clear(); factory.Reset();
        HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if(FAILED(hr)) { ReleaseSRWLockExclusive(&lock); return hr; }
    }
    ULONGLONG key=0; bool selected=false;
    for(UINT a=0;!selected;++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if(factory->EnumAdapters1(a,&adapter)!=S_OK) break;
        for(UINT o=0;;++o) {
            ComPtr<IDXGIOutput> output;
            if(adapter->EnumOutputs(o,&output)!=S_OK) break;
            DXGI_OUTPUT_DESC desc;
            if(SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==monitor) {
                DXGI_ADAPTER_DESC1 ad; HRESULT hr=adapter->GetDesc1(&ad);
                if(FAILED(hr)) { ReleaseSRWLockExclusive(&lock); return hr; }
                memcpy(&key,&ad.AdapterLuid,sizeof(key)); selected=true; break;
            }
        }
    }
    if(!selected) { ReleaseSRWLockExclusive(&lock); return WGXERR_NO_HARDWARE_DEVICE; }
    auto found=cached.find(key);
    if(found!=cached.end()) { *caps=found->second.caps; *memory=found->second.memory; *luid=found->second.luid; ReleaseSRWLockExclusive(&lock); return S_OK; }
    QkD3D11State *state=nullptr;
    HRESULT hr=QkD3D11State::Create(monitor,&state);
    if(SUCCEEDED(hr)) {
        state->ReadCaps(caps,memory); *luid=state->adapter.AdapterLuid;
        cached[key]={*caps,*memory,*luid}; state->Release();
        QkFacadeTrace("QK-FACADE caps probe cached per adapter LUID\n");
    }
    ReleaseSRWLockExclusive(&lock); return hr;
}

HRESULT QkD3D11State::Initialize()
{
    HRESULT hr=S_OK;
    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cb, nullptr, &m_constants);
    if (FAILED(hr)) return hr;
    cb.ByteWidth=sizeof(m_vsData); hr=device->CreateBuffer(&cb,nullptr,&m_vsConstants); if(FAILED(hr)) return hr;
    cb.ByteWidth=sizeof(m_psData); hr=device->CreateBuffer(&cb,nullptr,&m_psConstants); if(FAILED(hr)) return hr;
    for (UINT i = 0; i < 2; ++i)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = i != 0;
        hr = device->CreateRasterizerState(&rd, &m_raster[i]);
        if (FAILED(hr)) return hr;
    }
    char identity[256];
    StringCchPrintfA(identity, ARRAYSIZE(identity), "QK-FACADE device hardware generation=%llu monitor=%p FL=0x%x vendor=0x%x device=0x%x shader=vs_4_0/ps_4_0\n",
        generation, m_monitor, featureLevel, adapter.VendorId, adapter.DeviceId);
    QkFacadeTrace(identity);
    return S_OK;
}

HRESULT QkFacadeBuffer::Lock(UINT offset, UINT count, void **data, DWORD flags)
{
    *data = nullptr;
    if (m_mapped || offset > size || count > size-offset) return E_INVALIDARG;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context->Map(buffer.Get(), 0, (flags & D3DLOCK_DISCARD) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped);
    if (SUCCEEDED(hr)) { m_mapped = true; *data = static_cast<BYTE *>(mapped.pData) + offset; }
    return hr;
}
HRESULT QkFacadeBuffer::Unlock()
{
    if (!m_mapped) return E_INVALIDARG;
    context->Unmap(buffer.Get(), 0); m_mapped = false; return S_OK;
}
HRESULT QkD3D11State::CreateBuffer(UINT bytes, UINT bind, QkFacadeBuffer **out)
{
    *out = nullptr;
    QkFacadeBuffer *b = new QkFacadeBuffer;
    if (!b) return E_OUTOFMEMORY;
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = bytes; desc.Usage = D3D11_USAGE_DYNAMIC; desc.BindFlags = bind; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&desc, nullptr, &b->buffer);
    if (FAILED(hr)) { b->Release(); return hr; }
    b->context = context; b->size = bytes; b->generation = generation; *out = b; return S_OK;
}
HRESULT QkD3D11State::BindTarget(ID3D11RenderTargetView *rtv, UINT w, UINT h)
{
    ID3D11ShaderResourceView *empty[8]={}; context->PSSetShaderResources(0,8,empty);
    target = rtv; width = w; height = h;
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    return DeviceStatus();
}
HRESULT QkD3D11State::RecordPresent()
{
    D3D11_QUERY_DESC desc = { D3D11_QUERY_EVENT, 0 };
    ComPtr<ID3D11Query> query;
    HRESULT hr = device->CreateQuery(&desc, &query);
    if (FAILED(hr)) return hr;
    context->End(query.Get()); context->Flush();
    m_presentQueries.push_back(query); return S_OK;
}
HRESULT QkD3D11State::GetNumQueuedPresents(UINT *count)
{
    *count = 0;
    while (!m_presentQueries.empty())
    {
        BOOL done = FALSE;
        HRESULT hr = context->GetData(m_presentQueries.front().Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(hr)) return hr;
        if (hr == S_FALSE || !done) break;
        m_presentQueries.erase(m_presentQueries.begin());
    }
    *count = static_cast<UINT>(m_presentQueries.size());
    return DeviceStatus();
}
HRESULT QkD3D11State::SetRenderState(D3DRENDERSTATETYPE type, DWORD value)
{
    if (static_cast<UINT>(type) >= ARRAYSIZE(m_states)) return E_INVALIDARG;
    m_states[type] = value; return S_OK; // 缓存意图，Draw 时统一验证并提交。
}
HRESULT QkD3D11State::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
    if (stage >= 8 || type >= 33) return E_INVALIDARG;
    m_stages[stage][type] = value; return S_OK;
}
HRESULT QkD3D11State::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    if (sampler >= 16 || type >= 14) return E_INVALIDARG;
    m_samplers[sampler][type] = value; return S_OK;
}
HRESULT QkD3D11State::SetTexture(DWORD n, QkBaseTextureHandle *p) {
    if(n>=8) return E_INVALIDARG;
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    m_textures[n]=p; return S_OK;
}
HRESULT QkD3D11State::SetVertexShader(QkVertexShaderHandle *p) {
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundVS=p; return S_OK;
}
HRESULT QkD3D11State::SetPixelShader(QkPixelShaderHandle *p) {
    if(p && p->generation!=generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundPS=p; return S_OK;
}
HRESULT QkD3D11State::SetDepthStencilSurface(QkSurfaceHandle *p) { return p ? QkFacadeUnimpl("SetDepthStencilSurface") : S_OK; }
HRESULT QkD3D11State::SetTransform(D3DTRANSFORMSTATETYPE type, const void *matrix)
{
    if (!matrix) return E_INVALIDARG;
    m_transforms[type] = *static_cast<const CBaseMatrix *>(matrix); return S_OK;
}
HRESULT QkD3D11State::SetVertexShaderConstantF(UINT start, const float *data, UINT count) {
    if(!data || start>256 || count>256-start) return E_INVALIDARG;
    memcpy(m_vsData+start,data,count*16); return S_OK;
}
HRESULT QkD3D11State::SetPixelShaderConstantF(UINT start, const float *data, UINT count) {
    if(!data || start>224 || count>224-start) return E_INVALIDARG;
    memcpy(m_psData+start,data,count*16); return S_OK;
}
HRESULT QkD3D11State::SetPixelShaderConstantI(UINT, const int *, UINT) { return QkFacadeUnimpl("SetPixelShaderConstantI"); }
HRESULT QkD3D11State::SetPixelShaderConstantB(UINT, const BOOL *, UINT) { return QkFacadeUnimpl("SetPixelShaderConstantB"); }
HRESULT QkD3D11State::SetMaterial(const D3DMATERIAL9 *material)
{
    if (!material) return E_INVALIDARG;
    m_material = *material; return S_OK; // Lighting=false 时保留材料，开启光照由 Draw 显式拒绝。
}
HRESULT QkD3D11State::SetStreamSource(UINT stream, QkVertexBufferHandle *b, UINT offset, UINT stride)
{
    if (stream) return QkFacadeUnimpl("SetStreamSource.nonzero");
    if (b && b->generation != generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundVB = b ? b->buffer : nullptr; m_offset = offset; m_stride = stride; return S_OK;
}
HRESULT QkD3D11State::SetIndices(QkIndexBufferHandle *b)
{
    if (b && b->generation != generation) return WGXERR_DISPLAYSTATEINVALID;
    m_boundIB = b ? b->buffer : nullptr; return S_OK;
}

static D3D11_BLEND QkBlend(DWORD value)
{
    switch (value) {
    case D3DBLEND_ZERO: return D3D11_BLEND_ZERO;
    case D3DBLEND_ONE: return D3D11_BLEND_ONE;
    case D3DBLEND_SRCALPHA: return D3D11_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_INVDESTALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
    case D3DBLEND_INVSRCCOLOR: return D3D11_BLEND_INV_SRC_COLOR;
    case D3DBLEND_BLENDFACTOR: return D3D11_BLEND_BLEND_FACTOR;
    case D3DBLEND_INVBLENDFACTOR: return D3D11_BLEND_INV_BLEND_FACTOR;
    default: return static_cast<D3D11_BLEND>(0); }
}
HRESULT QkD3D11State::Apply()
{
    if (!target || !width || !height) return E_UNEXPECTED;
    if (!(m_fvf & D3DFVF_DIFFUSE) || m_states[D3DRS_ZENABLE] || m_states[D3DRS_STENCILENABLE] || m_states[D3DRS_LIGHTING]) return QkFacadeUnimpl("Draw.nonSolidState");
    if (m_states[D3DRS_ALPHATESTENABLE] || m_states[D3DRS_FOGENABLE] || m_states[D3DRS_CLIPPLANEENABLE] ||
        m_states[D3DRS_SRGBWRITEENABLE] || m_states[D3DRS_SEPARATEALPHABLENDENABLE] ||
        m_states[D3DRS_CULLMODE] != D3DCULL_NONE || m_states[D3DRS_FILLMODE] != D3DFILL_SOLID ||
        m_states[D3DRS_BLENDOP] != D3DBLENDOP_ADD) return QkFacadeUnimpl("Draw.unsupportedRenderState");
    if ((m_fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ && (m_fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW)
        return QkFacadeUnimpl("Draw.vertexLayout");
    unsigned long long key = m_states[D3DRS_ALPHABLENDENABLE] | (static_cast<unsigned long long>(m_states[D3DRS_SRCBLEND]) << 8) |
        (static_cast<unsigned long long>(m_states[D3DRS_DESTBLEND]) << 16) | (static_cast<unsigned long long>(m_states[D3DRS_COLORWRITEENABLE]) << 24);
    auto found = m_blends.find(key);
    if (found == m_blends.end())
    {
        D3D11_BLEND_DESC bd = {};
        auto &b = bd.RenderTarget[0]; b.BlendEnable = !!m_states[D3DRS_ALPHABLENDENABLE];
        b.SrcBlend = b.SrcBlendAlpha = b.BlendEnable ? QkBlend(m_states[D3DRS_SRCBLEND]) : D3D11_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = b.BlendEnable ? QkBlend(m_states[D3DRS_DESTBLEND]) : D3D11_BLEND_ZERO;
        if (b.SrcBlendAlpha == D3D11_BLEND_INV_SRC_COLOR) b.SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (b.DestBlendAlpha == D3D11_BLEND_INV_SRC_COLOR) b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        if (!b.SrcBlend || !b.DestBlend) return QkFacadeUnimpl("Blend.factor");
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTargetWriteMask = static_cast<UINT8>(m_states[D3DRS_COLORWRITEENABLE]);
        ComPtr<ID3D11BlendState> state;
        HRESULT hr = device->CreateBlendState(&bd, &state); if (FAILED(hr)) return hr;
        found = m_blends.insert(std::make_pair(key, state)).first;
    }
    D3D11_MAPPED_SUBRESOURCE map;
    HRESULT hr = context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map); if (FAILED(hr)) return hr;
    float size[4] = {static_cast<float>(m_viewport.Width), static_cast<float>(m_viewport.Height), static_cast<float>(m_viewport.X), static_cast<float>(m_viewport.Y)}; memcpy(map.pData, size, sizeof(size)); context->Unmap(m_constants.Get(), 0);
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    DWORD blend=m_states[D3DRS_BLENDFACTOR];
    float factor[]={((blend>>16)&255)/255.f,((blend>>8)&255)/255.f,(blend&255)/255.f,(blend>>24)/255.f};
    context->OMSetBlendState(found->second.Get(), factor, UINT_MAX);
    context->OMSetDepthStencilState(nullptr, 0); // 没有 DSV，原 2D Z 关闭意图在上面验证。
    context->RSSetState(m_raster[!!m_states[D3DRS_SCISSORTESTENABLE]].Get());
    context->RSSetScissorRects(1, &m_scissor);
    D3D11_VIEWPORT vp = {static_cast<float>(m_viewport.X), static_cast<float>(m_viewport.Y), static_cast<float>(m_viewport.Width), static_cast<float>(m_viewport.Height), m_viewport.MinZ, m_viewport.MaxZ};
    context->RSSetViewports(1, &vp);
    hr=ApplyShaders(); if(FAILED(hr)) return hr;
    context->IASetVertexBuffers(0, 1, m_boundVB.GetAddressOf(), &m_stride, &m_offset);
    context->IASetIndexBuffer(m_boundIB.Get(), DXGI_FORMAT_R16_UINT, 0);
    return S_OK;
}
HRESULT QkD3D11State::Draw(D3DPRIMITIVETYPE type, UINT vertices, UINT start)
{
    if (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP && type != D3DPT_LINELIST) return QkFacadeUnimpl("Draw.topology");
    HRESULT hr = Apply(); if (FAILED(hr)) { char line[128]; StringCchPrintfA(line,128,"QK-FACADE Draw failed hr=0x%08x fvf=0x%x\n",hr,m_fvf); QkFacadeTrace(line); return hr; }
    context->IASetPrimitiveTopology(type == D3DPT_TRIANGLELIST ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : type == D3DPT_TRIANGLESTRIP ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->Draw(vertices, start);
    QkFacadeTrace("QK-FACADE Draw original-hw\n");
    return DeviceStatus();
}
HRESULT QkD3D11State::DrawIndexed(UINT indices, UINT startIndex, INT baseVertex)
{
    HRESULT hr = Apply(); if (FAILED(hr)) { char line[128]; StringCchPrintfA(line,128,"QK-FACADE Draw failed hr=0x%08x fvf=0x%x\n",hr,m_fvf); QkFacadeTrace(line); return hr; }
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context->DrawIndexed(indices, startIndex, baseVertex);
    QkFacadeTrace("QK-FACADE DrawIndexed original-hw\n");
    return DeviceStatus();
}
HRESULT QkD3D11State::Upload(ComPtr<ID3D11Buffer> &buffer, UINT &capacity, UINT bind, const void *data, UINT bytes)
{
    if (bytes > capacity)
    {
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = max(bytes, 1024U*1024U); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = bind; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        buffer.Reset(); HRESULT hr = device->CreateBuffer(&bd, nullptr, &buffer); if (FAILED(hr)) return hr; capacity = bd.ByteWidth;
    }
    D3D11_MAPPED_SUBRESOURCE map; HRESULT hr = context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) return hr; memcpy(map.pData, data, bytes); context->Unmap(buffer.Get(), 0); return S_OK;
}
HRESULT QkD3D11State::DrawUP(D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride)
{
    if (!data || !stride || primitives > (UINT_MAX-2)/3) return E_INVALIDARG;
    UINT count = type == D3DPT_TRIANGLELIST ? primitives*3 : type == D3DPT_LINELIST ? primitives*2 : primitives+2;
    if (count > UINT_MAX/stride) return E_INVALIDARG;
    if (type == D3DPT_TRIANGLEFAN && count > 65536) return QkFacadeUnimpl("DrawUP.largeFan");
    HRESULT hr = Upload(m_uploadVB, m_vbCapacity, D3D11_BIND_VERTEX_BUFFER, data, count*stride); if (FAILED(hr)) return hr;
    auto savedVB = m_boundVB; auto savedIB = m_boundIB;
    UINT savedStride = m_stride, savedOffset = m_offset;
    m_boundVB = m_uploadVB; m_stride = stride; m_offset = 0;
    if (type == D3DPT_TRIANGLEFAN)
    {
        std::vector<WORD> indices(primitives*3);
        for (UINT i=0;i<primitives;++i) { indices[i*3]=0; indices[i*3+1]=static_cast<WORD>(i+1); indices[i*3+2]=static_cast<WORD>(i+2); }
        hr = Upload(m_uploadIB, m_ibCapacity, D3D11_BIND_INDEX_BUFFER, indices.data(), primitives*3*sizeof(WORD));
        if (SUCCEEDED(hr)) { m_boundIB = m_uploadIB; hr = DrawIndexed(primitives*3, 0, 0); }
    }
    else { hr = Draw(type, count, 0); }
    // UP 上传是临时绑定；不能污染原状态管理器缓存的 VB/IB 身份。
    m_boundVB = savedVB; m_boundIB = savedIB; m_stride = savedStride; m_offset = savedOffset;
    return hr;
}
HRESULT QkD3D11State::DrawIndexedUP(UINT vertices, UINT primitives, const WORD *indices, const void *data, UINT stride)
{
    if (!indices || !data || !stride || vertices > UINT_MAX/stride || primitives > UINT_MAX/(3*sizeof(WORD))) return E_INVALIDARG;
    HRESULT hr = Upload(m_uploadVB, m_vbCapacity, D3D11_BIND_VERTEX_BUFFER, data, vertices*stride); if (FAILED(hr)) return hr;
    hr = Upload(m_uploadIB, m_ibCapacity, D3D11_BIND_INDEX_BUFFER, indices, primitives*3*sizeof(WORD)); if (FAILED(hr)) return hr;
    auto savedVB = m_boundVB; auto savedIB = m_boundIB;
    UINT savedStride = m_stride, savedOffset = m_offset;
    m_boundVB = m_uploadVB; m_boundIB = m_uploadIB; m_stride = stride; m_offset = 0;
    hr = DrawIndexed(primitives*3, 0, 0);
    m_boundVB = savedVB; m_boundIB = savedIB; m_stride = savedStride; m_offset = savedOffset;
    return hr;
}
HRESULT QkD3D11State::Clear(DWORD flags, DWORD color, UINT count, const D3DRECT *rects)
{
    if (flags != D3DCLEAR_TARGET) return QkFacadeUnimpl("Clear.depthStencil");
    if (!target) return E_UNEXPECTED;
    float rgba[] = {((color>>16)&255)/255.f, ((color>>8)&255)/255.f, (color&255)/255.f, (color>>24)/255.f};
    if (!count) { context->ClearRenderTargetView(target.Get(), rgba); return DeviceStatus(); }
    auto savedVS=m_boundVS; auto savedPS=m_boundPS;
    DWORD savedStages[8][33]; memcpy(savedStages,m_stages,sizeof(m_stages));
    m_boundVS.Reset(); m_boundPS.Reset();
    m_stages[0][D3DTSS_COLOROP]=D3DTOP_SELECTARG1; m_stages[0][D3DTSS_COLORARG1]=D3DTA_DIFFUSE;
    m_stages[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1; m_stages[0][D3DTSS_ALPHAARG1]=D3DTA_DIFFUSE;
    m_stages[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;
    DWORD saved[210]; memcpy(saved, m_states, sizeof(saved)); DWORD fvf = m_fvf; D3DVIEWPORT9 vp = m_viewport;
    auto vb = m_boundVB; auto ib = m_boundIB; UINT stride = m_stride, offset = m_offset;
    m_states[D3DRS_ALPHABLENDENABLE]=FALSE; m_states[D3DRS_SCISSORTESTENABLE]=FALSE; m_states[D3DRS_COLORWRITEENABLE]=15;
    m_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE; m_viewport = {0,0,width,height,0,1};
    struct V { float x,y,z; DWORD c; };
    HRESULT hr = S_OK;
    for (UINT i=0; i<count && SUCCEEDED(hr); ++i)
    {
        float l=static_cast<float>(rects[i].x1),t=static_cast<float>(rects[i].y1),r=static_cast<float>(rects[i].x2),b=static_cast<float>(rects[i].y2);
        V v[] = {{l,t,0,color},{r,t,0,color},{l,b,0,color},{l,b,0,color},{r,t,0,color},{r,b,0,color}};
        hr = DrawUP(D3DPT_TRIANGLELIST, 2, v, sizeof(V));
    }
    m_boundVS=savedVS; m_boundPS=savedPS; memcpy(m_stages,savedStages,sizeof(m_stages));
    memcpy(m_states, saved, sizeof(saved)); m_fvf=fvf; m_viewport=vp; m_boundVB=vb; m_boundIB=ib; m_stride=stride; m_offset=offset;
    return hr;
}
#include "d3d11facade-resources.inl"
#endif
