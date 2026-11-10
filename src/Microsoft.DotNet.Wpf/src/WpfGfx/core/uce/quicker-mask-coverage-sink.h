#pragma once
#include "quicker-coverage-sink.h"

// 移植 hwvertexbuffer.cpp 的 outside geometry/stratum 逻辑。
// 原条带先绘制、扫描线后绘制；不能将两者按生成顺序混合，也不能省掉零覆盖率区间。
class QkMaskCoverageSink final : public IGeometrySink {
    std::vector<QkCoverageVertex> strip, lines;
    RECT bounds;
    float top=FLT_MAX,bottom=-FLT_MAX,lastRight=0;
    bool inside;
    void V(float x,float y,float a){strip.push_back({x,y,a});}
    void Prepare(float yt,float yb,bool trap,float left=0,float right=0){
        bool end=yt==bounds.bottom&&yb==bounds.bottom;
        if(end||yb!=bottom){
            if(top!=FLT_MAX){float r=max(float(bounds.right),lastRight);V(r,top,0);V(r,bottom,0);V(r,bottom,0);}
            if(yt-bottom>0){float t=bottom==-FLT_MAX?float(bounds.top):bottom;
                V(float(bounds.left),t,0);V(float(bounds.left),t,0);V(float(bounds.left),yt,0);
                V(float(bounds.right),t,0);V(float(bounds.right),yt,0);V(float(bounds.right),yt,0);}
            if(trap){float l=min(float(bounds.left),left);V(l,yt,0);V(l,yt,0);V(l,yb,0);}
        }
        if(trap)lastRight=right;
        top=trap?yt:FLT_MAX;bottom=yb;
    }
    void Quad(std::vector<QkCoverageVertex>& out,float x0,float x1,float y,float a){
        QkCoverageVertex v[]={{x0,y,a},{x0,y+1,a},{x1,y,a},{x1,y+1,a}};
        for(UINT i=2;i<4;i++){out.push_back(v[i-2]);out.push_back(v[i-1]);out.push_back(v[i]);}
    }
public:
    std::vector<QkCoverageVertex> vertices;
    QkMaskCoverageSink(const RECT& rc,bool needInside):bounds(rc),inside(needInside){}
    HRESULT AddVertex(const MilPoint2F&,WORD*) override{return E_NOTIMPL;}
    HRESULT AddIndexedVertices(UINT,const void*,UINT,MilVertexFormat,UINT,const UINT*) override{return E_NOTIMPL;}
    void SetTransformMapping(const MILMatrix3x2&) override{}
    HRESULT AddTriangle(DWORD,DWORD,DWORD) override{return E_NOTIMPL;}
    HRESULT AddParallelogram(const MilPoint2F*) override{return E_NOTIMPL;}
    BOOL IsEmpty() override{return strip.empty()&&lines.empty();}
    HRESULT AddComplexScan(INT y,const CCoverageInterval* span) override{
        try{
            Prepare(float(y),float(y+1),false);
            for(;span->m_nPixelX!=INT_MAX;span=span->m_pNext){
                if(!span->m_pNext)return E_INVALIDARG;
                if(!inside&&span->m_nCoverage==c_nShiftSizeSquared)continue;
                LONG l=span->m_nPixelX,r=span->m_pNext->m_nPixelX;
                l=max(l,min(r,bounds.left));r=min(r,max(l,bounds.right));
                float a=float(span->m_nCoverage)/float(c_nShiftSizeSquared);
                if(float(y)+.5f<float(bounds.top)+1){
                    V(float(l),float(y),a);V(float(l),float(y),a);V(float(l),float(y+1),a);
                    V(float(r),float(y),a);V(float(r),float(y+1),a);V(float(r),float(y+1),a);
                }else Quad(lines,float(l),float(r),float(y),a);
            }
        }catch(...){return E_OUTOFMEMORY;}return S_OK;
    }
    HRESULT AddTrapezoid(float yt,float xl0,float xr0,float yb,float xl1,float xr1,float dl,float dr) override{
        try{
            Prepare(yt,yb,true,min(xl0,xl1),max(xr0,xr1));
            V(xl0-dl,yt,0);V(xl1-dl,yb,0);V(xl0+dl,yt,1);V(xl1+dl,yb,1);
            if(!inside){V(xl1+dl,yb,1);V(xr0-dr,yt,1);}
            V(xr0-dr,yt,1);V(xr1-dr,yb,1);V(xr0+dr,yt,0);V(xr1+dr,yb,0);
        }catch(...){return E_OUTOFMEMORY;}return S_OK;
    }
    HRESULT Finish(){
        try{
            Prepare(float(bounds.bottom),float(bounds.bottom),false);
            if(strip.size()*3+lines.size()>1000000)return E_OUTOFMEMORY;
            for(size_t i=2;i<strip.size();i++){vertices.push_back(strip[i-2]);vertices.push_back(strip[i-1]);vertices.push_back(strip[i]);}
            vertices.insert(vertices.end(),lines.begin(),lines.end());
        }catch(...){return E_OUTOFMEMORY;}return S_OK;
    }
};
