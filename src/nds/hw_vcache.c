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
