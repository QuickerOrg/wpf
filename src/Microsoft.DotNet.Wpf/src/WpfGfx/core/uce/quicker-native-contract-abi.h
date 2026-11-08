#pragma once
// 仅原生契约探针使用；op: 0=begin, 1=line, 2=cubic, 3=end。
struct QkNativeSegment { int op; float x1,y1,x2,y2,x3,y3; int flag; };
