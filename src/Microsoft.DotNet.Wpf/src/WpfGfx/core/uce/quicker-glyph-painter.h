#pragma once
#include "quicker-glyph-abi.h"
#include "../glyph/quicker-text-trace.h"
#include "../glyph/quicker-glyph-capture.h"
// 原 CBaseGlyphRunPainter 负责 realization、DPI、pixel snapping 和滤波 mask。
class QkPortGlyphPainter final : public CBaseGlyphRunPainter {
public:
 HRESULT Build(DrawGlyphsParameters& pars,CGlyphPainterMemory& memory,bool targetClearType,QkGlyphPacket& out,bool& visible){
  visible=false;
  if(!Init(&memory,pars.pGlyphRun,pars.pContextState))return S_OK;
  HRESULT hr=PrepareTransforms();if(FAILED(hr))return hr;
  RECT rect{};m_pRealization->GetAlphaMap(&m_pAlphaArray,&m_alphaArraySize,&rect);
  if(IsRectEmpty(&rect))return S_OK;
  QkCaptureGlyphAlpha(m_pAlphaArray,m_alphaArraySize,rect);
  if(rect.right-rect.left>16378||rect.bottom-rect.top>16382)return E_NOTIMPL;
  out.version=3;out.identity=m_pRealization->GetPortGlyphIdentity();out.generation=m_pRealization->GetPortAlphaGeneration();
  out.alpha=m_pAlphaArray;out.width=rect.right-rect.left;out.height=rect.bottom-rect.top;out.bytes=m_alphaArraySize;
  // 与 CD3DGlyphRun::MakeGeometry / CD3DGlyphRunPainter 相同的 overlap 边界。
  out.textureWidth=1;out.textureHeight=1;while(out.textureWidth<out.width+6)out.textureWidth*=2;while(out.textureHeight<out.height+2)out.textureHeight*=2;
  WCHAR tank[8]{};if(GetEnvironmentVariableW(L"QUICKER_TEXT_TANK",tank,8)==1&&tank[0]==L'1'){if(out.width+6>2048)return E_NOTIMPL;out.textureWidth=2048;}
  float xs[]={(float(rect.left-3)+1.5f)*(1.f/3),(float(rect.right+3)-1.5f)*(1.f/3)},ys[]={float(rect.top-1)+.5f,float(rect.bottom+1)-.5f};
  float kx=(1.f/out.textureWidth)*3,ky=1.f/out.textureHeight,dx=(1.f/out.textureWidth)*float(3-rect.left),dy=(1.f/out.textureHeight)*float(1-rect.top);
  for(int i=0;i<4;i++){
   float x=xs[(i==1||i==2)?1:0],y=ys[i>=2?1:0];
   out.quad[i]={x*m_xfGlyphWR.m_00+y*m_xfGlyphWR.m_10+m_xfGlyphWR.m_20,
                x*m_xfGlyphWR.m_01+y*m_xfGlyphWR.m_11+m_xfGlyphWR.m_21,
                x*kx+dx,y*ky+dy};
  }
  auto brush=pars.pBrushRealizer->GetRealizedBrushNoRef(false);if(!brush||brush->GetType()!=BrushSolid)return E_NOTIMPL;
  MilColorF color;static_cast<CMILBrushSolid*>(brush)->GetColor(&color);
  DWORD packed=Convert_MilColorF_scRGB_To_MilColorB_sRGB(&color);
  int r=MIL_COLOR_GET_RED(packed),g=MIL_COLOR_GET_GREEN(packed),b=MIL_COLOR_GET_BLUE(packed),a=MIL_COLOR_GET_ALPHA(packed);
  float fg[]={r/255.f,g/255.f,b/255.f,float((r+g+g+b)>>2)/255.f};
  UINT gamma=pars.pGlyphRun->GetGammaIndex();if(gamma>MAX_GAMMA_INDEX)return E_INVALIDARG;
  const auto& coefs=CGammaHandler::sc_gammaRatios[gamma];
  for(int i=0;i<4;i++){out.constants[2][i]=a/255.f;out.constants[3][i]=1;out.constants[4][i]=coefs.g1*fg[i]+coefs.g2;out.constants[5][i]=coefs.g3*fg[i]+coefs.g4;}
  out.foreground[0]=fg[0];out.foreground[1]=fg[1];out.foreground[2]=fg[2];out.foreground[3]=1;
  out.constants[1][0]=pars.pGlyphRun->BlueSubpixelOffset()*m_xfGlyphRW.m_00*kx;
  out.constants[1][1]=pars.pGlyphRun->BlueSubpixelOffset()*m_xfGlyphRW.m_01*ky;
  out.clearType=(m_recommendedBlendMode==ClearType)&&targetClearType;
  {float m[]={m_xfGlyphWR.m_00,m_xfGlyphWR.m_01,m_xfGlyphWR.m_10,m_xfGlyphWR.m_11,m_xfGlyphWR.m_20,m_xfGlyphWR.m_21};QkTraceTextState("port",packed,gamma,out.clearType!=0,m);}
  for(UINT i=2;i<6;i++){float values[6]={};memcpy(values,out.constants[i],4*sizeof(float));QkTraceTextState("port-constants",i,0,false,values);}
  visible=a!=0;return S_OK;
 }
};
