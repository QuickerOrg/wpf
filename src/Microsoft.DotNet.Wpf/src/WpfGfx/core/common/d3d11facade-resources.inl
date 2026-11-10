// 本文件只由 D3D11 编译期变体包含；B 继续使用原资源与 shader 执行链。
static DXGI_FORMAT QkFormat(D3DFORMAT f)
{
    switch(f) {
    case D3DFMT_X8R8G8B8: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case D3DFMT_A8R8G8B8: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_A8: return DXGI_FORMAT_A8_UNORM;
    case D3DFMT_L8: return DXGI_FORMAT_R8_UNORM;
    case D3DFMT_A32B32G32R32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

static UINT QkPixelSize(D3DFORMAT format)
{
    return format==D3DFMT_A8 ? 1 : D3DFormatSize(format);
}

HRESULT QkD3D11State::CreateTexture(const D3DSURFACE_DESC *desc,UINT levels,QkTextureHandle **out)
{
    *out=nullptr;
    if(!desc || !desc->Width || !desc->Height || desc->MultiSampleType!=D3DMULTISAMPLE_NONE) return E_INVALIDARG;
    DXGI_FORMAT format=QkFormat(desc->Format);
    if(format==DXGI_FORMAT_UNKNOWN) return QkFacadeUnimpl("Texture.format");
    if(!levels) { levels=1; for(UINT n=max(desc->Width,desc->Height);n>1;n>>=1) ++levels; }
    if(levels>15) return E_INVALIDARG;
    ComPtr<QkTextureHandle> t; t.Attach(new QkTextureHandle);
    if(!t) return E_OUTOFMEMORY;
    t->context=context; t->generation=generation;
    D3D11_TEXTURE2D_DESC td={}; td.Width=desc->Width; td.Height=desc->Height;
    td.MipLevels=levels; td.ArraySize=1; td.Format=format; td.SampleDesc.Count=1;
    // 系统内存资源保存锁定镜像；可采样资源只分配一次 DEFAULT，脏区 UpdateSubresource。
    if(desc->Pool!=D3DPOOL_SYSTEMMEM) {
        td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(desc->Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_AUTOGENMIPMAP)) td.BindFlags|=D3D11_BIND_RENDER_TARGET;
        if(desc->Usage&D3DUSAGE_AUTOGENMIPMAP) td.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        HRESULT hr=device->CreateTexture2D(&td,nullptr,&t->texture); if(FAILED(hr)) return hr;
        hr=device->CreateShaderResourceView(t->texture.Get(),nullptr,&t->srv); if(FAILED(hr)) return hr;
    }
    for(UINT n=0;n<levels;++n) {
        ComPtr<QkSurfaceHandle> v; v.Attach(new QkSurfaceHandle);
        if(!v) return E_OUTOFMEMORY;
        v->texture=t->texture; v->context=context; v->generation=generation; v->mip=n;
        v->desc=*desc; v->desc.Width=max(1U,desc->Width>>n); v->desc.Height=max(1U,desc->Height>>n);
        if(t->texture) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sd={}; sd.Format=format; sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MostDetailedMip=n; sd.Texture2D.MipLevels=1;
            HRESULT hr=device->CreateShaderResourceView(t->texture.Get(),&sd,&v->srv); if(FAILED(hr)) return hr;
        }
        if(td.BindFlags&D3D11_BIND_RENDER_TARGET) {
            D3D11_RENDER_TARGET_VIEW_DESC rd={}; rd.Format=format; rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D; rd.Texture2D.MipSlice=n;
            HRESULT hr=device->CreateRenderTargetView(t->texture.Get(),&rd,&v->rtv); if(FAILED(hr)) return hr;
        }
        t->levels.push_back(v.Detach());
    }
    QkFacadeTrace((desc->Usage&D3DUSAGE_RENDERTARGET)?"QK-FACADE create offscreen RT texture\n":"QK-FACADE create upload/sample texture\n");
    *out=t.Detach(); return S_OK;
}

HRESULT QkSurfaceHandle::LockRect(D3DLOCKED_RECT *out,const RECT *rect,DWORD flags)
{
    if(!out || locked) return E_INVALIDARG;
    RECT rc=rect?*rect:RECT{0,0,static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height)};
    if(rc.left<0 || rc.top<0 || rc.right<=rc.left || rc.bottom<=rc.top ||
        static_cast<UINT>(rc.right)>desc.Width || static_cast<UINT>(rc.bottom)>desc.Height) return E_INVALIDARG;
    UINT bpp=QkPixelSize(desc.Format),pitch=desc.Width*bpp;
    if(!bpp || static_cast<ULONGLONG>(pitch)*desc.Height>UINT_MAX) return E_INVALIDARG;
    if(!reference && pixels.empty()) pixels.resize(static_cast<size_t>(pitch)*desc.Height);
    BYTE *data=reference?reference:pixels.data();
    if((flags&D3DLOCK_READONLY) && texture) {
        if(!staging) {
            D3D11_TEXTURE2D_DESC td={}; texture->GetDesc(&td);
            td.Usage=D3D11_USAGE_STAGING; td.BindFlags=td.MiscFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Device> device; texture->GetDevice(&device);
            HRESULT hr=device->CreateTexture2D(&td,nullptr,&staging); if(FAILED(hr)) return hr;
        }
        context->CopyResource(staging.Get(),texture.Get());
        D3D11_MAPPED_SUBRESOURCE map; HRESULT hr=context->Map(staging.Get(),mip,D3D11_MAP_READ,0,&map); if(FAILED(hr)) return hr;
        for(UINT y=0;y<desc.Height;++y) {
            auto row=static_cast<BYTE*>(map.pData)+y*map.RowPitch;
            if(desc.Format==D3DFMT_A8 || desc.Format==D3DFMT_L8) {
                memcpy(data+y*pitch,row,pitch);
            } else memcpy(data+y*pitch,row,pitch);
        }
        context->Unmap(staging.Get(),mip);
    }
    locked=true; lockFlags=flags; lockRect=rc;
    out->Pitch=pitch; out->pBits=data+rc.top*pitch+rc.left*bpp; return S_OK;
}
HRESULT QkSurfaceHandle::UnlockRect()
{
    if(!locked) return E_INVALIDARG;
    locked=false;
    if(texture && !(lockFlags&D3DLOCK_READONLY)) {
        UINT pitch=desc.Width*QkPixelSize(desc.Format);
        BYTE *data=reference?reference:pixels.data();
        return UploadRect(lockRect,data+lockRect.top*pitch+lockRect.left*QkPixelSize(desc.Format),pitch);
    }
    return S_OK;
}
HRESULT QkSurfaceHandle::UploadRect(const RECT &rect,const BYTE *data,UINT pitch)
{
    if(!texture || !data) return E_INVALIDARG;
    D3D11_BOX box={static_cast<UINT>(rect.left),static_cast<UINT>(rect.top),0,
        static_cast<UINT>(rect.right),static_cast<UINT>(rect.bottom),1};
    // A8/R8 保留单字节 atlas；文字 shader 在采样边界恢复 D3D9 通道语义。
    context->UpdateSubresource(texture.Get(),mip,&box,data,pitch,0);
    QkFacadeTrace("QK-FACADE upload texture dirty rectangle\n");
    return S_OK;
}
HRESULT QkTextureHandle::AddDirtyRect(const RECT *rect)
{
    if(!texture) return S_OK; // SYSTEMMEM 脏区在 UpdateTexture 时传输。
    auto s=levels[0];
    if(s->pixels.empty() && !s->reference) return E_UNEXPECTED;
    D3DLOCKED_RECT mapped; HRESULT hr=s->LockRect(&mapped,rect,0);
    return SUCCEEDED(hr)?s->UnlockRect():hr;
}

static void QkReplace(std::string &s,const std::string &from,const std::string &to)
{
    size_t pos=0; while((pos=s.find(from,pos))!=std::string::npos) { s.replace(pos,from.size(),to); pos+=to.size(); }
}

// 在原片段生成器与编译器边界做 SM4 方言转换，缓存键包含转换后的全文和阶段。
static std::string QkModernHLSL(std::string s)
{
    size_t technique=s.find("technique T0"); if(technique!=std::string::npos) s.erase(technique);
    // 原参数表每个 scalar/vector 独占 c 寄存器；补齐 cbuffer 的 16 字节字段。
    std::regex structures("struct ([A-Za-z0-9_]+_ConstData)\\s*\\{([^}]+)\\};");
    std::string padded; size_t end=0;
    for(std::sregex_iterator i(s.begin(),s.end(),structures),last;i!=last;++i) {
        auto m=*i; padded+=s.substr(end,m.position()-end);
        std::string body=m[2]; unsigned pad=0;
        std::regex field("(float([123])?) ([A-Za-z0-9_]+);");
        std::string fields; size_t e=0;
        for(std::sregex_iterator j(body.begin(),body.end(),field);j!=last;++j) {
            auto f=*j; fields+=body.substr(e,f.position()-e)+f.str();
            unsigned n=f[2].str().empty()?1:static_cast<unsigned>(f[2].str()[0]-'0');
            fields+=" float"+std::to_string(4-n)+" QkPad"+std::to_string(pad++)+";";
            e=f.position()+f.length();
        }
        fields+=body.substr(e); padded+="struct "+m[1].str()+" {"+fields+"};"; end=m.position()+m.length();
    }
    padded+=s.substr(end); s=std::move(padded);
    QkReplace(s,"VertexShaderConstantData Data_VS;","cbuffer VSData : register(b1) { VertexShaderConstantData Data_VS; };");
    QkReplace(s,"PixelShaderConstantData Data_PS;","cbuffer PSData : register(b2) { PixelShaderConstantData Data_PS; };");
    std::regex sampler("sampler ([A-Za-z0-9_]+);"),sample("tex2D\\(\\s*([A-Za-z0-9_]+)\\s*,");
    std::vector<std::string> names;
    for(std::sregex_iterator i(s.begin(),s.end(),sampler),last;i!=last;++i) names.push_back((*i)[1]);
    for(UINT n=0;n<names.size();++n) {
        auto name=names[n];
        QkReplace(s,"sampler "+name+";","Texture2D "+name+"Tex : register(t"+std::to_string(n)+"); SamplerState "+name+"State : register(s"+std::to_string(n)+");");
        s=std::regex_replace(s,std::regex("\\b"+name+"\\b"),name+"Tex, "+name+"State");
    }
    s=std::regex_replace(s,std::regex("sampler ([A-Za-z0-9_]+)"),"Texture2D $1, SamplerState $1State");
    s=std::regex_replace(s,sample,"$1.Sample($1State,");
    s=std::regex_replace(s,std::regex("tex1D\\(\\s*([A-Za-z0-9_]+)\\s*,"),"QkSample1D($1,$1State,");
    QkReplace(s,"float4 Position : POSITION;","float4 Position : SV_Position;");
    QkReplace(s,"PixelShaderImpl(\n    float4 Position : POSITION,","PixelShaderImpl(\n    float4 Position : SV_Position,");
    QkReplace(s,") : COLOR\n",") : SV_Target\n");
    // D3D9 COLOR 插值器会在 PS 输入处钳位；SM4 普通 varying 不会自动钳位。
    QkReplace(s,"float4 curColor = Diffuse;","float4 curColor = saturate(Diffuse);");
    // D3D9 的 diffuse 数据按 DWORD BGRA 上传；只有 VS 入口需要交换。
    size_t vs=s.find("VertexShaderImpl("); if(vs!=std::string::npos) {
        size_t brace=s.find('{',vs); s.insert(brace+1,"\nDiffuse=Diffuse.bgra;\n");
        size_t ret=s.find("return Output;",brace);
        if(ret!=std::string::npos) s.insert(ret,"Output.Position.xy += float2(1,-1)*Output.Position.w/QkTarget.xy;\n");
    }
    return "cbuffer QkTargetData : register(b0) { float4 QkTarget; };\n"
        "float4 QkSample1D(Texture2D t,SamplerState s,float u){return t.Sample(s,float2(u,0.5));}\n"+s;
}

HRESULT QkD3D11State::Compile(const char *source,UINT bytes,bool vertex,QkFacadeShader **out)
{
    *out=nullptr;
    std::string text(source,bytes); while(!text.empty() && text.back()==0) text.pop_back();
    bool fragments=text.find("VertexShaderImpl(")!=std::string::npos;
    if(fragments) text=QkModernHLSL(text);
    std::string key=(vertex?"vs_4_0:":"ps_4_0:")+text;
    auto found=m_shaderCache.find(key);
    if(found!=m_shaderCache.end()) { *out=found->second.Get(); (*out)->AddRef(); return S_OK; }
    ComPtr<QkFacadeShader> shader; shader.Attach(new QkFacadeShader);
    if(!shader) return E_OUTOFMEMORY;
    ComPtr<ID3DBlob> errors;
    HRESULT hr=D3DCompile(text.data(),text.size(),"QkOriginalHw",nullptr,nullptr,
        fragments?(vertex?"VertexShaderImpl":"PixelShaderImpl"):(vertex?"VS":"PS"),vertex?"vs_4_0":"ps_4_0",
        D3DCOMPILE_ENABLE_STRICTNESS,0,&shader->code,&errors);
    if(FAILED(hr)) { if(errors) QkFacadeTrace(static_cast<const char*>(errors->GetBufferPointer())); return hr; }
    if(vertex) hr=device->CreateVertexShader(shader->code->GetBufferPointer(),shader->code->GetBufferSize(),nullptr,&shader->vs);
    else hr=device->CreatePixelShader(shader->code->GetBufferPointer(),shader->code->GetBufferSize(),nullptr,&shader->ps);
    if(FAILED(hr)) return hr;
    if(!vertex) {
        ComPtr<ID3D11ShaderReflection> reflection;
        hr=D3DReflect(shader->code->GetBufferPointer(),shader->code->GetBufferSize(),IID_PPV_ARGS(&reflection)); if(FAILED(hr)) return hr;
        D3D11_SHADER_DESC desc; hr=reflection->GetDesc(&desc); if(FAILED(hr)) return hr;
        for(UINT n=0;n<desc.BoundResources;++n) {
            D3D11_SHADER_INPUT_BIND_DESC input; hr=reflection->GetResourceBindingDesc(n,&input); if(FAILED(hr)) return hr;
            if(input.Type==D3D_SIT_TEXTURE) {
                if(input.BindPoint>=8 || input.BindCount>8-input.BindPoint) return QkFacadeUnimpl("Shader.textureSlots");
                for(UINT i=0;i<input.BindCount;++i) shader->sampledMask|=1U<<(input.BindPoint+i);
            }
        }
    }
    shader->generation=generation; m_shaderCache[key]=shader;
    QkFacadeTrace(vertex?"QK-FACADE compile vs_4_0 cached\n":"QK-FACADE compile ps_4_0 cached\n");
    *out=shader.Detach(); return S_OK;
}

HRESULT QkD3D11State::InputLayout(QkFacadeShader *shader,ID3D11InputLayout **out)
{
    auto found=shader->layouts.find(m_fvf);
    if(found==shader->layouts.end()) {
        std::vector<D3D11_INPUT_ELEMENT_DESC> desc;
        UINT offset=0;
        bool rhw=(m_fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
        desc.push_back({"POSITION",0,rhw?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R32G32B32_FLOAT,0,offset,D3D11_INPUT_PER_VERTEX_DATA,0}); offset+=rhw?16:12;
        if(m_fvf&D3DFVF_NORMAL) { desc.push_back({"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offset,D3D11_INPUT_PER_VERTEX_DATA,0}); offset+=12; }
        else {
            // 原生成器统一声明 NORMAL，2D shader 不读取它，但 SM4 布局仍要求语义存在。
            desc.push_back({"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0});
        }
        if(m_fvf&D3DFVF_DIFFUSE) { desc.push_back({"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,offset,D3D11_INPUT_PER_VERTEX_DATA,0}); offset+=4; }
        if(m_fvf&D3DFVF_SPECULAR) { desc.push_back({"COLOR",1,DXGI_FORMAT_R8G8B8A8_UNORM,0,offset,D3D11_INPUT_PER_VERTEX_DATA,0}); offset+=4; }
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R32G32B32_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32_FLOAT};
        const UINT sizes[]={8,12,16,4};
        for(UINT n=0;n<((m_fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT);++n) {
            UINT dim=(m_fvf>>(16+2*n))&3;
            desc.push_back({"TEXCOORD",n,formats[dim],0,offset,D3D11_INPUT_PER_VERTEX_DATA,0}); offset+=sizes[dim];
        }
        ComPtr<ID3D11InputLayout> layout;
        HRESULT hr=device->CreateInputLayout(desc.data(),static_cast<UINT>(desc.size()),shader->code->GetBufferPointer(),shader->code->GetBufferSize(),&layout);
        if(FAILED(hr)) {
            ComPtr<ID3D11ShaderReflection> reflection;
            if(SUCCEEDED(D3DReflect(shader->code->GetBufferPointer(),shader->code->GetBufferSize(),IID_PPV_ARGS(&reflection)))) {
                D3D11_SHADER_DESC sd; reflection->GetDesc(&sd);
                for(UINT n=0;n<sd.InputParameters;++n) {
                    D3D11_SIGNATURE_PARAMETER_DESC param; reflection->GetInputParameterDesc(n,&param);
                    char line[128]; StringCchPrintfA(line,128,"QK-FACADE signature %s%u mask=%u\n",param.SemanticName,param.SemanticIndex,param.Mask); QkFacadeTrace(line);
                }
            }
            return hr;
        }
        found=shader->layouts.insert(std::make_pair(m_fvf,layout)).first;
    }
    *out=found->second.Get(); return S_OK;
}

static std::string QkArgument(DWORD arg,UINT stage)
{
    std::string s;
    switch(arg&D3DTA_SELECTMASK) {
    case D3DTA_CURRENT:s="c";break;
    case D3DTA_DIFFUSE:s="v.c";break;
    case D3DTA_TEXTURE:s="t"+std::to_string(stage);break;
    default:return "";
    }
    if(arg&D3DTA_ALPHAREPLICATE) s+=".aaaa";
    if(arg&D3DTA_COMPLEMENT) s="(1-"+s+")";
    return s;
}
static std::string QkOperation(DWORD op,const std::string &a,const std::string &b)
{
    if(a.empty() || b.empty()) return "";
    switch(op) {
    case D3DTOP_SELECTARG1:return a;
    case D3DTOP_SELECTARG2:return b;
    case D3DTOP_MODULATE:return "("+a+"*"+b+")";
    case D3DTOP_MODULATE2X:return "saturate(2*"+a+"*"+b+")";
    case D3DTOP_ADD:return "saturate("+a+"+"+b+")";
    default:return "";
    }
}
HRESULT QkD3D11State::ApplyShaders()
{
    ComPtr<QkFacadeShader> vs=m_boundVS,ps=m_boundPS;
    if(vs && !ps) return QkFacadeUnimpl("Draw.mixedShaderStages");
    if(!vs || !ps) {
        UINT count=(m_fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;
        std::string source="cbuffer Target:register(b0){float4 size;}; struct V{float4 p:SV_Position;float4 c:COLOR0;";
        for(UINT n=0;n<count;++n) source+="float2 uv"+std::to_string(n)+":TEXCOORD"+std::to_string(n)+";";
        source+="}; V VS(float4 p:POSITION,float4 c:COLOR0";
        for(UINT n=0;n<count;++n) source+=",float2 uv"+std::to_string(n)+":TEXCOORD"+std::to_string(n);
        source+="){V v;v.p=float4((p.x-size.z)*2/size.x-1,1-(p.y-size.w)*2/size.y,p.z,1);v.c=c.bgra;";
        for(UINT n=0;n<count;++n) source+="v.uv"+std::to_string(n)+"=uv"+std::to_string(n)+";";
        source+="return v;}";
        for(UINT n=0;n<8;++n) source+="Texture2D tex"+std::to_string(n)+":register(t"+std::to_string(n)+");SamplerState sam"+std::to_string(n)+":register(s"+std::to_string(n)+");";
        source+="float4 PS(V v):SV_Target{float4 c=v.c;";
        // 显式文字 PS 不读取固定功能 texture stage；只需生成配套 VS。
        for(UINT n=0;!ps && n<8 && m_stages[n][D3DTSS_COLOROP]!=D3DTOP_DISABLE;++n) {
            auto &st=m_stages[n];
            if(st[D3DTSS_TEXTURETRANSFORMFLAGS]!=D3DTTFF_DISABLE) return QkFacadeUnimpl("FF.textureTransform");
            bool texture=false;
            for(UINT alpha=0;alpha<2;++alpha) {
                DWORD op=st[alpha?D3DTSS_ALPHAOP:D3DTSS_COLOROP];
                if(op==D3DTOP_DISABLE) continue;
                if(op!=D3DTOP_SELECTARG2 && (st[alpha?D3DTSS_ALPHAARG1:D3DTSS_COLORARG1]&D3DTA_SELECTMASK)==D3DTA_TEXTURE) texture=true;
                if(op!=D3DTOP_SELECTARG1 && (st[alpha?D3DTSS_ALPHAARG2:D3DTSS_COLORARG2]&D3DTA_SELECTMASK)==D3DTA_TEXTURE) texture=true;
            }
            if(texture) {
                UINT coord=st[D3DTSS_TEXCOORDINDEX];
                if(coord>=count || !m_textures[n]) return QkFacadeUnimpl("FF.textureCoordinate");
                source+="float4 t"+std::to_string(n)+"=tex"+std::to_string(n)+".Sample(sam"+std::to_string(n)+",v.uv"+std::to_string(coord)+");";
            }
            auto rgb=QkOperation(st[D3DTSS_COLOROP],QkArgument(st[D3DTSS_COLORARG1],n),QkArgument(st[D3DTSS_COLORARG2],n));
            auto alpha=st[D3DTSS_ALPHAOP]==D3DTOP_DISABLE?"c":QkOperation(st[D3DTSS_ALPHAOP],QkArgument(st[D3DTSS_ALPHAARG1],n),QkArgument(st[D3DTSS_ALPHAARG2],n));
            if(rgb.empty() || alpha.empty()) return QkFacadeUnimpl("FF.operation");
            source+="c=float4(("+rgb+").rgb,("+alpha+").a);";
        }
        source+="return c;}";
        HRESULT hr=Compile(source.data(),static_cast<UINT>(source.size()),true,&vs); if(FAILED(hr)) return hr;
        if(!ps) { hr=Compile(source.data(),static_cast<UINT>(source.size()),false,&ps); if(FAILED(hr)) return hr; }
    }
    ID3D11InputLayout *layout=nullptr; HRESULT hr=InputLayout(vs.Get(),&layout); if(FAILED(hr)) return hr;
    context->IASetInputLayout(layout); context->VSSetShader(vs->vs.Get(),nullptr,0); context->PSSetShader(ps->ps.Get(),nullptr,0);
    for(UINT n=0;n<2;++n) {
        auto buffer=n?m_psConstants:m_vsConstants;
        D3D11_MAPPED_SUBRESOURCE map; hr=context->Map(buffer.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map); if(FAILED(hr)) return hr;
        memcpy(map.pData,n?m_psData:m_vsData,n?sizeof(m_psData):sizeof(m_vsData)); context->Unmap(buffer.Get(),0);
    }
    context->VSSetConstantBuffers(0,1,m_constants.GetAddressOf()); context->VSSetConstantBuffers(1,1,m_vsConstants.GetAddressOf());
    context->PSSetConstantBuffers(2,1,m_psConstants.GetAddressOf());
    for(UINT n=0;n<8;++n) {
        ID3D11ShaderResourceView *srv=(ps->sampledMask&(1U<<n)) && m_textures[n]?m_textures[n]->srv.Get():nullptr;
        if(srv && target) {
            ComPtr<ID3D11Resource> rt,res; target->GetResource(&rt); srv->GetResource(&res);
            if(rt.Get()==res.Get()) {
                D3D11_RENDER_TARGET_VIEW_DESC rd; target->GetDesc(&rd);
                D3D11_SHADER_RESOURCE_VIEW_DESC sd; srv->GetDesc(&sd);
                if(rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
                   (rd.Texture2D.MipSlice>=sd.Texture2D.MostDetailedMip && rd.Texture2D.MipSlice-sd.Texture2D.MostDetailedMip<sd.Texture2D.MipLevels)) return E_INVALIDARG;
            }
        }
        context->PSSetShaderResources(n,1,&srv);
        D3D11_SAMPLER_DESC sd={}; auto &st=m_samplers[n];
        if(srv && st[D3DSAMP_SRGBTEXTURE]) return QkFacadeUnimpl("Sampler.sRGB");
        if((st[D3DSAMP_MINFILTER]!=D3DTEXF_POINT && st[D3DSAMP_MINFILTER]!=D3DTEXF_LINEAR) ||
           (st[D3DSAMP_MAGFILTER]!=D3DTEXF_POINT && st[D3DSAMP_MAGFILTER]!=D3DTEXF_LINEAR)) {
            if(!srv) continue;
            return QkFacadeUnimpl("Sampler.filter");
        }
        sd.Filter=static_cast<D3D11_FILTER>((st[D3DSAMP_MINFILTER]==D3DTEXF_LINEAR?0x10:0)|(st[D3DSAMP_MAGFILTER]==D3DTEXF_LINEAR?4:0)|(st[D3DSAMP_MIPFILTER]==D3DTEXF_LINEAR?1:0));
        sd.AddressU=static_cast<D3D11_TEXTURE_ADDRESS_MODE>(st[D3DSAMP_ADDRESSU]);
        sd.AddressV=static_cast<D3D11_TEXTURE_ADDRESS_MODE>(st[D3DSAMP_ADDRESSV]); sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        if(!sd.AddressU) sd.AddressU=D3D11_TEXTURE_ADDRESS_WRAP; if(!sd.AddressV) sd.AddressV=D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxLOD=st[D3DSAMP_MIPFILTER]==D3DTEXF_NONE?0:D3D11_FLOAT32_MAX; sd.MinLOD=static_cast<float>(st[D3DSAMP_MAXMIPLEVEL]); sd.MaxAnisotropy=1;
        DWORD c=st[D3DSAMP_BORDERCOLOR]; sd.BorderColor[0]=((c>>16)&255)/255.f; sd.BorderColor[1]=((c>>8)&255)/255.f; sd.BorderColor[2]=(c&255)/255.f; sd.BorderColor[3]=(c>>24)/255.f;
        memcpy(&sd.MipLODBias,&st[D3DSAMP_MIPMAPLODBIAS],sizeof(float));
        std::string key(reinterpret_cast<char*>(&sd),sizeof(sd)); auto f=m_samplerCache.find(key);
        if(f==m_samplerCache.end()) { ComPtr<ID3D11SamplerState> sampler; hr=device->CreateSamplerState(&sd,&sampler); if(FAILED(hr)) return hr; f=m_samplerCache.insert(std::make_pair(key,sampler)).first; }
        context->PSSetSamplers(n,1,f->second.GetAddressOf());
    }
    return S_OK;
}

HRESULT QkD3D11State::Copy(QkSurfaceHandle *src,const RECT *srcRect,QkSurfaceHandle *dst,const RECT *dstRect,D3DTEXTUREFILTERTYPE filter)
{
    if(!src || !dst || src->generation!=generation || dst->generation!=generation) return E_INVALIDARG;
    RECT a=srcRect?*srcRect:RECT{0,0,static_cast<LONG>(src->desc.Width),static_cast<LONG>(src->desc.Height)};
    RECT b=dstRect?*dstRect:RECT{0,0,static_cast<LONG>(dst->desc.Width),static_cast<LONG>(dst->desc.Height)};
    if(a.left<0 || a.top<0 || a.right<=a.left || a.bottom<=a.top || static_cast<UINT>(a.right)>src->desc.Width || static_cast<UINT>(a.bottom)>src->desc.Height ||
       b.left<0 || b.top<0 || b.right<=b.left || b.bottom<=b.top || static_cast<UINT>(b.right)>dst->desc.Width || static_cast<UINT>(b.bottom)>dst->desc.Height) return E_INVALIDARG;
    bool sameSize=a.right-a.left==b.right-b.left && a.bottom-a.top==b.bottom-b.top;
    if(!src->texture) {
        if(!sameSize || !dst->texture || src->desc.Format!=dst->desc.Format) return E_INVALIDARG;
        BYTE *data=src->reference?src->reference:src->pixels.data(); if(!data) return E_UNEXPECTED;
        UINT bpp=QkPixelSize(src->desc.Format),pitch=src->desc.Width*bpp;
        HRESULT hr=dst->UploadRect(b,data+a.top*pitch+a.left*bpp,pitch);
        return FAILED(hr)?hr:DeviceStatus();
    }
    if(!dst->texture) return E_INVALIDARG;
    ID3D11ShaderResourceView *empty[8]={}; context->PSSetShaderResources(0,8,empty); context->OMSetRenderTargets(0,nullptr,nullptr);
    if(sameSize && src->desc.Format==dst->desc.Format && src->texture.Get()!=dst->texture.Get()) {
        D3D11_BOX box={static_cast<UINT>(a.left),static_cast<UINT>(a.top),0,static_cast<UINT>(a.right),static_cast<UINT>(a.bottom),1};
        context->CopySubresourceRegion(dst->texture.Get(),dst->mip,b.left,b.top,0,src->texture.Get(),src->mip,&box);
        QkFacadeTrace("QK-FACADE CopySubresourceRegion\n");
        return DeviceStatus();
    }
    // 缩放与同资源不同 mip 通过 shader blit；为源 mip 建独立 SRV，避免读写重叠。
    if(!dst->rtv || (src->texture.Get()==dst->texture.Get() && src->mip==dst->mip)) return QkFacadeUnimpl("Copy.overlappingSubresource");
    auto oldTarget=target; UINT oldW=width,oldH=height; auto oldVP=m_viewport; DWORD oldFVF=m_fvf;
    DWORD states[210],stages[8][33],samplers[16][14]; memcpy(states,m_states,sizeof(states)); memcpy(stages,m_stages,sizeof(stages)); memcpy(samplers,m_samplers,sizeof(samplers));
    auto oldVS=m_boundVS,oldPS=m_boundPS; auto oldTex=m_textures[0];
    ComPtr<QkTextureHandle> t; t.Attach(new QkTextureHandle); t->texture=src->texture; t->srv=src->srv; t->generation=generation;
    m_textures[0]=t; m_boundVS.Reset(); m_boundPS.Reset();
    target=dst->rtv; width=dst->desc.Width; height=dst->desc.Height; m_viewport={0,0,width,height,0,1};
    m_fvf=D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1;
    m_states[D3DRS_ALPHABLENDENABLE]=FALSE; m_states[D3DRS_SCISSORTESTENABLE]=FALSE; m_states[D3DRS_COLORWRITEENABLE]=15;
    m_stages[0][D3DTSS_COLOROP]=m_stages[0][D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;
    m_stages[0][D3DTSS_COLORARG1]=m_stages[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE; m_stages[0][D3DTSS_TEXCOORDINDEX]=0; m_stages[1][D3DTSS_COLOROP]=D3DTOP_DISABLE;
    m_samplers[0][D3DSAMP_MINFILTER]=m_samplers[0][D3DSAMP_MAGFILTER]=filter==D3DTEXF_LINEAR?D3DTEXF_LINEAR:D3DTEXF_POINT;
    m_samplers[0][D3DSAMP_ADDRESSU]=m_samplers[0][D3DSAMP_ADDRESSV]=D3DTADDRESS_CLAMP;
    struct V{float x,y,z;DWORD c;float u,v;};
    float l=static_cast<float>(b.left),top=static_cast<float>(b.top),right=static_cast<float>(b.right),bottom=static_cast<float>(b.bottom);
    float u=a.left/static_cast<float>(src->desc.Width),v=a.top/static_cast<float>(src->desc.Height),u1=a.right/static_cast<float>(src->desc.Width),v1=a.bottom/static_cast<float>(src->desc.Height);
    V vertices[]={{l,top,0,~0U,u,v},{right,top,0,~0U,u1,v},{l,bottom,0,~0U,u,v1},{right,bottom,0,~0U,u1,v1}};
    HRESULT hr=DrawUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V));
    if(SUCCEEDED(hr)) QkFacadeTrace("QK-FACADE shader blit\n");
    target=oldTarget;width=oldW;height=oldH;m_viewport=oldVP;m_fvf=oldFVF;m_boundVS=oldVS;m_boundPS=oldPS;m_textures[0]=oldTex;
    memcpy(m_states,states,sizeof(states));memcpy(m_stages,stages,sizeof(stages));memcpy(m_samplers,samplers,sizeof(samplers));
    return hr;
}
