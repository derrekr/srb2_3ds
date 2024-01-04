#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdatomic.h>
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

#define RINGBUF_SIZE	(8 * 1024)

static queuePacket packets[RINGBUF_SIZE];
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
	ringBuffer.mem = packets;
	ringBuffer.offw = 0;
	ringBuffer.offr = 0;
	ringBuffer.num = RINGBUF_SIZE;

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

// lock must be taken
static size_t queueGetFreeSpace()
{
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

static void queueWakeupSleepers()
{
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

	res = svcWaitSynchronization(*event, nanoseconds);

	return res == 0;
}

queuePacket *queueAllocPacket()
{
	size_t curOffsetW = ringBuffer.offw;

	if (queueGetFreeSpace() < 100u)
	{
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

	return ringBuffer.mem + curOffsetW;
}

void queueEnqueuePacket(queuePacket *packet)
{
	size_t curOffset = ringBuffer.offw;
	size_t packet_ = (size_t) packet;

#ifdef DIAGNOSTIC
	if (packet_ != (size_t) ringBuffer.mem + curOffset * sizeof(queuePacket))
		NDS3D_driverPanic("Bad packet ptr!");
#endif
	//N3DS_Print("sent packet %i\n", packet->type);
	
	atomic_store_explicit(&ringBuffer.offw, (curOffset + 1) % ringBuffer.num, memory_order_acq_rel);

	if (want_eventNotEmpty)
	{
		want_eventNotEmpty = false;
		svcSignalEvent(eventNotEmpty);
	}

	//N3DS_Print("enqueued w %x, r %x\n", ringBuffer.offw, ringBuffer.offr);
}

bool queuePollForDequeue()
{
	size_t offw = atomic_load_explicit(&ringBuffer.offw, memory_order_acq_rel);
	size_t offr = atomic_load_explicit(&ringBuffer.offr, memory_order_acq_rel);
	bool result = offw != offr;
	return result;
}

queuePacket *queueDequeuePacket()
{
	queuePacket *packet;

	size_t curOffset = ringBuffer.offr;

#ifdef DIAGNOSTIC
	if (curOffset == ringBuffer.offw)
		NDS3D_driverPanic("queueDequeuePacket: Queue empty!");
#endif

	packet = ringBuffer.mem + curOffset;

	atomic_store_explicit(&ringBuffer.offr, (curOffset + 1) % ringBuffer.num, memory_order_release);


	//N3DS_Print("dequeued w %x, r %x\n", ringBuffer.offw, ringBuffer.offr);

	if (want_eventEmpty)
	{
		if (ringBuffer.offw == ringBuffer.offr && !usesCheckPoint)
		{
			want_eventEmpty = false;
			svcSignalEvent(eventEmpty);
			goto done;
		}
	}
	else if (want_eventAlmostEmpty)
	{
		if (queueCheckDiff(10))
		{
			want_eventAlmostEmpty = false;
			svcSignalEvent(eventAlmostEmpty);
			goto done;
		}
	}
	else if (want_eventAllocOk)
	{
		if (queueGetFreeSpace() >= 100u)
		{
			want_eventAllocOk = false;
			svcSignalEvent(eventAllocOk);
			goto done;
		}
	}

done:
	
	return packet;
}

void queueCreateCheckPoint()
{
	assert(!usesCheckPoint);
	checkPoint = ringBuffer.offr;
	usesCheckPoint = true;
}

void queueRestoreCheckPoint()
{
	assert(usesCheckPoint);
	ringBuffer.offr = checkPoint;
	usesCheckPoint = false;
	checkPoint = 0;
}

float queueGetUsage()
{
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

	return (float) diff / (float) ringBuffer.num;
}

u32 queueGetFrameProgress()
{
	u32 retval = framesDone;

	return retval;
}

void queueWaitForFrameProgress(u32 expected)
{
	if (expected <= framesDone)
	{
		return;
	}

	waitingFameDone = true;

	LightEvent_Wait(&eventFrameDone);
}

void queueNotifyFrameProgress()
{
	framesDone++;

	if (waitingFameDone)
	{
		LightEvent_Signal(&eventFrameDone);
		waitingFameDone = false;
	}
}

void queueDump()
{

	size_t offw = ringBuffer.offw;
	size_t offr = ringBuffer.offr;

	N3DS_Print("queue w %x, r %x\n", offw, offr);
	N3DS_Print("data [w] %x, [r] %x\n", ringBuffer.mem[offw], ringBuffer.mem[offr]);
	NDS3D_driverMemDump(ringBuffer.mem, RINGBUF_SIZE);

	N3DS_Print("queue dump done.\n");
}
