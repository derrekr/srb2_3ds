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
/// \brief SRB2 graphics stuff for NDS

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>
#include <citro3d.h>

#define __BYTEBOOL__
#define boolean bool

#include "../doomdef.h"
#include "../command.h"
#include "../i_video.h"

#include "../hardware/hw_drv.h"
#include "../hardware/hw_main.h"
#include "../hardware/hw_vcache.h"
#include "r_nds3d.h"
#include "r_queue.h"
#include "r_texcache.h"
#include "nds_utils.h"

#define GPU_CMDBUF_SIZE		(1024 * 1024 * 4)

#define PALETTE_SIZE		256
#define PALETTE_CACHE_SIZE	2

#define MAX_TEX_SIZE			(1 * 1024 * 1024)

#define RGB8A_TO_UINT32(r,g,b,a)	((u32)(((u8)r<<24) | ((u8)g<<16) | ((u8)b<<8) | ((u8)a)))

rendermode_t rendermode = render_opengl;
boolean highcolor = false;
boolean allow_fullscreen = false;
consvar_t cv_vidwait = {"vid_wait", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};
extern consvar_t cv_ticrate;

// -------------------------------------------------------

static Thread workerThread;

static u32 myPaletteData[256];
static TextureInfo *texCurrent;

static bool frameEnded;

// fog state
static u32 fogColor;
static u32 fogDensity;
static bool fogEnabled;


static bool queueWaitEmptyTimeout()
{
	const u64 nanoseconds = 1000LL * 1000LL * 50LL;	// 50 ms

	return queueWaitForEvent(QUEUE_EVENT_EMPTY, nanoseconds);
}

static void stallAndFlushTextures()
{
	queueWaitEmptyTimeout();

	queuePacket *packet = queueAllocPacket();
	packet->type = CMD_TYPE_FLUSH;
	queueEnqueuePacket(packet);

	queueWaitEmptyTimeout();

	packet = queueAllocPacket();
	packet->type = CMD_TYPE_DUMMY;
	queueEnqueuePacket(packet);

	queueWaitEmptyTimeout();

	texCacheFlush(2, &texCurrent);
}

static void SetNoTexture()
{
	texCurrent = NULL;
}

void NDS3DVIDEO_SetPalette(RGBA_t *ppal, RGBA_t *pgamma)
{
	u32 *curPalette = myPaletteData;

	for(size_t i=0; i<PALETTE_SIZE; i++)
	{
		curPalette[i] = ppal[i].rgba;
	}
	
	/*
	NDS3D_driverLog("%x %x %x\n%x %x %x\n", ppal[0].s.blue, ppal[0].s.red, ppal[0].s.green,
		pgamma->s.blue, pgamma->s.red, pgamma->s.green);
	*/
}

#ifdef N3DS_PERF_MEASURE
static TextureInfo *prevTex;
static u32 numTexChanges;
#endif

void NDS3DVIDEO_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags)
{
	/* Caused by SetNoTexture() */
	if (texCurrent == NULL)
		return;

	if (frameEnded)
	{
		queueWaitForEvent(QUEUE_EVENT_ALMOST_EMPTY, U64_MAX);
		frameEnded = false;
	}
#ifdef N3DS_PERF_MEASURE
	if (prevTex != texCurrent)
	{
		numTexChanges++;
		prevTex = texCurrent;
	}
#endif

#ifdef DIAGNOSTIC
	if ((size_t)pOutVerts < (size_t)geometryBuf ||
		(size_t) pOutVerts >= (size_t)geometryBuf + (VCACHE_NUM_BUFFERS * MAX_NUM_VECTORS) * sizeof(FOutVector))
		NDS3D_driverPanic("Invalid geometry ptr passed to NDS3DVIDEO_DrawPolygon\n%p vs %p\n", pSurf, geometryBuf);
#endif

	size_t bufIndex = ((size_t)pOutVerts - (size_t)geometryBuf)/sizeof(*pOutVerts);

	queuePacket *packet = queueAllocPacket();
	packet->type = CMD_TYPE_DRAW;
	packet->args.argsDraw.surfColor = pSurf ? pSurf->FlatColor.rgba : 0xFFFFFFFF;
	packet->args.argsDraw.geometryIdx = bufIndex;
	packet->args.argsDraw.geometryNum = iNumPts;
	packet->args.argsDraw.PolyFlags = PolyFlags;
	packet->args.argsDraw.tex = texCacheGetC3DTex(texCurrent);
	if (fogEnabled)
	{
		packet->args.argsDraw.fogColor = fogColor;
		packet->args.argsDraw.fogDensity = fogDensity;
	}
	else packet->args.argsDraw.fogDensity = 0;

	queueEnqueuePacket(packet);
}

void NDS3DVIDEO_SetBlend(FBITFIELD PolyFlags)
{
	queuePacket *packet = queueAllocPacket();
	packet->type = CMD_TYPE_BLEND;
	packet->args.argsBlend.PolyFlags = PolyFlags;
	queueEnqueuePacket(packet);
}

void NDS3DVIDEO_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor)
{
	u32 clearColor;

	if (ColorMask && ClearColor)
	{
		clearColor = 	RGB8A_TO_UINT32((u8)(ClearColor->red * 255.0f),
										(u8)(ClearColor->green * 255.0f),
										(u8)(ClearColor->blue * 255.0f),
										(u8)(ClearColor->alpha * 255.0f));
	}
	else clearColor = 0xFFFFFFFF;

	queuePacket *packet = queueAllocPacket();
	packet->type = CMD_TYPE_CLEAR;
	packet->args.argsClear.ColorMask = ColorMask;
	packet->args.argsClear.DepthMask = DepthMask;
	packet->args.argsClear.ClearColor = clearColor;
	queueEnqueuePacket(packet);
}

void NDS3DVIDEO_ClearMipMapCache(void)
{
	stallAndFlushTextures();
}

void NDS3DVIDEO_SetSpecialState(hwdspecialstate_t IdState, INT32 Value)
{
	switch (IdState)
	{
		case HWD_SET_FOG_COLOR:
		{
			u32 color = (u32) Value;
			fogColor = color;
			break;
		}
		
		case HWD_SET_FOG_DENSITY:
		{
			u8 density = (u8) Value;

			fogDensity = density;
			break;
		}

		case HWD_SET_FOG_MODE:
		{
			bool enabled = Value > 0 ? true : false;
			fogEnabled = enabled;
			break;
		}

		default:
			return;
	}
}

void NDS3DVIDEO_SetTransform(FTransform *ptransform)
{
	static FTransform *prevTransf;

	if (prevTransf == ptransform)
		return;
	prevTransf = ptransform;

	queuePacket *packet = queueAllocPacket();

	if (ptransform)
	{
		packet->type = CMD_TYPE_TRANSFORM;
		packet->args.argsTransform.transform = *ptransform;
	}
	else 
	{
		packet->type = CMD_TYPE_TRANSFRST;
	}
	
	queueEnqueuePacket(packet);
}

void NDS3DVIDEO_FinishUpdate(INT32 waitvbl)
{
	queuePacket *packet = queueAllocPacket();
	packet->type = CMD_TYPE_FINISH;
	/* no args needed */
	queueEnqueuePacket(packet);
}

void NDS3DVIDEO_Stub(void)
{

}

void workerThreadEntry(void *arg)
{
	s32 curCPU = svcGetProcessorID();
	printf("workerThreadEntry cpuID: %i\n", curCPU);

	s32 actualPrio = 0;
	svcGetThreadPriority(&actualPrio, CUR_THREAD_HANDLE);
	printf("worker thread prio: %i\n", actualPrio);

	extern void NDS3D_Thread(void);
	NDS3D_Thread();
}

bool spawnWorkerThread(void)
{
	const size_t stackSize = 64 * 1024;
	const int cpuID = 2;
	const s32 bestPrio = 0x18;
	s32 actualPrio = 0;

	s32 curCPU = svcGetProcessorID();
	printf("main thread cpuID: %i\n", curCPU);


	/* Create worker */
	// NOTE: Would actually work with a bad prio as well...
	for (s32 prio = 0x22; ; prio++)
	{
		workerThread = threadCreate(workerThreadEntry, NULL, stackSize, prio, cpuID, true);
		if (workerThread != NULL)
		{
			break;
		}
	}

	svcSleepThread(1000u*1000u);


	/* Opt main thead */

	svcGetThreadPriority(&actualPrio, CUR_THREAD_HANDLE);

	for (s32 prio = actualPrio - 1; prio >= bestPrio+3; prio--)
	{
		if (svcSetThreadPriority(CUR_THREAD_HANDLE, prio) < 0)
			break;
	}

	svcGetThreadPriority(&actualPrio, CUR_THREAD_HANDLE);
	printf("set main thread prio: %i\n", actualPrio);

	return true;
}

boolean NDS3DVIDEO_Init(I_Error_t ErrorFunction)
{	
	NDS3D_driverLog("NDS3D_Init\n");

	NDS3D_Init(ErrorFunction);

	texCacheInit();

	// Init render queue
	queueInit();

	if (!spawnWorkerThread())
		return false;

	return true;
}

void NDS3DVIDEO_Shutdown(void)
{
	printf("Attempting video shutdown\n");

	queueWaitEmptyTimeout();

	queuePacket *packet = queueAllocPacket();
	packet = queueAllocPacket();
	packet->type = CMD_TYPE_DUMMY;
	queueEnqueuePacket(packet);

	queueWaitEmptyTimeout();

	packet = queueAllocPacket();
	packet->type = CMD_TYPE_EXIT;
	queueEnqueuePacket(packet);

	queueWaitEmptyTimeout();

	packet = queueAllocPacket();
	packet->type = CMD_TYPE_DUMMY;
	queueEnqueuePacket(packet);

	queueWaitEmptyTimeout();

	printf("OK, bye!\n");

	gfxExit();
}

static inline size_t calcTexScaleFactor(size_t width, size_t height)
{
	size_t smallest = min(width, height);
	size_t scaleFactor = 1;
	
	if(smallest < 8)
	{
		switch(smallest)
		{
			case 1:
				scaleFactor = 8; break;
			case 2:
				scaleFactor = 4; break;
			case 4:
				scaleFactor = 2; break;
			/* Weird cases we don't support */
			case 3:
			case 5:
			case 6:
			case 7:
			default:
				NDS3D_driverPanic("calcTexScaleFactor: Unsupported tex dim!\n");
		}
	}
	
	return scaleFactor;
}

static void convertToGpuTexture(u32 *bufIn, size_t width, size_t height, u32 *bufOut,
								GPU_TEXCOLOR gpuFormat, size_t pixelSize)
{
	// stolen from smdhtool (which stole it from 3ds_hb_menu)
	static const u8 tileOrder[] =
	{
		0, 1, 8, 9, 2, 3, 10, 11, 16, 17, 24, 25, 18, 19, 26, 27,
		4, 5, 12, 13, 6, 7, 14, 15, 20, 21, 28, 29, 22, 23, 30, 31,
		32, 33, 40, 41, 34, 35, 42, 43, 48, 49, 56, 57, 50, 51, 58, 59,
		36, 37, 44, 45, 38, 39, 46, 47, 52, 53, 60, 61, 54, 55, 62, 63
	};

	u32 scaleFactor;
	size_t totalSize;
	u8 *input;
	u8 *output;
	static u32 scaledTexBuf[8 * 1024];
	u8 *scaledTexBufp8 = (u8 *)scaledTexBuf;

#ifdef DIAGNOSTIC
	if((size_t)bufIn % 4 || (size_t)bufOut % 0x80)
		NDS3D_driverPanic("Bad buffer alignment!\n");
#endif

	input = (u8 *)bufIn;
	output = (u8 *)bufOut;

	/* 
	 * The 3DS' GPU doesn't support textures smaller
	 * than 8x8 pixels.
	 * Check if we have to enlarge the texture.
	 */
	
	scaleFactor = calcTexScaleFactor(width, height);
	
	totalSize = width * scaleFactor * height * scaleFactor * pixelSize;
	
	if(scaleFactor != 1)
	{
		const u32 outWidth = scaleFactor * width;

#ifdef DIAGNOSTIC
		if(totalSize > sizeof scaledTexBuf)
			NDS3D_driverPanic("Local scaledTexBuf is too small!\n");
#endif

		// printf("Scaling texture %i %i %i\n", width, height, (int) scaleFactor);
	
		switch(gpuFormat)
		{
			case GPU_RGBA8:
				for(u32 x = 0; x < width; x++)
				{
					for(u32 y = 0; y < height; y++)
					{
						u32 val = bufIn[y * width + x];
					
						for(u32 scaleX = 0; scaleX < scaleFactor; scaleX++)
						{
							for(u32 scaleY = 0; scaleY < scaleFactor; scaleY++)
							{
								scaledTexBuf[outWidth * (y*scaleFactor + scaleY) + x * scaleFactor + scaleX] = val;
							}
						}
					}
				}
				break;
			
			default:
				NDS3D_driverPanic("Cannot scale texture (gpuFormat: %i)!\n", gpuFormat);
		}
	
		width *= scaleFactor;
		height *= scaleFactor;
		
		input = scaledTexBufp8;
	}
	
	/* Do conversion in SW */
	{
		size_t outputOffset = 0;
	
		switch(gpuFormat)
		{
			case GPU_L8:
			case GPU_A8:
			{
				for(u32 tY = 0; tY < height / 8; tY++)
				{
					for(u32 tX = 0; tX < width / 8; tX++)
					{
						for(u32 pixel = 0; pixel < sizeof(tileOrder); pixel++)
						{
							u32 x = tileOrder[pixel] % 8;
							u32 y = (tileOrder[pixel] - x) / 8;
							size_t inputOffset = ( (tX*8+x)  +  ((width*height) - (tY*8+y+1)*width) ) * 4;
							output[outputOffset++] = input[inputOffset];
						}
					}
				}
			}
			break;
			
			case GPU_RGBA8:
			{
				for(u32 tY = 0; tY < height / 8; tY++)
				{
					for(u32 tX = 0; tX < width / 8; tX++)
					{
						for(u32 pixel = 0; pixel < sizeof(tileOrder); pixel++)
						{
							u32 x = tileOrder[pixel] % 8;
							u32 y = (tileOrder[pixel] - x) / 8;
							size_t inputOffset = ( (tX*8+x)  +  ((width*height) - (tY*8+y+1)*width) ) * 4;
							u32 *ptr = (u32 *)(&input[inputOffset]);
							u32 val = *ptr;
							bufOut[outputOffset++] = NDS3D_Reverse32(val);
						}
					}
				}
			}
			break;
			
			default:
				NDS3D_driverPanic("Cannot convert texture (gpuFormat: %i)!\n", gpuFormat);
		}
	}
}

static void generateTexFromPalette(u32 *bufIn, size_t width, size_t height,
								GrTextureFormat_t format, RGBA_t *bufOut)
{
	u8 *imgData = (u8 *) bufIn;

	if(format == GR_TEXFMT_P_8)
	{
		for(size_t i=0; i<width*height; i++)
		{
			bufOut->rgba = myPaletteData[*imgData++];
			bufOut++;
		}
	}
	// texFormat == GR_TEXFMT_AP_88
	else
	{
#ifdef DIAGNOSTIC
		if(format != GR_TEXFMT_AP_88)
			NDS3D_driverPanic("Bad palette texture format!\n");
#endif
		for(size_t i=0; i<width*height; i++)
		{
			bufOut->rgba = myPaletteData[*imgData++];
			bufOut->s.alpha = *imgData++;
			bufOut++;
		}
	}
}

void NDS3DVIDEO_SetTexture(FTextureInfo *TexInfo)
{
	static RGBA_t localTexBuf[MAX_TEX_SIZE];
	UINT16 height;
	UINT16 width;
	UINT8 scale;
	GrTextureFormat_t texFormat;
	GPU_TEXCOLOR gpuFormat;
	UINT8 texPixelSize;
	void *texData;
	C3D_Tex *tex;
	int texFlags;
	
	//NDS3D_driverLog("NDS3DVIDEO_SetTexture\n");
	
	if(!TexInfo)
	{
		SetNoTexture();
		return;
	}

	/* check if we know this texture already */
	if(TexInfo->downloaded)
	{
		/* check if we need to swap textures */
		if((void *)TexInfo->downloaded != texCurrent)
		{
			texCurrent = (void *)TexInfo->downloaded;
		}
		
		//NDS3D_driverLog("Activated downloaded texture\n");			
		return;
	}

	/* Currently, we don't have this texture in our cache. */
	
	texData = TexInfo->grInfo.data;
	
#ifdef DIAGNOSTIC
	if(!texData)
	{
		NDS3D_driverPanic("Texture data == NULL!\n");
	}
#endif

	/* ... process a new texture ... */
	
	height = TexInfo->height;
	width = TexInfo->width;

	//NDS3D_driverLog("Texture dimensions: %i x %i\n", width, height);

#ifdef DIAGNOSTIC
	if(width * height > MAX_TEX_SIZE || width > 1024 || height > 1024)
	{
		NDS3D_driverPanic("Texture (0x%lX) is too large!\n", width * height);
	}
#endif

	// this returns != 1 for very small textures
	scale = calcTexScaleFactor(width, height);
	
	texFormat = TexInfo->grInfo.format;
	
	switch(texFormat)
	{
		case GR_TEXFMT_ALPHA_8:
			texPixelSize = 1;
			gpuFormat = GPU_A8; break;
		case GR_TEXFMT_INTENSITY_8:
			texPixelSize = 1;
			gpuFormat = GPU_L8; break;
		case GR_TEXFMT_ALPHA_INTENSITY_44:
			texPixelSize = 1;
			gpuFormat = GPU_LA4; break;
		case GR_TEXFMT_P_8:
			texPixelSize = 4;
			gpuFormat = GPU_RGBA8; break; // ???
		case GR_TEXFMT_RGB_565:
			texPixelSize = 2;
			gpuFormat = GPU_RGB565; break;
		case GR_TEXFMT_ARGB_1555:
			texPixelSize = 2;
			gpuFormat = GPU_RGBA5551; break;
		case GR_TEXFMT_ARGB_4444:
			texPixelSize = 2;
			gpuFormat = GPU_RGBA4; break;
		case GR_TEXFMT_ALPHA_INTENSITY_88:
			texPixelSize = 2;
			gpuFormat = GPU_LA8; break;
		case GR_TEXFMT_AP_88:
			texPixelSize = 4;
			gpuFormat = GPU_RGBA8; break; // ???
		case GR_RGBA:
			texPixelSize = 4;
			gpuFormat = GPU_RGBA8; break;
		
		default:
			NDS3D_driverPanic("Unknown texture format!\n");
			texPixelSize = 0;
			gpuFormat = 0;
	}
	
	//NDS3D_driverLog("Using texFormat %i\n", texFormat);
	
	texCurrent = texCacheAdd(TexInfo);
	tex = texCacheGetC3DTex(texCurrent);


	unsigned attempts = 0;

retry:

	if(!C3D_TexInit(tex, width * scale, height * scale, gpuFormat))
	{
		/* Ah crap, we are low on memory... */
		if (attempts <= 0)
		{
			texCacheFlush(1, &texCurrent);
		}
		else if (attempts <= 1)
		{
			texCacheFlush(2, &texCurrent);
		}
		else if (attempts <= 2)
		{
			S_SafeClearSfx();
		}
		else if (attempts <= 3)
		{
			I_StopSong();
			I_UnloadSong();
		}
		else
		{
			NDS3D_driverHeapStatus();
			NDS3D_driverPanic("Failed init tex struct! (heap exhausted?)\n");
		}

		attempts++;
		goto retry;
	}
	
	//NDS3D_driverHeapStatus();
	
	texFlags = TexInfo->flags;
	
	/* check for texture wrapping flags */
	if(texFlags & TF_WRAPXY)
	{
		int wrapParamX, wrapParamY;
	
		wrapParamX = (texFlags & TF_WRAPX) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		wrapParamY = (texFlags & TF_WRAPY) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
		
		C3D_TexSetWrap(tex, wrapParamX, wrapParamY);
	}
	
	if(texFormat == GR_TEXFMT_P_8 || texFormat == GR_TEXFMT_AP_88)
	{
		generateTexFromPalette(texData, width, height, texFormat, localTexBuf);
		texData = localTexBuf;
	}
	
	convertToGpuTexture(texData, width, height, tex->data, gpuFormat, texPixelSize);

	//C3D_TexSetFilter(tex, GPU_LINEAR, GPU_NEAREST);
	
	//NDS3D_driverMemDump(tex->data, width * height * texPixelSize);

	//printf("Downloaded new texture ID %i\n", texCurrent);
	
	//NDS3D_driverLog("New texture downloaded\n");
}

INT32 NDS3DVIDEO_GetTextureUsed(void)
{
	return getTextureMemUsed();
}

INT32 NDS3DVIDEO_GetRenderVersion(void)
{
	return 0;
}

void NDS3DVIDEO_FlushScreenTextures(void)
{
	stallAndFlushTextures();
}

void I_StartupGraphics(void)
{
	vid.width = 400;
	vid.height = 240;
	vid.bpp = 4;
	vid.rowbytes = vid.width * vid.bpp;
	vid.recalc = false;

	HWD.pfnInit             = NDS3DVIDEO_Init;
	HWD.pfnShutdown         = NDS3DVIDEO_Shutdown;
	HWD.pfnFinishUpdate     = NDS3DVIDEO_FinishUpdate;
	HWD.pfnDraw2DLine       = NDS3DVIDEO_Stub;
	HWD.pfnDrawPolygon      = NDS3DVIDEO_DrawPolygon;
	HWD.pfnSetBlend         = NDS3DVIDEO_SetBlend;
	HWD.pfnClearBuffer      = NDS3DVIDEO_ClearBuffer;
	HWD.pfnSetTexture       = NDS3DVIDEO_SetTexture;
	HWD.pfnReadRect         = NDS3DVIDEO_Stub;
	HWD.pfnGClipRect        = NDS3DVIDEO_Stub;
	HWD.pfnClearMipMapCache = NDS3DVIDEO_ClearMipMapCache;
	HWD.pfnSetSpecialState  = NDS3DVIDEO_SetSpecialState;
	HWD.pfnSetPalette       = NDS3DVIDEO_SetPalette;
	HWD.pfnGetTextureUsed   = NDS3DVIDEO_GetTextureUsed;
	HWD.pfnDrawMD2          = NDS3DVIDEO_Stub;
	HWD.pfnDrawMD2i         = NDS3DVIDEO_Stub;
	HWD.pfnSetTransform     = NDS3DVIDEO_SetTransform;
	HWD.pfnGetRenderVersion = NDS3DVIDEO_GetRenderVersion;
	HWD.pfnFlushScreenTextures                       = NDS3DVIDEO_FlushScreenTextures;
	HWD.pfnStartScreenWipe                           = NDS3DVIDEO_Stub;
	HWD.pfnEndScreenWipe                             = NDS3DVIDEO_Stub;
	HWD.pfnDoScreenWipe                              = NDS3DVIDEO_Stub;
	HWD.pfnDrawIntermissionBG                        = NDS3DVIDEO_Stub;
	HWD.pfnMakeScreenTexture                         = NDS3DVIDEO_Stub;
	HWD.pfnMakeScreenFinalTexture                    = NDS3DVIDEO_Stub;
	HWD.pfnDrawScreenFinalTexture                    = NDS3DVIDEO_Stub;
	

	CV_RegisterVar(&cv_vidwait);

	C3D_Init(GPU_CMDBUF_SIZE);

	HWD.pfnInit(I_Error);

	osSetSpeedupEnable(true);

	HWR_Startup();
}

void I_ShutdownGraphics(void){}

void I_SetPalette(RGBA_t *palette)
{
	(void)palette;
}

INT32 VID_NumModes(void)
{
	return 0;
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void){}

INT32 VID_SetMode(INT32 modenum)
{
	(void)modenum;
	return 0;
}

const char *VID_GetModeName(INT32 modenum)
{
	(void)modenum;
	return NULL;
}

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void)
{
	static unsigned int frameCounter;

	frameEnded = true;

	if (cv_ticrate.value)
        SCR_DisplayTicRate();

    //printf("\x1b[29;1Hgpu: %5.2f%%  cpu: %5.2f%%  buf:%5.2f%%\n", C3D_GetDrawingTime()*6, C3D_GetProcessingTime()*6, C3D_GetCmdBufUsage()*100);
	
	/*
	printf("L:0x%x,V:0x%x,Q:%f%,T:%i\r",
				linearSpaceFree()/0x1000,
				vramSpaceFree()/0x1000,
				queueGetUsage()*100.0f,
				texCacheGetNumCached());
	*/

	HWD.pfnFinishUpdate(true);

	HWR_SwapVertexBuffer();

	/* Update texture age after every second passed */
	if (frameCounter == TICRATE)
	{
		texCacheFlush(0, &texCurrent);
		frameCounter = 0;
	}
	else frameCounter++;

	//svcSleepThread(1000LL*1000LL*200);


#ifdef N3DS_PERF_MEASURE

extern void NDS3D_ResetRenderStatsMeasureBegin(int part);
extern void NDS3D_ResetRenderStatsMeasureEnd(int part);

	n3dsRenderStats *stats = NDS3D_GetRenderStats();
	static updatectr;
	if(updatectr < 5)
		updatectr++;
	else
	{
		updatectr=0;
		/*
		printf("\x1b[29;1Hv: %5i t: %4i ms: %4lli, %4lli\ns2f: %5i f2s: %5i\n", stats->vertsPF, stats->texChangesPF,
			stats->msPF,stats->msWaiting, stats->msStart2Finish, stats->msFinish2Start);
		*/
		/*
		printf("\x1b[29;1Ha: %4lli, w: %4lli, s: %4lli, s: %4lli\n",
			stats->parts[0], stats->msWaiting,stats->parts[10], stats->parts[11]);
		*/

		printf("\x1b[29;1Hms: %4lli, %4lli, %4lli, %6lli  \n", stats->msPF,stats->msWaiting, stats->parts[0x13], stats->msFinish2Start);
	}

	NDS3D_ResetRenderStats();

	numTexChanges = 0;

#endif

}

void I_UpdateNoVsync(void) {}

void I_WaitVBL(INT32 count)
{
	(void)count;
}

void I_ReadScreen(UINT8 *scr)
{
	(void)scr;
}

void I_BeginRead(void){}

void I_EndRead(void){}
