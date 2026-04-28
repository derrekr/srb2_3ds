
// Copyright (C) 2018 by derrek

#pragma once

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"

typedef struct {
	C3D_Tex c3dtex;
	FTextureInfo *ftexinfo;
	u16 unusedCount;
	u8 wasUsed;
} TextureInfo;

void texCacheInit();
C3D_Tex *texCacheGetC3DTex(TextureInfo *info);
TextureInfo *texCacheAdd(FTextureInfo *texInfo);
void texCacheFlush(unsigned purgeLevel, TextureInfo **curTexInfo);
size_t texCacheGetNumCached();
INT32 getTextureMemUsed(void);
