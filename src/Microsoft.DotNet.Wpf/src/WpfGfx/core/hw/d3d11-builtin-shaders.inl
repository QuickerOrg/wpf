// 自动生成：运行 Quicker 实验 S0b/generate-builtin-shaders.py。仅门面变体包含。
static const char *QkBuiltinShader(UINT id, bool vertex) {
if(vertex) {
if(id!=VS_ShaderEffects20 && id!=VS_ShaderEffects30) return nullptr;
return R"QK(cbuffer Target:register(b0){float4 size;};cbuffer V:register(b1){float4x4 transform;};
struct O{float4 p:SV_Position;float4 c:COLOR0;float2 uv:TEXCOORD0;};
O VS(float4 p:POSITION,float2 uv:TEXCOORD0){O o;o.p=mul(p,transform);o.p.xy+=float2(1,-1)*o.p.w/size.xy;o.c=0;o.uv=uv;return o;})QK";
} switch(id) {
case g_PixelShader_Text11A_CTSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1,float2 uv2:TEXCOORD2):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=float4(uv2,0,0);
t0=float4(1,1,1,(tex0.Sample(sam0,uv0)).a);
t1=float4(1,1,1,(tex1.Sample(sam1,uv1)).a);
t2=float4(1,1,1,(tex2.Sample(sam2,uv2)).a);
r0=((c[1]*t0.aaaa)).rgba;
r0=(mad(c[2],t1.aaaa,r0)).rgba;
r0=(saturate(mad(c[3],t2.aaaa,r0))).rgba;
r1=(mad(c[4],r0,c[5])).rgba;
r1=(((r1*(1-r0))*4)).rgba;
r0=(mad(r0,r1,r0)).rgba;
return r0;})QK";
case g_PixelShader_Text11A_GSSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
t0=float4(1,1,1,(tex0.Sample(sam0,uv0)).a);
r0=(saturate(dot((t0.aaaa).rgb,(c[1]).rgb).xxxx)).rgba;
r1.a=(mad(c[4],r0,c[5])).a;
r1.a=(((r1*(1-r0))*4)).a;
r0=(mad(r0.aaaa,r1.aaaa,r0.aaaa)).rgba;
return r0;})QK";
case g_PixelShader_Text11A_CTTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
t0=tex0.Sample(sam0,uv0);
t1=float4(1,1,1,(tex1.Sample(sam1,uv1)).a);
t0=(saturate((t0*c[5].aaaa))).rgba;
r1=(saturate((t0*t1.aaaa))).rgba;
r1.rgb=(saturate(dot((r1).rgb,(c[0]).rgb).xxxx)).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=(mad(c[2],r1.aaaa,c[1])).rgb;
r0.a=(mad(c[2],r1.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r0.rgb=((((1-r1.aaaa)*r0)*4)).rgb;
r0.a=((mad(c[3],r1.bbbb,r0)*4)).a;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad((1-r1),r0,r1)).a;
return r0;})QK";
case g_PixelShader_Text11A_GSTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
t0=tex0.Sample(sam0,uv0);
t1=float4(1,1,1,(tex1.Sample(sam1,uv1)).a);
t0=(saturate((t0*c[5].aaaa))).rgba;
r1=(saturate((t0*t1.aaaa))).rgba;
t0.rgb=(dot((r1).rgb,(c[0]).rgb).xxxx).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=(mad(c[2],r1.aaaa,c[1])).rgb;
r0.a=(mad(c[2],t0.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r0.rgb=((((1-r1.aaaa)*r0)*4)).rgb;
r0.a=((mad(c[3],t0.bbbb,r0)*4)).a;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad((1-r1),r0,r1)).a;
return r0;})QK";
case g_PixelShader_Text11L_CTSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1,float2 uv2:TEXCOORD2):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=float4(uv2,0,0);
t0=float4((tex0.Sample(sam0,uv0)).rrr,1);
t1=float4((tex1.Sample(sam1,uv1)).rrr,1);
t2=float4((tex2.Sample(sam2,uv2)).rrr,1);
r0=((c[1]*t0)).rgba;
r0=(mad(c[2],t1,r0)).rgba;
r0=(saturate(mad(c[3],t2,r0))).rgba;
r1=(mad(c[4],r0,c[5])).rgba;
r1=(((r1*(1-r0))*4)).rgba;
r0=(mad(r0,r1,r0)).rgba;
return r0;})QK";
case g_PixelShader_Text11L_GSSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
t0=float4((tex0.Sample(sam0,uv0)).rrr,1);
r0=(saturate(dot((t0).rgb,(c[1]).rgb).xxxx)).rgba;
r1.a=(mad(c[4],r0,c[5])).a;
r1.a=(((r1*(1-r0))*4)).a;
r0=(mad(r0.aaaa,r1.aaaa,r0.aaaa)).rgba;
return r0;})QK";
case g_PixelShader_Text11L_CTTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
t0=tex0.Sample(sam0,uv0);
t1=float4((tex1.Sample(sam1,uv1)).rrr,1);
t0=(saturate((t0*c[5].aaaa))).rgba;
r1.rgb=((t0*t1)).rgb;
r1.a=(saturate((t0*t1.bbbb))).a;
r1.rgb=(saturate(dot((r1).rgb,(c[0]).rgb).xxxx)).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=(mad(c[2],r1.aaaa,c[1])).rgb;
r0.a=(mad(c[2],r1.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r0.rgb=((((1-r1.aaaa)*r0)*4)).rgb;
r0.a=((mad(c[3],r1.bbbb,r0)*4)).a;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad((1-r1),r0,r1)).a;
return r0;})QK";
case g_PixelShader_Text11L_GSTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
t0=tex0.Sample(sam0,uv0);
t1=float4((tex1.Sample(sam1,uv1)).rrr,1);
t0=(saturate((t0*c[5].aaaa))).rgba;
r1.rgb=(saturate((t0*t1))).rgb;
r1.a=(saturate((t0*t1.bbbb))).a;
t0.rgb=(dot((r1).rgb,(c[0]).rgb).xxxx).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=(mad(c[2],r1.aaaa,c[1])).rgb;
r0.a=(mad(c[2],t0.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r0.rgb=((((1-r1.aaaa)*r0)*4)).rgb;
r0.a=((mad(c[3],t0.bbbb,r0)*4)).a;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad((1-r1),r0,r1)).a;
return r0;})QK";
case g_PixelShader_Text20A_CTSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
r1=float4(1,1,1,(tex0.Sample(sam0,t0.xy)).a);
r0=((t0+c[1])).rgba;
r2=float4(1,1,1,(tex0.Sample(sam0,r0.xy)).a);
r0=((t0-c[1])).rgba;
r0=float4(1,1,1,(tex0.Sample(sam0,r0.xy)).a);
r0.r=(r0.aaaa).r;
r0.g=(r1.aaaa).g;
r0.b=(r2.aaaa).b;
r0.rgb=((r0*c[2])).rgb;
r0.a=((r1.aaaa*c[2].rrrr)).a;
r1.rgb=((c[4]*r0)).rgb;
r1.rgb=((r1+c[5])).rgb;
r2.rgb=((c[3]-r0)).rgb;
r1.rgb=((r1*r2)).rgb;
r1.rgb=((r1+r1)).rgb;
r1.rgb=((r1+r1)).rgb;
r0.rgb=(mad(r0,r1,r0)).rgb;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20A_GSSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
r0=float4(1,1,1,(tex0.Sample(sam0,t0.xy)).a);
r0.a=((r0*c[2])).a;
r1.a=((c[4]*r0)).a;
r1.a=((r1+c[5])).a;
r2.a=((c[3]-r0)).a;
r1.a=((r1*r2)).a;
r1.a=((r1+r1)).a;
r1.a=((r1+r1)).a;
r0=(mad(r0.aaaa,r1.aaaa,r0.aaaa)).rgba;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20A_CTTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
r0=tex0.Sample(sam0,t0.xy);
r1=float4(1,1,1,(tex1.Sample(sam1,t1.xy)).a);
r0=(saturate((r0*c[5].aaaa))).rgba;
r1=(saturate((r0*r1.aaaa))).rgba;
r1.rgb=(saturate(dot((r1).rgb,(c[0]).rgb).xxxx)).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=((c[2]*r1.aaaa)).rgb;
r0.rgb=((r0+c[1])).rgb;
r0.a=(mad(c[2],r1.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r3=((c[4]-r1)).rgba;
r0.rgb=((r3.aaaa*r0)).rgb;
r0.a=(mad(c[3],r1.bbbb,r0)).a;
r0=((r0+r0)).rgba;
r0=((r0+r0)).rgba;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad(r3,r0,r1)).a;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20A_GSTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
r2=tex0.Sample(sam0,t0.xy);
r0=float4(1,1,1,(tex1.Sample(sam1,t1.xy)).a);
r2=(saturate((r2*c[5].aaaa))).rgba;
r1=(saturate((r2*r0.aaaa))).rgba;
r2.rgb=(dot((r1).rgb,(c[0]).rgb).xxxx).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=((c[2]*r1.aaaa)).rgb;
r0.rgb=((r0+c[1])).rgb;
r0.a=(mad(c[2],r2.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r3=((c[4]-r1)).rgba;
r0.rgb=((r3.aaaa*r0)).rgb;
r0.a=(mad(c[3],r2.bbbb,r0)).a;
r0=((r0+r0)).rgba;
r0=((r0+r0)).rgba;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad(r3,r0,r1)).a;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20L_CTSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
r1=float4((tex0.Sample(sam0,t0.xy)).rrr,1);
r0=((t0+c[1])).rgba;
r2=float4((tex0.Sample(sam0,r0.xy)).rrr,1);
r0=((t0-c[1])).rgba;
r0=float4((tex0.Sample(sam0,r0.xy)).rrr,1);
r0.r=(r0.rrrr).r;
r0.g=(r1.gggg).g;
r0.b=(r2.bbbb).b;
r0.rgb=((r0*c[2])).rgb;
r0.a=((r1.gggg*c[2].rrrr)).a;
r1.rgb=((c[4]*r0)).rgb;
r1.rgb=((r1+c[5])).rgb;
r2.rgb=((c[3]-r0)).rgb;
r1.rgb=((r1*r2)).rgb;
r1.rgb=((r1+r1)).rgb;
r1.rgb=((r1+r1)).rgb;
r0.rgb=(mad(r0,r1,r0)).rgb;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20L_GSSB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=0,t2=0;
r0=float4((tex0.Sample(sam0,t0.xy)).rrr,1);
r0.a=((r0.rrrr*c[2].aaaa)).a;
r1.a=((c[4]*r0)).a;
r1.a=((r1+c[5])).a;
r2.a=((c[3]-r0)).a;
r1.a=((r1*r2)).a;
r1.a=((r1+r1)).a;
r1.a=((r1+r1)).a;
r0=(mad(r0.aaaa,r1.aaaa,r0.aaaa)).rgba;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20L_CTTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
r0=tex0.Sample(sam0,t0.xy);
r1=float4((tex1.Sample(sam1,t1.xy)).rrr,1);
r0=(saturate((r0*c[5].aaaa))).rgba;
r1=(saturate((r0*r1.rrrr))).rgba;
r1.rgb=(saturate(dot((r1).rgb,(c[0]).rgb).xxxx)).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=((c[2]*r1.aaaa)).rgb;
r0.rgb=((r0+c[1])).rgb;
r0.a=(mad(c[2],r1.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r3=((c[4]-r1)).rgba;
r0.rgb=((r3.aaaa*r0)).rgb;
r0.a=(mad(c[3],r1.bbbb,r0)).a;
r0=((r0+r0)).rgba;
r0=((r0+r0)).rgba;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad(r3,r0,r1)).a;
oC0=(r0).rgba;
return oC0;})QK";
case g_PixelShader_Text20L_GSTB_P0: return R"QK(cbuffer C:register(b2){float4 c[224];};
Texture2D tex0:register(t0);SamplerState sam0:register(s0);
Texture2D tex1:register(t1);SamplerState sam1:register(s1);
Texture2D tex2:register(t2);SamplerState sam2:register(s2);
Texture2D tex3:register(t3);SamplerState sam3:register(s3);
float4 PS(float4 position:SV_Position,float4 color:COLOR0,float2 uv0:TEXCOORD0,float2 uv1:TEXCOORD1):SV_Target{
float4 r0=0,r1=0,r2=0,r3=0,oC0=0;
float4 t0=float4(uv0,0,0),t1=float4(uv1,0,0),t2=0;
r2=tex0.Sample(sam0,t0.xy);
r0=float4((tex1.Sample(sam1,t1.xy)).rrr,1);
r2=(saturate((r2*c[5].aaaa))).rgba;
r1=(saturate((r2*r0.rrrr))).rgba;
r2.rgb=(dot((r1).rgb,(c[0]).rgb).xxxx).rgb;
r0.a=((c[1]*r1)).a;
r0.rgb=((c[2]*r1.aaaa)).rgb;
r0.rgb=((r0+c[1])).rgb;
r0.a=(mad(c[2],r2.bbbb,r0)).a;
r0.rgb=(mad(c[3],r1,r0)).rgb;
r0.a=((r1*r0)).a;
r3=((c[4]-r1)).rgba;
r0.rgb=((r3.aaaa*r0)).rgb;
r0.a=(mad(c[3],r2.bbbb,r0)).a;
r0=((r0+r0)).rgba;
r0=((r0+r0)).rgba;
r0.rgb=(mad(r1,r0,r1)).rgb;
r0.a=(mad(r3,r0,r1)).a;
oC0=(r0).rgba;
return oC0;})QK";
case PS_PassThroughShaderEffect: return R"QK(Texture2D t:register(t0);SamplerState s:register(s0);float4 PS(float4 p:SV_Position,float4 c:COLOR0,float2 uv:TEXCOORD0):SV_Target{return t.Sample(s,uv);})QK";
case PS_BlurH: return R"QK(cbuffer C:register(b2){float size : packoffset(c0);float sampleStart : packoffset(c1);float numSamples : packoffset(c2);float4x4 weights : packoffset(c3);};






















Texture2D g_samSrcColor_tex:register(t0);SamplerState g_samSrcColor:register(s0);

static const float maxSamples = 15; 
                                    



struct VS_OUTPUT{float4 Pos:SV_Position;float4 Color:COLOR0;float2 TexCoord:TEXCOORD0;};
float4 PS( VS_OUTPUT input ) : SV_Target
{
    float2 Tex = input.TexCoord;
    float4 Color = 0;
    float2 sampleTex;
    sampleTex.y = Tex.y;
    for (int i = 0; i < maxSamples; i++)
    {
	sampleTex.x = Tex.x + ((sampleStart + i) / size);
        Color += (g_samSrcColor_tex.Sample(g_samSrcColor, sampleTex ) * weights[i%4][i/4]); 
    }
    
    return Color;
}


)QK";
case PS_BlurV: return R"QK(cbuffer C:register(b2){float size : packoffset(c0);float sampleStart : packoffset(c1);float numSamples : packoffset(c2);float4x4 weights : packoffset(c3);};






















Texture2D g_samSrcColor_tex:register(t0);SamplerState g_samSrcColor:register(s0);

static const float maxSamples = 15; 
                                    



struct VS_OUTPUT{float4 Pos:SV_Position;float4 Color:COLOR0;float2 TexCoord:TEXCOORD0;};
float4 PS( VS_OUTPUT input ) : SV_Target
{
    float2 Tex = input.TexCoord;
    float4 Color = 0;
    float2 sampleTex;
    sampleTex.x = Tex.x;
    for (int i = 0; i < maxSamples; i++)
    {
	sampleTex.y = Tex.y + ((sampleStart + i) / size);
        Color += (g_samSrcColor_tex.Sample(g_samSrcColor, sampleTex ) * weights[i%4][i/4]); 
    }
    
    return Color;
}


)QK";
case PS_BlurHMulti: return R"QK(cbuffer C:register(b2){float size : packoffset(c0);float sampleStart : packoffset(c1);float numSamples : packoffset(c2);float4x4 weights : packoffset(c3);};






















Texture2D g_samSrcColor_tex:register(t0);SamplerState g_samSrcColor:register(s0);
Texture2D g_samAccumColor_tex:register(t1);SamplerState g_samAccumColor:register(s1);

static const float maxSamples = 15; 
                                    



struct VS_OUTPUT{float4 Pos:SV_Position;float4 Color:COLOR0;float2 TexCoord:TEXCOORD0;};
float4 PS( VS_OUTPUT input ) : SV_Target
{
    float2 Tex = input.TexCoord;
    float4 Color = g_samAccumColor_tex.Sample(g_samAccumColor, Tex);
    float2 sampleTex;
    sampleTex.y = Tex.y;
    for (int i = 0; i < maxSamples; i++)
    {
	sampleTex.x = Tex.x + ((sampleStart + i) / size);
        Color += (g_samSrcColor_tex.Sample(g_samSrcColor, sampleTex ) * weights[i%4][i/4]); 
    }
    
    return Color;
}


)QK";
case PS_BlurVMulti: return R"QK(cbuffer C:register(b2){float size : packoffset(c0);float sampleStart : packoffset(c1);float numSamples : packoffset(c2);float4x4 weights : packoffset(c3);};






















Texture2D g_samSrcColor_tex:register(t0);SamplerState g_samSrcColor:register(s0);
Texture2D g_samAccumColor_tex:register(t1);SamplerState g_samAccumColor:register(s1);

static const float maxSamples = 15; 
                                    



struct VS_OUTPUT{float4 Pos:SV_Position;float4 Color:COLOR0;float2 TexCoord:TEXCOORD0;};
float4 PS( VS_OUTPUT input ) : SV_Target
{
    float2 Tex = input.TexCoord;
    float4 Color = g_samAccumColor_tex.Sample(g_samAccumColor, Tex);
    float2 sampleTex;
    sampleTex.x = Tex.x;
    for (int i = 0; i < maxSamples; i++)
    {
	sampleTex.y = Tex.y + ((sampleStart + i) / size);
        Color += (g_samSrcColor_tex.Sample(g_samSrcColor, sampleTex ) * weights[i%4][i/4]); 
    }
    
    return Color;
}


)QK";
case PS_DropShadow: return R"QK(cbuffer C:register(b2){float2 offset : packoffset(c0);float3 shadowColor : packoffset(c1);float opacity : packoffset(c2);};





















Texture2D g_samSrcColor_tex:register(t0);SamplerState g_samSrcColor:register(s0);
Texture2D g_samShdColor_tex:register(t1);SamplerState g_samShdColor:register(s1);



struct VS_OUTPUT{float4 Pos:SV_Position;float4 Color:COLOR0;float2 TexCoord:TEXCOORD0;};
float4 PS( VS_OUTPUT input ) : SV_Target
{
    float2 Tex = input.TexCoord;

    float4 outColor;
    float4 srcColor = (g_samSrcColor_tex.Sample(g_samSrcColor, Tex));
    if (srcColor.a != 1.0f)
    {
        float2 shdTex;
        shdTex.x = Tex.x - offset.x; 
        shdTex.y = Tex.y - offset.y;
        float4 shdColor = (g_samShdColor_tex.Sample(g_samShdColor, shdTex));
        shdColor.rgb = shadowColor * shdColor.a * opacity;
        shdColor.a = shdColor.a * opacity;
        outColor = (1 - srcColor.a)*shdColor + srcColor;
    }
    else
    {
        outColor = srcColor;
    }

    return outColor;
}





)QK";
default:return nullptr;}}
