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
#include "r_queue.h"
#include "nds_utils.h"

typedef struct {
	queuePacket *mem;
	size_t num;
	size_t offw;
	size_t offr;
} RingBuffer;

const size_t ringBufferSize = 8 * 1024 * sizeof(queuePacket);

static RingBuffer ringBuffer;
static LightLock lock;
static Handle eventNotEmpty;
static Handle eventEmpty;
static Handle eventAlmostEmpty;
static Handle eventAllocOk;
static bool want_eventNotEmpty, want_eventEmpty,
			want_eventAlmostEmpty, want_eventAllocOk;
static size_t checkPoint;
static bool usesCheckPoint;
static u32 framesDone;
static LightEvent eventFrameDone;
static bool waitingFameDone;

static bool initRingBuffer()
{
	ringBuffer.mem = (queuePacket *) malloc(ringBufferSize);
	ringBuffer.offw = 0;
	ringBuffer.offr = 0;
	ringBuffer.num = ringBufferSize / sizeof(queuePacket);

	__dmb();

	return ringBuffer.mem != NULL;
}

bool queueInit()
{
	if (svcCreateEvent(&eventEmpty, RESET_ONESHOT) != 0)
		goto fail;
	if (svcCreateEvent(&eventNotEmpty, RESET_ONESHOT) != 0)
		goto fail;
	if (svcCreateEvent(&eventAlmostEmpty, RESET_ONESHOT) != 0)
		goto fail;
	if (svcCreateEvent(&eventAllocOk, RESET_ONESHOT) != 0)
		goto fail;

	LightLock_Init(&lock);
	LightEvent_Init(&eventFrameDone, RESET_ONESHOT);

	return initRingBuffer();

fail:

	NDS3D_driverPanic("queueInit failed!");
	return false;
}

static bool islocked;

static inline void queueLock()
{
	LightLock_Lock(&lock);
	islocked = true;
}

static inline void queueUnlock()
{
	assert(islocked);
	islocked = false;
	LightLock_Unlock(&lock);
}

// lock must be taken
static size_t queueGetFreeSpace()
{
	assert(islocked);

	size_t diff;
	size_t offw = ringBuffer.offw;
	size_t offr = ringBuffer.offr;

	if (usesCheckPoint)
	{
		size_t cp = checkPoint;

		if (offw < cp)
		{
			diff = cp - offw;
		}
		else
		{
			diff = ringBuffer.num - offw;
			diff += cp;
		}
	}
	else
	{
		if (offw < offr)
		{
			diff = offr - offw;
		}
		else
		{
			diff = ringBuffer.num - offw;
			diff += offr;
		}
	}

	return diff;
}

// lock must be taken
bool queueCheckDiff(size_t maxDiff)
{
	assert(islocked);

	size_t diff;
	size_t offw = ringBuffer.offw;
	size_t offr = ringBuffer.offr;

	if (offw < offr)
	{
		diff = ringBuffer.num - offr;
		diff += offw;
	}
	else
	{
		diff = offw - offr;
	}

	return maxDiff >= diff;
}

// lock must be taken
static void queueWakeupSleepers()
{
	assert(islocked);

	if (want_eventNotEmpty)
	{
		if (ringBuffer.offw != ringBuffer.offr)
		{
			want_eventNotEmpty = false;
			svcSignalEvent(eventNotEmpty);
		}
	}

	if (want_eventEmpty)
	{
		if (ringBuffer.offw == ringBuffer.offr && !usesCheckPoint)
		{
			want_eventEmpty = false;
			svcSignalEvent(eventEmpty);
		}
	}

	if (want_eventAlmostEmpty)
	{
		if (queueCheckDiff(10) && !usesCheckPoint)
		{
			want_eventAlmostEmpty = false;
			svcSignalEvent(eventAlmostEmpty);
		}
	}

	if (want_eventAllocOk)
	{
		if (queueGetFreeSpace() >= 100u)
		{
			want_eventAllocOk = false;
			svcSignalEvent(eventAllocOk);
		}
	}
}

bool queueWaitForEvent(int mode, s64 nanoseconds)
{
	Result res;
	Handle *event;

	queueLock();

	switch (mode)
	{
		case QUEUE_EVENT_NOT_EMPTY:
			want_eventNotEmpty = true;
			event = &eventNotEmpty;
			break;
		case QUEUE_EVENT_EMPTY:
			want_eventEmpty = true;
			event = &eventEmpty;
			break;
		case QUEUE_EVENT_ALMOST_EMPTY:
			want_eventAlmostEmpty = true;
			event = &eventAlmostEmpty;
			break;
		case QUEUE_EVENT_ALLOC_OK:
			want_eventAllocOk = true,
			event = &eventAllocOk;
			break;
		default:
			event = NULL;
	}

	/* First, wake up any other sleeping thread */
	queueWakeupSleepers();

	queueUnlock();

	res = svcWaitSynchronization(*event, nanoseconds);

	return res == 0;
}

queuePacket *queueAllocPacket()
{
	queueLock();

	size_t curOffsetW = ringBuffer.offw;

	if (queueGetFreeSpace() < 100u)
	{
		queueUnlock();
		return NULL;
/*
#ifdef DIAGNOSTIC
		if (curOffsetW < curOffsetR && curOffsetW + 10 >= curOffsetR)
		{
			queueDump();
			NDS3D_driverPanic("Queue overflow! offw 0x%X, offr 0x%X",
				curOffsetW, curOffsetR);
		}
#endif
*/
	}

	queueUnlock();

	return ringBuffer.mem + curOffsetW;
}

void queueEnqueuePacket(queuePacket *packet)
{
	queueLock();

	size_t curOffset = ringBuffer.offw;
	size_t packet_ = (size_t) packet;

#ifdef DIAGNOSTIC
	if (packet_ != (size_t) ringBuffer.mem + curOffset * sizeof(queuePacket))
		NDS3D_driverPanic("Bad packet ptr!");
#endif
	//N3DS_Print("sent packet %i\n", packet->type);
	
	ringBuffer.offw = (curOffset + 1) % ringBuffer.num;

	if (want_eventNotEmpty)
	{
		want_eventNotEmpty = false;
		queueUnlock();
		svcSignalEvent(eventNotEmpty);
	}
	else queueUnlock();

	//N3DS_Print("enqueued w %x, r %x\n", ringBuffer.offw, ringBuffer.offr);
}

bool queuePollForDequeue()
{
	queueLock();

	bool result = ringBuffer.offw != ringBuffer.offr;

	queueUnlock();

	return result;
}

queuePacket *queueDequeuePacket()
{
	queuePacket *packet;

	queueLock();

	size_t curOffset = ringBuffer.offr;

#ifdef DIAGNOSTIC
	if (curOffset == ringBuffer.offw)
		NDS3D_driverPanic("queueDequeuePacket: Queue empty!");
#endif

	packet = ringBuffer.mem + curOffset;
	
	ringBuffer.offr = (curOffset + 1) % ringBuffer.num;

	//N3DS_Print("dequeued w %x, r %x\n", ringBuffer.offw, ringBuffer.offr);

	if (want_eventEmpty)
	{
		if (ringBuffer.offw == ringBuffer.offr && !usesCheckPoint)
		{
			want_eventEmpty = false;
			queueUnlock();
			svcSignalEvent(eventEmpty);
			goto done;
		}
	}
	else if (want_eventAlmostEmpty)
	{
		if (queueCheckDiff(10))
		{
			want_eventAlmostEmpty = false;
			queueUnlock();
			svcSignalEvent(eventAlmostEmpty);
			goto done;
		}
	}
	else if (want_eventAllocOk)
	{
		if (queueGetFreeSpace() >= 100u)
		{
			want_eventAllocOk = false;
			queueUnlock();
			svcSignalEvent(eventAllocOk);
			goto done;
		}
	}

	queueUnlock();

done:
	
	return packet;
}

void queueCreateCheckPoint()
{
	queueLock();
	assert(!usesCheckPoint);
	checkPoint = ringBuffer.offr;
	usesCheckPoint = true;
	queueUnlock();
}

void queueRestoreCheckPoint()
{
	queueLock();
	assert(usesCheckPoint);
	ringBuffer.offr = checkPoint;
	usesCheckPoint = false;
	checkPoint = 0;
	queueUnlock();
}

float queueGetUsage()
{
	queueLock();

	size_t diff;
	size_t offw = ringBuffer.offw;
	size_t offr = ringBuffer.offr;

	if (offw < offr)
	{
		diff = ringBuffer.num - offr;
		diff += offw;
	}
	else
	{
		diff = offw - offr;
	}

	queueUnlock();

	return (float) diff / (float) ringBuffer.num;
}

u32 queueGetFrameProgress()
{
	queueLock();
	u32 retval = framesDone;
	queueUnlock();

	return retval;
}

void queueWaitForFrameProgress()
{
	queueLock();
	waitingFameDone = true;
	queueUnlock();

	LightEvent_Wait(&eventFrameDone);
}

void queueNotifyFrameProgress()
{
	queueLock();

	framesDone++;

	if (waitingFameDone)
	{
		LightEvent_Signal(&eventFrameDone);
		waitingFameDone = false;
	}

	queueUnlock();
}

void queueDump()
{
	queueLock();

	size_t offw = ringBuffer.offw;
	size_t offr = ringBuffer.offr;

	N3DS_Print("queue w %x, r %x\n", offw, offr);
	N3DS_Print("data [w] %x, [r] %x\n", ringBuffer.mem[offw], ringBuffer.mem[offr]);
	NDS3D_driverMemDump(ringBuffer.mem, ringBufferSize);

	queueUnlock();

	N3DS_Print("queue dump done.\n");
}
