#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <3ds.h>
#include <citro3d.h>

#define __BYTEBOOL__
#define boolean bool

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"
#include "r_texcache.h"
#include "r_nds3d.h"
#include "nds_utils.h"

#define TEX_AGING

#define NOTEXTURE_NUM     0     // small white texture
#define FIRST_TEX_AVAIL   (NOTEXTURE_NUM + 1)
#define MAX_SRB2_TEXTURES      2048

// Texture state
static TextureInfo textureCache[MAX_SRB2_TEXTURES];
static size_t nextCacheIndex;
static TextureInfo *cachedTextures[MAX_SRB2_TEXTURES];
static size_t nextFreeIndex;
static TextureInfo *freeTextures[MAX_SRB2_TEXTURES];

void texCacheInit()
{
	for (size_t i=0; i<MAX_SRB2_TEXTURES; i++)
	{
		TextureInfo *entry = &textureCache[i];
		freeTextures[i] = entry;
		nextFreeIndex++;
	}
}

static TextureInfo *getFreeEntry()
{
	if (nextFreeIndex == 0)
		return NULL;

	return freeTextures[--nextFreeIndex];
}

static void addEntry(TextureInfo *entry)
{
	entry->unusedCount = 0;
	entry->ftexinfo->downloaded = (uint32_t) entry;
	cachedTextures[nextCacheIndex++] = entry;
}

static void freeEntry(TextureInfo *entry)
{
	C3D_TexDelete(&entry->c3dtex);
	entry->ftexinfo->downloaded = 0;
	entry->ftexinfo = NULL;
	freeTextures[nextFreeIndex++] = entry;
}

C3D_Tex *texCacheGetC3DTex(TextureInfo *info)
{
	info->wasUsed = 1;

	return &info->c3dtex;
}

TextureInfo *texCacheAdd(FTextureInfo *texInfo)
{
	TextureInfo *entry;
	C3D_Tex *c3dTex;

	entry = getFreeEntry();
	assert(entry);

	entry->ftexinfo = texInfo;
	addEntry(entry);

	return entry;
}

void texCacheFlush(unsigned purgeLevel, TextureInfo **curTexInfo)
{
	unsigned unusedThreshold;
	bool force;
	size_t i;
	size_t updateIdx;

	switch (purgeLevel)
	{
		case 0:		unusedThreshold = 10;
					force = false;
					break;
		case 1:		unusedThreshold = 3;
					force = false;
					break;
		default:	unusedThreshold = 0;
					force = true;
					break;
	}
	
	if (!force)
	{
		updateIdx = 0;

		for (i=0; i<nextCacheIndex; i++)
		{
			TextureInfo *entry = cachedTextures[i];
			if (entry->unusedCount >= unusedThreshold)
			{
				//printf("Freeing entry at 0x%x\n", i);
				freeEntry(entry);
			}
			else
			{
				if (entry->wasUsed == 0)
					entry->unusedCount++;
				else entry->unusedCount = 0;

				entry->wasUsed = 0;

				//if (i != updateIdx)
				{
					cachedTextures[updateIdx] = cachedTextures[i];
					updateIdx++;
				}
			}
		}

		nextCacheIndex = updateIdx;
	}
	else
	{
		for (i=0; i<nextCacheIndex; i++)
		{
			TextureInfo *entry = cachedTextures[i];
			freeEntry(entry);
		}

		*curTexInfo = NULL;
		nextCacheIndex = 0;
	}
}

size_t texCacheGetNumCached()
{
	return nextCacheIndex;
}

INT32 getTextureMemUsed(void)
{
	INT32 res = 0;
	
	for(size_t i=0; i<nextCacheIndex; i++)
	{
		FTextureInfo *texInfo = cachedTextures[i]->ftexinfo;
		res += texInfo->height * texInfo->width * 4;
		
		if(res < 0)
			NDS3D_driverPanic("Illegal texture cache state!\n");
	}
	
	//NDS3D_driverLog("NDS3D_GetTextureUsed: 0x%lx\n", res);
	
	return res;
}
