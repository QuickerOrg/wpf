#pragma once
#include <vector>
#include "quicker-coverage-abi.h"

// 复用 CHwRasterizer；顶点排列对应 hwvertexbuffer.cpp 的标准梯形及 AddLine 四边形。
// 不实现 outside geometry、waffling、3D 或其他生成器，未支持入口明确失败。
class QkCoverageSink final : public IGeometrySink
{
public:
    std::vector<QkCoverageVertex> vertices;
    UINT trapezoids = 0, scans = 0;
    HRESULT AddVertex(const MilPoint2F&, WORD*) override { return E_NOTIMPL; }
    HRESULT AddIndexedVertices(UINT,const void*,UINT,MilVertexFormat,UINT,const UINT*) override { return E_NOTIMPL; }
    void SetTransformMapping(const MILMatrix3x2&) override {}
    HRESULT AddTriangle(DWORD,DWORD,DWORD) override { return E_NOTIMPL; }
    HRESULT AddParallelogram(const MilPoint2F*) override { return E_NOTIMPL; }
    BOOL IsEmpty() override { return vertices.empty(); }
    HRESULT Strip(const QkCoverageVertex* v, UINT count)
    {
        if(vertices.size() + (count-2)*3 > 1000000) return E_OUTOFMEMORY;
        try { for(UINT i=2;i<count;++i){vertices.push_back(v[i-2]);vertices.push_back(v[i-1]);vertices.push_back(v[i]);} }
        catch(...) { return E_OUTOFMEMORY; }
        return S_OK;
    }
    HRESULT AddComplexScan(INT y, const CCoverageInterval* span) override
    {
        ++scans;
        for(;span->m_nPixelX!=INT_MAX;span=span->m_pNext)
        {
            if(!span->m_pNext) return E_INVALIDARG;
            if(!span->m_nCoverage) continue;
            float coverage=float(span->m_nCoverage)/float(c_nShiftSizeSquared);
            float x0=float(span->m_nPixelX),x1=float(span->m_pNext->m_nPixelX);
            // 原 AddComplexScan 先加 0.5，AddLine 转三角形时再减 0.5，等价于这里的边界坐标。
            QkCoverageVertex v[]={{x0,float(y),coverage},{x0,float(y+1),coverage},{x1,float(y),coverage},{x1,float(y+1),coverage}};
            HRESULT hr=Strip(v,4);if(FAILED(hr))return hr;
        }
        return S_OK;
    }
    HRESULT AddTrapezoid(float yt,float xl0,float xr0,float yb,float xl1,float xr1,float dl,float dr) override
    {
        ++trapezoids;
        // 标准条带的首尾退化顶点只负责连接批次；这里每个条带独立为三角形列表，无需复制。
        QkCoverageVertex v[]={{xl0-dl,yt,0},{xl1-dl,yb,0},{xl0+dl,yt,1},{xl1+dl,yb,1},
            {xr0-dr,yt,1},{xr1-dr,yb,1},{xr0+dr,yt,0},{xr1+dr,yb,0}};
        return Strip(v,8);
    }
};
