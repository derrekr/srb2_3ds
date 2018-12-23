
#define MAX_NUM_VECTORS			(64 * 1024)
#define VCACHE_NUM_BUFFERS		4

extern FOutVector *	geometryBuf;
extern size_t		geometryBufIndex;
extern size_t		geometryBufSlot;

inline FOutVector *HWR_AllocVertexBuffer(size_t numVectors)
{
	const size_t remaining = MAX_NUM_VECTORS - (geometryBufIndex - geometryBufSlot * MAX_NUM_VECTORS);
	const size_t bufIndex = geometryBufIndex;
	FOutVector *vectors;

#ifdef DIAGNOSTIC
	if(remaining < numVectors)
	{
		I_Error("geobuf too small!\n");
		return NULL;
	}
#endif

	vectors = &geometryBuf[bufIndex];
	
	geometryBufIndex += numVectors;

	return vectors;
}