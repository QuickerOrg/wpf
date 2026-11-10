#pragma once
// 仅显式诊断。观察原状态机，不增加 pass、不改变 realization 的选择或失效。
UINT64 QkG0hCompositionFrame();
inline UINT64 QkG0hHash(const void* data,size_t size){const BYTE* b=static_cast<const BYTE*>(data);UINT64 h=14695981039346656037ULL;for(size_t i=0;i<size;i++){h^=b[i];h*=1099511628211ULL;}return h;}
inline void QkG0hGlyphTrace(const char* event,CGlyphRunResource* run,CGlyphRunRealization* r,float reqX,float reqY,UTC_TIME frame,const float* matrix=nullptr){
 static const bool enabled=[](){WCHAR v[2];return GetEnvironmentVariableW(L"QUICKER_G0H_GLYPH_TRACE",v,2)>0;}();if(!enabled||!run)return;
 WCHAR path[1024];DWORD n=GetEnvironmentVariableW(L"QUICKER_G0H_GLYPH_TRACE",path,1024);if(!n||n>=1024)return;
 LARGE_INTEGER q;QueryPerformanceCounter(&q);UINT64 key=QkG0hHash(run->GetGlyphIndices(),run->GetGlyphCount()*sizeof(USHORT));UINT64 alpha=0;RECT box{};float sx=0,sy=0,fx=0,fy=0;int anim=-1;
 if(r){sx=r->GetScaleX();sy=r->GetScaleY();anim=r->IsAnimationQuality()?1:0;if(anim){fx=r->LastFulfilledScaleX();fy=r->LastFulfilledScaleY();}
  if(r->HasAlphaMaps()){BYTE* bytes=nullptr;UINT size=0;r->GetAlphaMap(&bytes,&size,&box);UINT64 valid=UINT64(box.right-box.left)*UINT64(box.bottom-box.top);if(bytes&&valid<=size)alpha=QkG0hHash(bytes,static_cast<size_t>(valid));}}
 float z[6]{};if(!matrix)matrix=z;char line[1024];int len=_snprintf_s(line,sizeof line,_TRUNCATE,"%s,%lld,%llu,%llu,%p,%llu,%016llx,%u,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%016llx,%ld,%ld,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",event,q.QuadPart,QkG0hCompositionFrame(),static_cast<UINT64>(frame?frame:(r?r->LastUsedFrame():0)),run,r?r->GetPortGlyphIdentity():0,key,run->GetGlyphCount(),run->GetMUSize(),anim,reqX,reqY,sx,sy,fx,fy,alpha,box.right-box.left,box.bottom-box.top,matrix[0],matrix[1],matrix[2],matrix[3],matrix[4],matrix[5]);
 if(len<=0)return;HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f!=INVALID_HANDLE_VALUE){DWORD done;WriteFile(f,line,len,&done,nullptr);CloseHandle(f);}
}
