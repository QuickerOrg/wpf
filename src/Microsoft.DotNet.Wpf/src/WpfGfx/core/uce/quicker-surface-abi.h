#pragma once
// 同进程、同步传递；来源是 D3D11 GPU 纹理，不接受 CPU 整窗图像。
struct QkSurfacePacket {
 unsigned int version,width,height,kind,radius,quality,linear;
 void* source;
 float weights[101],color[4],opacity,alphaScale,offset[2];
 QkGlyphVertex quad[4];
 int clip[4],viewport[4];
 float transform[4][4];
};
