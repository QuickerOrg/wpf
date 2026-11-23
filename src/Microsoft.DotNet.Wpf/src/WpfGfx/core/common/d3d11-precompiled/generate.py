"""离线 fxc 再生成/校验。普通 wpfgfx 构建直接使用随源码保存的头文件。

python generate.py --fxc <Windows SDK/x64/fxc.exe> --output <证据目录> [--check]
所有编译采用 /Ges /O1（D3DCompile flags=ENABLE_STRICTNESS，优化默认 level 1）。
以同 SDK D3DCompile 的字节逐一校验，反射资源槽写入表中以免运行时加载编译器。
"""
from pathlib import Path
import argparse, ctypes as C, json, hashlib, subprocess, uuid, re

HERE=Path(__file__).resolve().parent
CALL=C.WINFUNCTYPE
def method(obj,index,restype,*args):
    table=C.cast(obj,C.POINTER(C.POINTER(C.c_void_p))).contents
    return CALL(restype,C.c_void_p,*args)(table[index])
def blob(obj):
    return C.string_at(method(obj,3,C.c_void_p)(obj),method(obj,4,C.c_size_t)(obj))
def release(obj):
    if obj:method(obj,2,C.c_ulong)(obj)
class Desc(C.Structure):
    _fields_=[('version',C.c_uint),('creator',C.c_char_p),('flags',C.c_uint),('constantBuffers',C.c_uint),('resources',C.c_uint),('rest',C.c_uint*40)]
class Bind(C.Structure):
    _fields_=[('name',C.c_char_p),('type',C.c_uint),('point',C.c_uint),('count',C.c_uint),('flags',C.c_uint),('returnType',C.c_uint),('dimension',C.c_uint),('samples',C.c_uint)]

def main():
    p=argparse.ArgumentParser();p.add_argument('--fxc',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--check',action='store_true');p.add_argument('--runtime-compiler',type=Path);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False)
    core=HERE.parent.parent
    text=(core/'common/d3d11facade-resources.inl').read_text(encoding='utf-8')
    sections={'builtin':(core/'hw/d3d11-builtin-shaders.inl').read_text(encoding='utf-8'),'modern':text.split('static std::string QkModernHLSL')[1].split('// 只在预编译未命中')[0], 'fixed':text.split('std::string source="cbuffer Target:register(b0)')[1].split('sourceScope.Stop();')[0], 'copy':str(re.findall(r'static const char (?:vs|ps)\[\]="(.*?)";',text))}
    assert json.loads((HERE/'inputs.json').read_text(encoding='utf-8'))=={k:hashlib.sha256(v.encode()).hexdigest() for k,v in sections.items()},'HLSL generator inputs changed; refresh sources and inputs explicitly'
    runtime=a.runtime_compiler or a.fxc.parent/'d3dcompiler_47.dll'
    compiler=C.WinDLL(str(runtime))
    compiler.D3DCompile.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p,C.c_void_p,C.c_void_p,C.c_char_p,C.c_char_p,C.c_uint,C.c_uint,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]
    compiler.D3DCompile.restype=C.c_long
    compiler.D3DReflect.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_void_p)];compiler.D3DReflect.restype=C.c_long
    sources=json.loads((HERE/'sources.json').read_text(encoding='utf-8'))
    lines=['// 自动生成：d3d11-precompiled/generate.py；勿手改。', '#if QUICKER_WPF_D3D11_FACADE',
           'struct QkPrecompiledShader { const char *profile,*source; const BYTE *code; size_t size; UINT textures,samplers; };']
    rows=[];receipt=[]
    for i,item in enumerate(sources):
        source=item['source'].encode();profile=item['profile'];entry=item['entry']
        file=a.output/f'{i}.hlsl';file.write_bytes(source);output=a.output/f'{i}.dxbc'
        cmd=[str(a.fxc),'/nologo','/Ges','/O1','/T',profile,'/E',entry,'/Fo',str(output),str(file)]
        proc=subprocess.run(cmd,capture_output=True);assert proc.returncode==0,proc.stdout+proc.stderr
        code=output.read_bytes();b=C.c_void_p();error=C.c_void_p()
        hr=compiler.D3DCompile(source,len(source),b'QkOriginalHw',None,None,entry.encode(),profile.encode(),1<<11,0,C.byref(b),C.byref(error))
        assert hr>=0,blob(error) if error else hr
        same=blob(b)==code
        assert same,(i,'fxc/D3DCompile bytecode mismatch')
        release(b);release(error)
        ref=C.c_void_p();guid=C.create_string_buffer(uuid.UUID('8d536ca1-0cca-4956-a837-786963755584').bytes_le)
        assert compiler.D3DReflect(code,len(code),guid,C.byref(ref))>=0
        desc=Desc();assert method(ref,3,C.c_long,C.POINTER(Desc))(ref,C.byref(desc))>=0
        masks=[0,0]
        for j in range(desc.resources):
            bind=Bind();assert method(ref,6,C.c_long,C.c_uint,C.POINTER(Bind))(ref,j,C.byref(bind))>=0
            if bind.type in [2,3]:
                assert bind.point+bind.count<=8
                masks[0 if bind.type==2 else 1]|=((1<<bind.count)-1)<<bind.point
        release(ref)
        lines.append(f'static const BYTE qkBytecode{i}[] = {{'+','.join(str(v) for v in code)+'};')
        lines.append(f'static const char qkSource{i}[] = R"QK9('+item['source']+')QK9";')
        rows.append('{'+json.dumps(profile)+f',qkSource{i},qkBytecode{i},sizeof(qkBytecode{i}),{masks[0]},{masks[1]}'+'}')
        receipt.append({'index':i,'name':item['name'],'profile':profile,'entry':entry,'bytes':len(code),'sha256':hashlib.sha256(code).hexdigest(),'fxcEqualsRuntime':same,'textures':masks[0],'samplers':masks[1]})
    lines+=['static const QkPrecompiledShader qkPrecompiledShaders[] = {',',\n'.join(rows),'};','#endif','']
    header='\n'.join(lines).encode()
    target=HERE.parent/'d3d11-precompiled.h'
    if a.check:assert target.read_bytes()==header,'generated header differs'
    else:target.write_bytes(header)
    (a.output/'verification.json').write_text(json.dumps({'fxc':str(a.fxc),'fxcSha256':hashlib.sha256(a.fxc.read_bytes()).hexdigest(),'compiler':str(runtime),'compilerSha256':hashlib.sha256(runtime.read_bytes()).hexdigest(),'headerSha256':hashlib.sha256(header).hexdigest(),'shaders':receipt},indent=2),encoding='utf-8')
    print('shaders',len(rows),'header bytes',len(header),'all fxc bytes equal runtime D3DCompile')
if __name__=='__main__':main()
