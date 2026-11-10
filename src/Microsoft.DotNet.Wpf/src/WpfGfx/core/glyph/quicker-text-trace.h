#pragma once
#if QUICKER_WPF_D3D11_FACADE
inline void QkTraceTextState(const char*,DWORD,UINT,bool,const float*) {}
inline void QkTraceGpuText(QkTextureHandle*) {}
inline DWORD QkTextRawBegin(QkTextureHandle*) { return MAXDWORD; }
inline void QkTextRawEnd(QkTextureHandle*,DWORD) {}
struct QkTextSampleState { HRESULT hr=S_OK; };
inline QkTextSampleState QkTextSampleBegin(QkTextureHandle*) { return {}; }
inline void QkTextSampleEnd(QkTextureHandle*,QkTextSampleState&) {}
#else
#pragma once
inline void QkTraceTextState(const char* role,DWORD color,UINT gamma,bool clearType,const float* matrix){
 WCHAR path[1024]{};if(!GetEnvironmentVariableW(L"QUICKER_G0C_TEXT_TRACE",path,1024))return;
 char line[512];int n=_snprintf_s(line,sizeof line,_TRUNCATE,"%s,%08lx,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\r\n",role,color,gamma,clearType?1:0,matrix?matrix[0]:0,matrix?matrix[1]:0,matrix?matrix[2]:0,matrix?matrix[3]:0,matrix?matrix[4]:0,matrix?matrix[5]:0);
 if(n<=0)return;HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(file,line,n,&written,nullptr);CloseHandle(file);}
}

inline void QkTraceGpuText(IDirect3DTexture9* texture){
 WCHAR path[1024]{};if(!GetEnvironmentVariableW(L"QUICKER_G0C_TEXT_TRACE",path,1024))return;
 D3DSURFACE_DESC desc{};if(FAILED(texture->GetLevelDesc(0,&desc)))return;
 float dims[]={float(desc.Width),float(desc.Height),0,0,0,0};QkTraceTextState("texture",desc.Format,desc.Width,false,dims);
 IDirect3DDevice9* dev=nullptr;if(FAILED(texture->GetDevice(&dev)))return;
 IDirect3DPixelShader9* ps=nullptr;if(SUCCEEDED(dev->GetPixelShader(&ps))&&ps){UINT bytes=0;if(SUCCEEDED(ps->GetFunction(nullptr,&bytes))&&bytes){std::vector<BYTE> data(bytes);if(SUCCEEDED(ps->GetFunction(data.data(),&bytes))){WCHAR shaderFile[1100]{};_snwprintf_s(shaderFile,ARRAYSIZE(shaderFile),_TRUNCATE,L"%s.psbin",path);HANDLE f=CreateFileW(shaderFile,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(f,data.data(),bytes,&written,nullptr);CloseHandle(f);}}}ps->Release();}
 D3DVIEWPORT9 viewport{};if(SUCCEEDED(dev->GetViewport(&viewport))){float v[]={float(viewport.X),float(viewport.Y),float(viewport.Width),float(viewport.Height),viewport.MinZ,viewport.MaxZ};QkTraceTextState("gpu-viewport",0,0,false,v);}
 // 仅用 D3D9 API 读取实际状态；与仓库 api_renderstate.cpp 相同的局部弃用例外。
#pragma warning(push)
#pragma warning(disable:4995)
 D3DMATRIX projection{};if(SUCCEEDED(dev->GetTransform(D3DTS_PROJECTION,&projection))){float v[]={projection._11,projection._22,projection._41,projection._42,projection._33,projection._44};QkTraceTextState("gpu-projection",0,0,false,v);}
#pragma warning(pop)
 for(UINT i=2;i<6;i++){float values[6]{};if(SUCCEEDED(dev->GetPixelShaderConstantF(i,values,1)))QkTraceTextState("gpu-constants",i,0,false,values);}
 DWORD blend=0,srgb=0,filter=0;dev->GetRenderState(D3DRS_BLENDFACTOR,&blend);dev->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&srgb);dev->GetSamplerState(0,D3DSAMP_MAGFILTER,&filter);QkTraceTextState("blend-sampler",blend,srgb,filter==D3DTEXF_LINEAR,nullptr);
 dev->Release();
}

// 仅诊断：输出文字 shader 原始 alpha，成对隔离采样/着色与混合阶段。
inline DWORD QkTextRawBegin(IDirect3DTexture9* texture){
 WCHAR v[8]{};if(GetEnvironmentVariableW(L"QUICKER_TEXT_RAW",v,8)!=1||v[0]!=L'1')return MAXDWORD;
 IDirect3DDevice9* dev=nullptr;DWORD old=MAXDWORD;if(SUCCEEDED(texture->GetDevice(&dev))){dev->GetRenderState(D3DRS_ALPHABLENDENABLE,&old);dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);dev->Release();}return old;
}
inline void QkTextRawEnd(IDirect3DTexture9* texture,DWORD old){if(old==MAXDWORD)return;IDirect3DDevice9* dev=nullptr;if(SUCCEEDED(texture->GetDevice(&dev))){dev->SetRenderState(D3DRS_ALPHABLENDENABLE,old);dev->Release();}}

// 从实际绑定 PS2 保留声明与 texld，仅替换后续计算为输出采样 alpha。
struct QkTextSampleState {HRESULT hr=S_OK;IDirect3DPixelShader9* old=nullptr;DWORD minFilter=0,magFilter=0;bool point=false;};
inline QkTextSampleState QkTextSampleBegin(IDirect3DTexture9* texture){
 QkTextSampleState state;WCHAR v[8]{};bool sample=GetEnvironmentVariableW(L"QUICKER_TEXT_SAMPLE",v,8)==1&&v[0]==L'1';bool point=GetEnvironmentVariableW(L"QUICKER_TEXT_POINT",v,8)==1&&v[0]==L'1';if(!sample&&!point)return state;
 IDirect3DDevice9* dev=nullptr;if(FAILED(texture->GetDevice(&dev)))return state;
 if(point){dev->GetSamplerState(0,D3DSAMP_MINFILTER,&state.minFilter);dev->GetSamplerState(0,D3DSAMP_MAGFILTER,&state.magFilter);dev->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);state.point=true;}
 if(sample)state.hr=E_FAIL;
 if(sample&&SUCCEEDED(dev->GetPixelShader(&state.old))&&state.old){UINT bytes=0;state.old->GetFunction(nullptr,&bytes);std::vector<DWORD> original(bytes/4);if(bytes&&SUCCEEDED(state.old->GetFunction(original.data(),&bytes))){std::vector<DWORD> code;code.push_back(original[0]);for(size_t i=1;i<original.size();){DWORD token=original[i];UINT op=token&0xffff;if(op==0xffff)break;UINT n=(token>>24)&15;if(op==0xfffe){i+=1+((token>>16)&0x7fff);continue;}if(!n||i+n>=original.size())break;if(op==31||op==66){for(UINT j=0;j<=n;j++)code.push_back(original[i+j]);}if(op==66){code.push_back(0x02000001);code.push_back(0x800f0000);code.push_back(0x80ff0000);code.push_back(0x02000001);code.push_back(0x800f0800);code.push_back(0x80e40000);code.push_back(0x0000ffff);break;}i+=n+1;}IDirect3DPixelShader9* ps=nullptr;if(!code.empty()&&code.back()==0xffff&&SUCCEEDED(dev->CreatePixelShader(code.data(),&ps))){state.hr=dev->SetPixelShader(ps);ps->Release();}}}
 dev->Release();return state;
}
inline void QkTextSampleEnd(IDirect3DTexture9* texture,QkTextSampleState& state){
 if(!state.old&&!state.point)return;IDirect3DDevice9* dev=nullptr;if(SUCCEEDED(texture->GetDevice(&dev))){if(state.old)dev->SetPixelShader(state.old);if(state.point){dev->SetSamplerState(0,D3DSAMP_MINFILTER,state.minFilter);dev->SetSamplerState(0,D3DSAMP_MAGFILTER,state.magFilter);}dev->Release();}if(state.old)state.old->Release();
}

#endif
