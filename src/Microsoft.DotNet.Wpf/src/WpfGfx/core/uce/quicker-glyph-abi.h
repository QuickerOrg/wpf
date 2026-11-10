#pragma once
// 同进程、同步调用。alpha 只包含原 WPF 字形 mask，不是整窗 CPU 图像。
struct QkGlyphVertex {float x,y,u,v;};
struct QkGlyphPacket {
 unsigned int version;
 const unsigned char* alpha;
 unsigned int width,height,bytes,textureWidth,textureHeight;
 unsigned long long identity,generation;
 QkGlyphVertex quad[4];
 float constants[6][4];
 float foreground[4];
 int clip[4],viewport[4];
 unsigned int clearType;
};
