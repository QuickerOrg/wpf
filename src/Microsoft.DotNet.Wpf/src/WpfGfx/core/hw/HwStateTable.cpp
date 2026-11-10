// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.


//+-----------------------------------------------------------------------------
//

//
//  $TAG ENGR

//      $Module:    win_mil_graphics_d3d
//      $Keywords:
//
//  $Description:
//      Contains CStateTable implementation
//
//  $ENDTAG
//
//------------------------------------------------------------------------------

#include "precomp.hpp"

MtDefine(StateTableMemory, MILRawMemory, "StateTableMemory");

// Explicit template instantiation

template class CStateTable<QkBaseTextureHandle *>;

template class CStateTable<QkVertexShaderHandle *>;
#if !QUICKER_WPF_D3D11_FACADE
template class CStateTable<QkPixelShaderHandle *>;
#endif

template class CStateTable<QkIndexBufferHandle *>;
template class CStateTable<QkVertexBufferHandle *>;
template class CStateTable<QkSurfaceHandle *>;



