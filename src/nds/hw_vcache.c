#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __BYTEBOOL__
#define boolean bool

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"
#include "../hardware/hw_vcache.h"

// Defined in r_nds3d.c. Without this declaration the call below was treated
// as returning the C89 implicit `int`, which LTO (cross-TU codegen) then
// mismatches against the real `void *` return — corrupts geometryBuf on
// 3DS and shows up as a crash/hang at first frame.
extern void *I_InitVertexBuffer(const size_t geoBufSize);


// Dynamic Geometry Buffer
FOutVector *	geometryBuf;
size_t			geometryBufIndex;
size_t			geometryBufSlot;

void HWR_InitVertexBuffer()
{
	const size_t geoBufSize = VCACHE_NUM_BUFFERS * MAX_NUM_VECTORS * sizeof(FOutVector);

	geometryBuf = I_InitVertexBuffer(geoBufSize);
	geometryBufIndex = 0;
	geometryBufSlot = 0;
}

void HWR_SwapVertexBuffer()
{
	geometryBufSlot = (geometryBufSlot + 1) % VCACHE_NUM_BUFFERS;
	geometryBufIndex = geometryBufSlot * MAX_NUM_VECTORS;
}
