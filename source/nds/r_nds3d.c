// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Portions Copyright (C) 1998-2000 by DooM Legacy Team.
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
//
// In an ideal world, we would share as much code as possible with r_opengl.c,
// but this will do for now.

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include <citro3d.h>

#define __BYTEBOOL__
#define boolean bool

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"
#include "vshader_shbin.h"
#include "r_nds3d.h"
#include "r_queue.h"
#include "r_texcache.h"
#include "nds_utils.h"

// Used to transfer the final rendered display to the framebuffer
#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

// Used to convert textures to 3DS tiled format
// Note: vertical flip flag set so 0,0 is top left of texture
#define TEXTURE_TRANSFER_BASE_FLAGS \
	(GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0))

#define MAX_FOG_DENSITY			255

extern LightEvent workerInitialized;

// XXX arbitrary near and far plane
FCOORD NEAR_CLIPPING_PLANE = NZCLIP_PLANE;
FCOORD FAR_CLIPPING_PLANE = 20000.0f;
const float fov = 62.0f;

// Render targets
static	C3D_RenderTarget *	targetLeft;
static	C3D_RenderTarget *	targetRight;
static	C3D_RenderTarget *	targetWide;
static	bool				drawing;

// Buffer and Attribute info
static	C3D_BufInfo *bufInfo;
static	C3D_AttrInfo *attrInfo;

static void *geometryBuf;

// Shader programs
static	const void *vshaderData = /*nds3d_*/vshader_shbin;
//static	size_t vshaderSize = /*nds3d_*/vshader_shbin_size;
static	size_t vshaderSize;
static	DVLB_s *vshader_dvlb;
static	shaderProgram_s shaderProgram;
// Uniforms
static	int uLoc_projection, uLoc_modelView;
// Matrices
static C3D_Mtx defaultModelView;
// Special Sate
static	C3D_FogLut	*fogLUTs[(MAX_FOG_DENSITY+1)/2];
static	u32			prevFogDensity = -1;
static	u32			prevFogColor = -1;
static	bool		fogEnabled;
// Misc
static	C3D_Tex *prevTex;
static	u32 clearCounter;
static  bool fading;
static  u32 fadeColor;

static float iod;
static float focalLength;

static bool wideModeEnabled;

static n3dsRenderStats renderStats;


//#define NDS3D_INDEX_LIST_DRAWING


static void InitRendererMode(void);

static bool get3DSettings(float *_iod, float *_foclen)
{
	extern float sliderState;
	extern consvar_t cv_3dsfoclen;

	*_iod = -sliderState/4;
	*_foclen = ((float) (cv_3dsfoclen.value / 10)) / 10.0f;

	if (sliderState == 0.0f)
		return false;	// 3d not enabled

	return true;
}

static void setScreenMode(bool enable_3d)
{
	extern consvar_t cv_3dswidemode;
	bool wideModePrev = wideModeEnabled;

	if (enable_3d)
	{
		gfxSetWide(false);
		gfxSet3D(true);
		wideModeEnabled = false;
	}
	else
	{
		gfxSet3D(false);
		if (cv_3dswidemode.value)
		{
			wideModeEnabled = true;
		}
		else wideModeEnabled = false;
	}

	/* allow smooth transition between modes */
	if (wideModePrev == wideModeEnabled)
	{
		if (wideModeEnabled)
		{
			gfxSetWide(true);
			C3D_RenderTargetSetOutput(targetWide,  GFX_TOP, 0,  DISPLAY_TRANSFER_FLAGS);
		}
		else
		{
			gfxSet3D(enable_3d);
			C3D_RenderTargetSetOutput(targetLeft,  GFX_TOP, GFX_LEFT,  DISPLAY_TRANSFER_FLAGS);
			C3D_RenderTargetSetOutput(targetRight, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
		}
	}
	else
	{
		gfxSetWide(wideModePrev);
	}
}

void *I_InitVertexBuffer(const size_t geoBufSize)
{
	geometryBuf = linearAlloc(geoBufSize);
	if(!geometryBuf)
	{
		NDS3D_driverPanic("Failed to allocate geometry buf!\n");
		return NULL;
	}
	
	// Configure attributes for use with the vertex shader
	attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	/*
	typedef struct
	{
		FLOAT       x,y,z;
		FLOAT       sow;            // s texture ordinate (s over w)
		FLOAT       tow;            // t texture ordinate (t over w)
	} FOutVector;
	*/
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);	// v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);	// v1=texcoord
	
	// Configure buffers
	bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, geometryBuf, sizeof(FOutVector), 2, 0x10);
	
	return geometryBuf;
}


/*
static void indexListAdd(size_t numVectors)
{
	const size_t remaining = MAX_NUM_INDICES - indexListIndex;
	const size_t toWrite = numVectors + (numVectors >> 2);
	s16 *ptr = &indexList[indexListIndex];
	s16 *endPtr = ptr + toWrite;
	
	if(numVectors >= UINT16_MAX/4)
		NDS3D_driverPanic("Polygon too large!\n");
	
	if(numVectors < 3)
		NDS3D_driverPanic("Polygon too small!\n");
		
	if(numVectors % 4)
		NDS3D_driverPanic("Polygon is not a quad!\n");
	
	if(remaining < toWrite)
		NDS3D_driverPanic("Geometry buffer is full!\n");
	else
	{
		do
		{
			*ptr++ = primitveIndex++;
			*ptr++ = primitveIndex++;
			*ptr++ = primitveIndex++;
			*ptr++ = primitveIndex++;
			*ptr++ = -1;	// indicate end of a quad
		} while(ptr < endPtr);
		
		indexListIndex += toWrite;
	}
}
*/

#ifdef N3DS_PERF_MEASURE
void NDS3D_ResetRenderStats()
{
	memset(&renderStats, 0, sizeof renderStats);
}

n3dsRenderStats *NDS3D_GetRenderStats()
{
	return &renderStats;
}

static u64 startTime;
static void renderStatsStartMeasure()
{
	startTime = osGetTime();
}
static void renderStatsEndMeasure()
{
	u64 diff = osGetTime() - startTime;
	if(diff + startTime < startTime || renderStats.msPF + diff < renderStats.msPF)
		NDS3D_driverPanic("renderStatsEndMeasure overflow!\n");
	renderStats.msPF += diff;
}

static void renderStatsLogFrameLiveness(bool start)
{
	static u64 startTime;
	static u64 endTime;

	u64 cur = osGetTime();

	if(start)
	{
		startTime = cur;
		renderStats.msFinish2Start = cur - endTime;
	}
	else
	{
		endTime = cur;
		renderStats.msStart2Finish = cur - startTime;
	}
}

void NDS3D_ResetRenderStatsMeasureBegin(int part)
{
	renderStats.parts[part] = osGetTime();
}

void NDS3D_ResetRenderStatsMeasureEnd(int part)
{
	renderStats.parts[part] = osGetTime() - renderStats.parts[part];
}

void NDS3D_ResetRenderStatsMeasureBeginAcc(int part)
{
	renderStats.starts[part] = osGetTime();
}

void NDS3D_ResetRenderStatsMeasureEndAcc(int part)
{
	renderStats.acc[part] += osGetTime() - renderStats.starts[part];
}

#endif

static void InitTextureUnits()
{
	C3D_TexEnv *env;

	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvOpRgb(env, 0, 0, 0);
	C3D_TexEnvOpAlpha(env, 0, 0, 0);

	env = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env);
	C3D_TexEnvOpRgb(env, 0, 0, 0);
	C3D_TexEnvOpAlpha(env, 0, 0, 0);
}
static void InitDefaultMatrices()
{
	Mtx_Identity(&defaultModelView);

	Mtx_Scale(&defaultModelView, 1.0f, 0.6f, 1.0f);	// XXX arbitrary
}

boolean NDS3D_Init()
{	
	NDS3D_driverLog("NDS3D_Init\n");

	// Initialize the render targets
	targetLeft  = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	targetRight = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(targetLeft,  GFX_TOP, GFX_LEFT,  DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetClear(targetLeft, C3D_CLEAR_ALL, 0, 0);
	C3D_RenderTargetSetOutput(targetRight, GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);
	C3D_RenderTargetClear(targetRight, C3D_CLEAR_ALL, 0, 0);

	targetWide  = C3D_RenderTargetCreate(240, 800, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	
	InitTextureUnits();

	//C3D_CullFace(GPU_CULL_BACK_CCW );
	C3D_CullFace(GPU_CULL_NONE);

	C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
	
	vshaderSize = vshader_shbin_size;
	if(vshaderSize == 0)
		NDS3D_driverPanic("Invalid shader bin size!\n");
	
	// Load the vertex shader, create a shader program and bind it
	vshader_dvlb = DVLB_ParseFile((u32*)vshaderData, vshaderSize);
	shaderProgramInit(&shaderProgram);
	shaderProgramSetVsh(&shaderProgram, &vshader_dvlb->DVLE[0]);
	C3D_BindProgram(&shaderProgram);
	
	// Get the location of the uniforms
	uLoc_projection   = shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "projection");
	uLoc_modelView    = shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "modelView");
	
	InitDefaultMatrices();

	// ensure uniforms are initialized
	NDS3D_SetTransform(NULL);

	InitRendererMode();

	NDS3D_ResetRenderStats();

	NDS3D_driverLog("NDS3D_Init done\n");
	
	return true;
}

void NDS3D_Shutdown(void)
{
	NDS3D_driverLog("NDS3D_Shutdown\n");

	NDS3D_FinishUpdate();

	if(geometryBuf)
		linearFree(geometryBuf);
	
	// Free the shader program
	shaderProgramFree(&shaderProgram);
	DVLB_Free(vshader_dvlb);
	
	C3D_Fini();
}



static inline void debugPrintPerfStats()
{
	//printf("\x1b[29;1Hgpu: %5.2f%%  cpu: %5.2f%%  buf:%5.2f%%\n", C3D_GetDrawingTime()*6, C3D_GetProcessingTime()*6, C3D_GetCmdBufUsage()*100);
	/*
	printf("LIN: 0x%x,VRAM: 0x%x,REG: 0x%x\r",
				linearSpaceFree()/0x1000, vramSpaceFree()/0x1000, mappableSpaceFree()/0x1000);
				*/
}

/* returns true if we just started a new frame with this call */
static bool ensureFrameBegin()
{
	if(drawing)
		return false;
	
	//NDS3D_driverLog("Beginning a new frame\n");

	debugPrintPerfStats();

	u64 time = osGetTime();

	C3D_FrameBegin(0);

	C3D_FrameDrawOn(wideModeEnabled ? targetWide : targetLeft);
	C3D_RenderTargetClear(wideModeEnabled ? targetWide : targetLeft, C3D_CLEAR_ALL, 0, 0);
	drawing = true;

	renderStats.msWaiting = osGetTime() - time;

	renderStatsLogFrameLiveness(true);
	
	//NDS3D_driverLog("Began a new frame\n");

	return true;
}

void NDS3D_FinishUpdate()
{
	//NDS3D_driverLog("NDS3D_FinishUpdate\n");

	if(drawing)
	{
		renderStatsStartMeasure();
		//ensurePolygonsQueued();
		C3D_FrameEnd(0);
		//NDS3D_driverLog("Ended frame\n");
		
		drawing = false;
		renderStatsEndMeasure();
		renderStatsLogFrameLiveness(false);
	}
	
	//NDS3D_driverLog("NDS3D_FinishUpdate done\n");
}

static inline void setCurrentTexture(C3D_Tex *tex)
{	
	//NDS3D_driverLog("Binding texture\n");
	C3D_TexBind(0, tex);
}

static void resetTextures()
{
	prevTex = NULL;
}

static void setFog(u32 fogColor, u32 fogDensity)
{
	if (!cv_grfog.value)
	{
		if (fogEnabled)
		{
			fogEnabled = false;
			C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
		}
		
		return;
	}

	/* Convert from 0RGB to 0BGR */
	fogColor = NDS3D_Reverse32(fogColor);
	fogColor >>= 8;

	//NDS3D_driverLog("fogDensity %i, fogColor %lx\n", fogDensity, fogColor);

	if(prevFogDensity != fogDensity)
	{
		if (fogDensity > MAX_FOG_DENSITY)
		{
			//printf("fogDensity %i is too high!", fogDensity);
			fogDensity = MAX_FOG_DENSITY;
		}

		size_t lutIndex = fogDensity >> 1;
		C3D_FogLut *lut = fogLUTs[lutIndex];

		if (!lut)
		{
			lut = malloc(sizeof(C3D_FogLut));
			float d =  fogDensity*100/(1250*10000.0f*14.0f); 
			FogLut_Exp(lut, d, 0.75f, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE);	/* arbitrary */
			fogLUTs[lutIndex] = lut;
		}

		C3D_FogLutBind(lut);

		prevFogDensity = fogDensity;
	}

	if(prevFogColor != fogColor)
	{
		C3D_FogColor(fogColor);
		prevFogColor = fogColor;
	}

	fogEnabled = true;
	C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
}

void NDS3D_DrawPolygon(u32 surfColor, u32 geomIdx, FUINT iNumPts, FBITFIELD PolyFlags,
		C3D_Tex *texture, u32 fogColor, u32 fogDensity)
{
	static FBITFIELD prevPolyFlags;
	static u32 prevColor;
	u32 color;
	C3D_TexEnv* env;
	bool texChanged = texture != prevTex;

	//NDS3D_driverLog("NDS3D_DrawPolygon\n");

	//if (PolyFlags & PF_Invisible)
		//return;

	//renderStats.vertsPF += iNumPts;

	//debugTraceDrawPolygon(pSurf, pOutVerts, iNumPts, PolyFlags);

	// process fog arguments
	setFog(fogColor, fogDensity);

	if(texChanged)
	{
		setCurrentTexture(texture);
	}

	NDS3D_SetBlend(PolyFlags);

	renderStatsStartMeasure();

#ifdef DIAGNOSTIC
	if(PolyFlags & PF_Corona)
		NDS3D_driverPanic("PF_Corona\n");

	if (PolyFlags & PF_MD2)
		NDS3D_driverPanic("PF_MD2\n");
#endif

	// check if there will be a graphics settings change
	if((prevPolyFlags & (PF_NoTexture | PF_Modulated)) != 
		(PolyFlags & (PF_NoTexture | PF_Modulated)))
	{
		//ensurePolygonsQueued();
	}


	/* TexEnv#0 processing */

	if(prevPolyFlags != PolyFlags)
	{
		env = C3D_GetTexEnv(0);
	
		if(PolyFlags & PF_NoTexture)
		{
			C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		}
		else
		{
			// mix texture and "flat-shaded" vertex color
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		}
	}

	/* TexEnv#1 processing */

	if((prevPolyFlags != PolyFlags) || (surfColor != prevColor))
	{
		env = C3D_GetTexEnv(1);
		// If Modulated, mix the surface colour to the texture
		if (PolyFlags & PF_Modulated)
		{
			color = surfColor;
			prevColor = color;
			
			// modulate with constant color
			C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
			C3D_TexEnvColor(env, color);
		}
		else
		{
			//TexEnv_Init(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		}
	}

	/*
		env = C3D_GetTexEnv(2);
	
	if (fading)
	{
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_CONSTANT);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
		C3D_TexEnvColor(env, fadeColor);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	*/
	/*
	if(PolyFlags & (PF_ForceWrapX | PF_ForceWrapY))
	{
		int wrapParamX, wrapParamY;
	
		wrapParamX = (PolyFlags & PF_ForceWrapX) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		wrapParamY = (PolyFlags & PF_ForceWrapY) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		C3D_Tex *tex = getTextureByID(texCurrent);
		C3D_TexSetWrap(tex, wrapParamX, wrapParamY);
	}
	*/
	prevPolyFlags = PolyFlags;
	
	//NDS3D_driverLog("Adding 0x%lx vertices\n", iNumPts);

	//NDS3D_driverLog("bufIndex: 0x%lx\n", bufIndex);
	
	//ensureFrameBegin();
	C3D_DrawArrays(GPU_TRIANGLE_FAN, geomIdx, iNumPts);

	renderStatsEndMeasure();

	/* log current gpu cmd buf offset */
	//extern u32 gpuCmdBufOffset;
	//NDS3D_driverLog("\ngpuCmdBufOffset: 0x%x\n\n", gpuCmdBufOffset);
}


static void BlendFuncDefault(GPU_BLENDFACTOR src, GPU_BLENDFACTOR dst)
{
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, // the ADD blend equation is the default in opengl?
					src, dst, src, dst);
}


static bool curDepthTestEnabled;
static int curDepthTestFunction;
static int curDepthWriteMask;

static bool curAlphaTestEnabled;
static int curAlphaTestFunction;
static int curAlphaTestValue;

static void InitRendererMode(void)
{
	NDS3D_SetBlend(0);

	//C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

	curDepthTestEnabled = false;
	curDepthTestFunction = GPU_GREATER;
	curDepthWriteMask = GPU_WRITE_ALL;
	C3D_DepthTest(curDepthTestEnabled, curDepthTestFunction, curDepthWriteMask);

	curAlphaTestEnabled = false;
	curAlphaTestFunction = GPU_GREATER;
	curAlphaTestValue = 128;
	C3D_AlphaTest(curAlphaTestEnabled, curAlphaTestFunction, curAlphaTestValue);
}

void NDS3D_SetBlend(FBITFIELD PolyFlags)
{
	static FBITFIELD curPolyFlags;
	const FBITFIELD changedFlags = curPolyFlags ^ PolyFlags;

	//printf("NDS3D_SetBlend: 0x%x\n", PolyFlags);

	renderStatsStartMeasure();

	if(changedFlags & PF_Blending) // if blending mode must be changed
	{
		switch (PolyFlags & PF_Blending)
		{
			case PF_Translucent & PF_Blending:
				BlendFuncDefault(GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
				curAlphaTestFunction = GPU_NOTEQUAL;
				curAlphaTestValue = 0;
				break;

			case PF_Masked & PF_Blending:
				BlendFuncDefault(GPU_SRC_ALPHA, GPU_ZERO);
				curAlphaTestFunction = GPU_GREATER;
				curAlphaTestValue = 128;
				break;

			case PF_Additive & PF_Blending:
				BlendFuncDefault(GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
				curAlphaTestFunction = GPU_NOTEQUAL;
				curAlphaTestValue = 0;
				break;

			case PF_Environment & PF_Blending:
				BlendFuncDefault(GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
				curAlphaTestFunction = GPU_NOTEQUAL;
				curAlphaTestValue = 0;
				break;

			case PF_Substractive & PF_Blending:
				BlendFuncDefault(GPU_ZERO, GPU_ONE_MINUS_SRC_ALPHA);
				curAlphaTestFunction = GPU_NOTEQUAL;
				curAlphaTestValue = 0;
				break;

			case PF_Fog & PF_Fog:
				BlendFuncDefault(GPU_SRC_ALPHA, GPU_SRC_COLOR);
				curAlphaTestFunction = GPU_NOTEQUAL;
				curAlphaTestValue = 0;
				break;

			default: 
#ifdef DIAGNOSTIC
				// must be 0, otherwise it's an error
				if((PolyFlags & PF_Blending) != 0)
					NDS3D_driverPanic("Couldn't handle all possible PF_Blending values!\n");
#endif
				// No blending
				BlendFuncDefault(GPU_ONE, GPU_ZERO);
				curAlphaTestFunction = GPU_GREATER;
				curAlphaTestValue = 128;
				break;
		}

		curAlphaTestEnabled = true;
		C3D_AlphaTest(curAlphaTestEnabled, curAlphaTestFunction, curAlphaTestValue);

		// End of PF_Blending changes 
		curPolyFlags = (curPolyFlags & ~PF_Blending) | (PolyFlags & PF_Blending);
	}

	if(changedFlags & PF_NoAlphaTest)
	{
		curAlphaTestEnabled = PolyFlags & PF_NoAlphaTest; // don't just flip the state, force the exact new state.
		C3D_AlphaTest(curAlphaTestEnabled, curAlphaTestFunction, curAlphaTestValue);
		curPolyFlags = (curPolyFlags & ~PF_NoAlphaTest) | (PolyFlags & PF_NoAlphaTest);
	}
	
#ifdef DIAGNOSTIC
	if (changedFlags & PF_Decal)
	{
		NDS3D_driverPanic("PF_Decal\n");
	}

	if (changedFlags & PF_RemoveYWrap)
	{
		NDS3D_driverPanic("PF_RemoveYWrap\n");
	}
#endif

	// Disable the depth test mode ?
	if(changedFlags & PF_NoDepthTest)
	{
		curDepthTestEnabled = (PolyFlags & PF_NoDepthTest) == false;
		C3D_DepthTest(curDepthTestEnabled, curDepthTestFunction, curDepthWriteMask);
		curPolyFlags = (curPolyFlags & ~PF_NoDepthTest) | (PolyFlags & PF_NoDepthTest);
	}
	
	// Update the depth buffer ?
	if(changedFlags & PF_Occlude)
	{
		if (PolyFlags & PF_Occlude)
		{
			curDepthWriteMask = GPU_WRITE_ALL;
		}
		else
		{
			curDepthWriteMask = GPU_WRITE_COLOR;
		}

		C3D_DepthTest(true, curDepthTestFunction, curDepthWriteMask);
		curPolyFlags = (curPolyFlags & ~PF_Occlude) | (PolyFlags & PF_Occlude);
	}
	
	if (changedFlags & PF_Invisible)
	{
		if (PolyFlags & PF_Invisible)
			BlendFuncDefault(GPU_ZERO, GPU_ONE);	// transparent blending
		else
		{
			if ((PolyFlags & PF_Blending) == PF_Masked)
				BlendFuncDefault(GPU_SRC_ALPHA, GPU_ZERO);
		}
	}

	curPolyFlags = PolyFlags;

	/* We don't support these, so just pretend we did our job. */
	const FBITFIELD unsupportedMask = PF_Clip | PF_NoZClip | PF_NoTexture | PF_Modulated;
	if(changedFlags & unsupportedMask)
		curPolyFlags = (curPolyFlags & ~unsupportedMask) | (PolyFlags & unsupportedMask);

#ifdef DIAGNOSTIC
	if(curPolyFlags != PolyFlags)
		NDS3D_driverPanic("Failed to adjust current poly flags!\ncurPolyFlags 0x%x, PolyFlags 0x%x\n", curPolyFlags, PolyFlags);
#endif

	renderStatsEndMeasure();
}

void NDS3D_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, u32 ClearColor)
{
	//NDS3D_driverLog("NDS3D_ClearBuffer\n");

	C3D_ClearBits clearBits = 0;
	const u32 clearDepth = 255;	/* arbitrary */
	C3D_FrameBuf *framebuf = C3D_GetFrameBuf();

	if(!framebuf || (!ColorMask && !DepthMask))
		return;

	renderStatsStartMeasure();

	if(ColorMask)
	{
		clearBits = C3D_CLEAR_COLOR;
	}

	if(DepthMask)
	{
		clearBits |= C3D_CLEAR_DEPTH;
	}

	{
		if(clearCounter >= 256)
		{
#ifdef DIAGNOSTIC
			printf("clearCounter at max!\n");
#endif
			renderStatsEndMeasure();
			return;
		}

		C3D_FrameSplit(0);
	}

	//ensurePolygonsQueued();

	//if(DepthMask)
		//NDS3D_SetBlend(DepthMask ? PF_Occlude | curPolyFlags : curPolyFlags&~PF_Occlude);

	C3D_FrameBufClear(framebuf, clearBits, ClearColor, clearDepth);

	clearCounter++;

	renderStatsEndMeasure();
	//printf("clearcount: %i\n", clearCounter);
}

/*
void NDS3D_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip)
{
	
	C3D_Mtx projection;

    NDS3D_driverLog("NDS3D_GClipRect()\n");

    printf("NDS3D_GClipRect: minx %i, miny %i, maxx %i, maxy %i\n", minx, miny, maxx, maxy);

    if(maxy > 240)
    	NDS3D_driverPanic("Illegal maxy!\n");
    // x y w h
    C3D_SetViewport(minx, 240-maxy, (maxx-minx), maxy-miny);

    NEAR_CLIPPING_PLANE = nearclip;

    Mtx_Identity(&projection);
    Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(62.0f),
						C3D_AspectRatioTop, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
    
}
*/

void NDS3D_SetSpecialState(hwdspecialstate_t IdState, INT32 Value)
{
	
	
}

/*
static void debugSetTransform(FTransform *ptransform)
{
	printf("ptransform dump:\n");
	printf("scalex %f  y %f  z %f\n", ptransform->scalex, ptransform->scaley, ptransform->scalez);
	printf("anglex %f  y %f  fovyangle %f\n", ptransform->anglex, ptransform->angley, ptransform->fovyangle);
	printf("scalex %f  y %f  z %f\n", ptransform->x, ptransform->y, ptransform->z);
	printf("flip: %i\n", ptransform->flip);
}
*/

void NDS3D_SetTransform(FTransform *ptransform)
{
	C3D_Mtx m, p;
	C3D_Mtx *modelView;
	C3D_Mtx *projection;
	bool flip;

	//NDS3D_driverLog("NDS3D_SetTransform()\n");

	renderStatsStartMeasure();

	//ensurePolygonsQueued();
	
	/* Update the modelView and projection matrix */

	if(ptransform)
	{
		//debugSetTransform(ptransform);

#ifndef NOSPLITSCREEN
		if(ptransform->splitscreen)
			NDS3D_driverPanic("Splitscreen is unsupported!\n");
#endif

		Mtx_Identity(&m);
		
		flip = ptransform->flip;
		
		if(flip)
			Mtx_Scale(&m, ptransform->scalex * 1.6f, -ptransform->scaley, -ptransform->scalez);
		else
			Mtx_Scale(&m, ptransform->scalex * 1.6f, ptransform->scaley, ptransform->scalez);

		//printf("scalex %f  y %f  z %f\n", ptransform->scalex, ptransform->scaley, ptransform->scalez);
			
		Mtx_RotateX(&m, C3D_AngleFromDegrees(ptransform->anglex), true);	// XXX bRightSide
		Mtx_RotateY(&m, C3D_AngleFromDegrees(ptransform->angley + 270.0f), true);
		Mtx_Translate(&m, -ptransform->x, -ptransform->z, -ptransform->y, true);
		
		/*
		Mtx_PerspTilt(&p, C3D_AngleFromDegrees(ptransform->fovxangle),
						C3D_AspectRatioTop, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, true);
		*/
		
		Mtx_PerspStereoTilt(&p, C3D_AngleFromDegrees(ptransform->fovxangle),
					C3D_AspectRatioTop, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, iod, focalLength, true);

		projection = &p;
		modelView = &m;
	}
	else	/* reset transformation */
	{
		/* used in HUD and skybox */
		Mtx_Identity(&p);
		Mtx_PerspStereoTilt(&p, C3D_AngleFromDegrees(fov),
					C3D_AspectRatioTop, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, -iod, 1.0f, true);
		projection = &p;
		modelView = &defaultModelView;
	}
	
	// Update the uniforms
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, projection);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);

	renderStatsEndMeasure();
}

void setFading(u32 color)
{
	if (color >= 10 && color <= 0xffff0000)
	{
		fading = true;
		fadeColor = color;
	}
	else fading = false;
}

/* Returns true if we actually have drawn something */
int processRenderQueue(void)
{
	queuePacket *packet;
	bool finishFrame = false;
	bool drawn = false;
	int retval = 0;
	int packetType;

	clearCounter = 0;	// needed by NDS3D_ClearBuffer

	for(;;)
	{
		packet = queueDequeuePacket();
		packetType = packet->type;

		//N3DS_Print("received packet %i\n", packetType);

		switch(packetType)
		{
			case CMD_TYPE_FINISH:
			{
				finishFrame = true;
				if (drawn)
					retval = 1;
				break;
			}

			case CMD_TYPE_BLEND:
			{
				NDS3D_SetBlend(packet->args.argsBlend.PolyFlags);
				break;
			}

			case CMD_TYPE_CLEAR:
			{
				NDS3D_ClearBuffer(
									packet->args.argsClear.ColorMask,
									packet->args.argsClear.DepthMask,
									packet->args.argsClear.ClearColor
								 );
				//drawn = true;
				break;
			}

			case CMD_TYPE_TRANSFORM:
			{
				FTransform *transform = &packet->args.argsTransform.transform;
				NDS3D_SetTransform(transform);
				break;
			}

			case CMD_TYPE_TRANSFRST:
			{
				NDS3D_SetTransform(NULL);
				break;
			}

			/*
			case CMD_TYPE_SPECIAL:
			{
				hwdspecialstate_t IdState = packet->args.argsSpecial.IdState;
				INT32 Value = packet->args.argsSpecial.Value;
				NDS3D_SetSpecialState(IdState, Value);
				break;
			}
			*/

			case CMD_TYPE_FADE:
			{
				//setFading(packet->args.argsFade.fadeColor);
				break;
			}

			case CMD_TYPE_DRAW:
			{
				u32			surfColor = packet->args.argsDraw.surfColor;
				u32			geometryIdx = packet->args.argsDraw.geometryIdx;
				u32			geometryNum = packet->args.argsDraw.geometryNum;
				FBITFIELD 	PolyFlags = packet->args.argsDraw.PolyFlags;
				C3D_Tex *	tex = packet->args.argsDraw.tex;
				u32			fogColor = packet->args.argsDraw.fogColor;
				u32			fogDensity = packet->args.argsDraw.fogDensity;

				NDS3D_DrawPolygon(surfColor, geometryIdx, geometryNum,
					PolyFlags, tex, fogColor, fogDensity);
				drawn = true;
				break;
			}

			case CMD_TYPE_DUMMY:
			{
				break;
			}
			
			case CMD_TYPE_FLUSH:
			{
				resetTextures();

				/* Make sure to finish what we started */
				if (drawn)
				{
					finishFrame = true;
					retval = 2;
				}

				break;
			}

			case CMD_TYPE_EXIT:
			{
				NDS3D_Shutdown();
				threadExit(0);
			}

			default:
				N3DS_Print("Unknown packet type %i\n", packetType);
		}

		if (finishFrame)
			break;

		//printf("next\n");

		while(!queuePollForDequeue());
			//printf("next\n");
	}

	return retval;
}

void NDS3D_Thread(void)
{
	N3DS_Print("NDS3D_Thread started\n");

	NDS3D_Init();

	LightEvent_Signal(&workerInitialized);

	int status;

	for(;;)
	{

		ensureFrameBegin();

		/* We are now in a frame ... */
		bool createdCp = false;
		bool enable3d = get3DSettings(&iod, &focalLength);

		setScreenMode(enable3d);

next:
		/* Wait for incoming packets */
		if (!queuePollForDequeue())
			queueWaitForEvent(QUEUE_EVENT_NOT_EMPTY, U64_MAX);

		if (!createdCp && enable3d)
		{
			queueCreateCheckPoint();
			createdCp = true;
		}

		if ((status = processRenderQueue()) == 0)
		{
			/* We haven't drawn anything */
			goto next;
		}

		/* If 3D is enabled, process queue again for the second target */
		if (createdCp)
		{
			queueRestoreCheckPoint();
			C3D_FrameDrawOn(targetRight);
			C3D_RenderTargetClear(targetRight, C3D_CLEAR_ALL, 0, 0);
			iod = -iod;

next2:
			if ((status = processRenderQueue()) == 0)
			{
				/* We haven't drawn anything */
				goto next2;
			}
		}

		NDS3D_FinishUpdate();

		if (status == 1)
			queueNotifyFrameProgress();
	}
	
}
