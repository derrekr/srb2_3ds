// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief NDS 3D API for SRB2.

#ifndef __R_NDS3D__
#define __R_NDS3D__

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"

typedef float FCOORD;

typedef struct {
	u32 vertsPF;
	u32 texChangesPF;
	u64 msPF;
	u64 msWaiting;
	u64 msStart2Finish;
	u64 msFinish2Start;
	u64 parts[0x20];
	u64 starts[0x20];
	u64 acc[0x20];
} n3dsRenderStats;

#ifdef N3DS_PERF_MEASURE
void NDS3D_ResetRenderStats();
n3dsRenderStats *NDS3D_GetRenderStats();
void NDS3D_ResetRenderStatsMeasureBegin(int part);
void NDS3D_ResetRenderStatsMeasureEnd(int part);
#else
#define NDS3D_ResetRenderStatsMeasureEnd(x) 
#define NDS3D_ResetRenderStatsMeasureBegin(x) 
#define renderStatsEndMeasure() 
#define renderStatsStartMeasure() 
#define NDS3D_GetRenderStats() 
#define NDS3D_ResetRenderStats() 
#define renderStatsLogFrameLiveness(x) 

#endif

boolean NDS3D_Init();
void NDS3D_Shutdown(void);
void NDS3D_SetPalette(RGBA_t *ppal, RGBA_t *pgamma);
void NDS3D_FinishUpdate();
void NDS3D_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color);
void NDS3D_DrawPolygon(u32 surfColor, u32 geomIdx, FUINT iNumPts, FBITFIELD PolyFlags, C3D_Tex *texture, u32 fogColor, u32 fogDensity);
void NDS3D_SetBlend(FBITFIELD PolyFlags);
void NDS3D_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, u32 ClearColor);
void NDS3D_SetTexture(FTextureInfo *TexInfo);
void NDS3D_ReadRect(INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data);
void NDS3D_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip);
void NDS3D_ClearMipMapCache(void);
void NDS3D_SetSpecialState(hwdspecialstate_t IdState, INT32 Value);
void NDS3D_DrawMD2(INT32 *gl_cmd_buffer, md2_frame_t *frame, FTransform *pos, float scale);
void NDS3D_DrawMD2i(INT32 *gl_cmd_buffer, md2_frame_t *frame, UINT32 duration, UINT32 tics, md2_frame_t *nextframe, FTransform *pos, float scale, UINT8 flipped, UINT8 *color);
void NDS3D_SetTransform(FTransform *ptransform);
INT32 NDS3D_GetTextureUsed(void);
INT32 NDS3D_GetRenderVersion(void);
void NDS3D_FlushScreenTextures(void);
void NDS3D_StartScreenWipe(void);
void NDS3D_EndScreenWipe(void);
void NDS3D_DoScreenWipe(float alpha);
void NDS3D_DrawIntermissionBG(void);
void NDS3D_MakeScreenTexture(void);
void NDS3D_MakeScreenFinalTexture(void);
void NDS3D_DrawScreenFinalTexture(int width, int height);

#endif
