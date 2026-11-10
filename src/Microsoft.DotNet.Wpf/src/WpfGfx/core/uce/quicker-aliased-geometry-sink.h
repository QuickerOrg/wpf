#pragma once
#include <vector>
// 只接收原 IShapeData::SetupFillTessellator 的结果，不实现新的细分算法。
class QkAliasedGeometrySink final : public IGeometrySink {
 std::vector<QkCoverageVertex> points;
 HRESULT Triangle(const QkCoverageVertex& a,const QkCoverageVertex& b,const QkCoverageVertex& c){if(vertices.size()+3>1000000)return E_OUTOFMEMORY;try{vertices.push_back(a);vertices.push_back(b);vertices.push_back(c);return S_OK;}catch(...){return E_OUTOFMEMORY;}}
public:
 static const UINT TessellatorBufferSize=(MAX_SPACE_FOR_TYPE(CTessellator)>MAX_SPACE_FOR_TYPE(CRectFillTessellator)?MAX_SPACE_FOR_TYPE(CTessellator):MAX_SPACE_FOR_TYPE(CRectFillTessellator));
 std::vector<QkCoverageVertex> vertices;
 HRESULT AddVertex(const MilPoint2F& p,WORD* index) override{if(!index)return E_POINTER;if(points.size()>=65536)return E_OUTOFMEMORY;try{*index=static_cast<WORD>(points.size());points.push_back({p.X,p.Y,1});return S_OK;}catch(...){return E_OUTOFMEMORY;}}
 HRESULT AddTriangle(DWORD a,DWORD b,DWORD c) override{if(a>=points.size()||b>=points.size()||c>=points.size())return E_INVALIDARG;return Triangle(points[a],points[b],points[c]);}
 HRESULT AddParallelogram(const MilPoint2F* p) override{if(!p)return E_POINTER;QkCoverageVertex v[]={{p[0].X,p[0].Y,1},{p[1].X,p[1].Y,1},{p[2].X,p[2].Y,1},{p[3].X,p[3].Y,1}};HRESULT hr=Triangle(v[0],v[1],v[3]);return FAILED(hr)?hr:Triangle(v[3],v[1],v[2]);}
 HRESULT AddIndexedVertices(UINT,const void*,UINT,MilVertexFormat,UINT,const UINT*) override{return E_NOTIMPL;}
 void SetTransformMapping(const MILMatrix3x2&) override{}
 BOOL IsEmpty() override{return vertices.empty();}
 HRESULT AddComplexScan(INT,const CCoverageInterval*) override{return E_NOTIMPL;}
 HRESULT AddTrapezoid(float,float,float,float,float,float,float,float) override{return E_NOTIMPL;}
};
