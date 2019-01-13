
// Copyright (C) 2018 by derrek

#pragma once

#include "../doomtype.h"
#include "../hardware/hw_defs.h"
#include "../hardware/hw_dll.h"
#include "../hardware/hw_md2.h"


#define CMD_TYPE_DUMMY		0
#define CMD_TYPE_FADE		1
#define CMD_TYPE_FINISH		2
#define CMD_TYPE_DRAW		3
#define CMD_TYPE_BLEND		4
#define CMD_TYPE_CLEAR		5
#define CMD_TYPE_FLUSH		6
#define CMD_TYPE_SPECIAL	7
#define CMD_TYPE_TRANSFORM	8
#define CMD_TYPE_TRANSFRST	9
#define CMD_TYPE_EXIT		10


typedef struct 
{
	u32			surfColor;
	u32			geometryIdx;
	u32			geometryNum;
	C3D_Tex *	tex;
	FBITFIELD 	PolyFlags;
	u32			fogColor;
	u8			fogDensity;
} ArgsDraw;

typedef struct 
{
	FBITFIELD 	PolyFlags;
} ArgsBlend;

typedef struct 
{
	FBOOLEAN	ColorMask;
	FBOOLEAN	DepthMask;
	u32			ClearColor;
} ArgsClear;

typedef struct 
{
} ArgsTexFlush;

typedef struct 
{
	hwdspecialstate_t		IdState;
	INT32					Value;
} ArgsSpecial;

typedef struct 
{
	FTransform		transform;
} ArgsTransform;

typedef struct 
{
} ArgsTransformReset;

typedef struct 
{
} ArgsFinish;

typedef struct 
{
	u32			fadeColor;
} ArgsFade;

typedef struct
{
	u32 type;
	union {
		ArgsDraw argsDraw;
		ArgsBlend argsBlend;
		ArgsClear argsClear;
		ArgsTexFlush argsTexFlush;
		ArgsSpecial argsSpecial;
		ArgsTransform argsTransform;
		ArgsTransformReset argsTransformReset;
		ArgsFinish argsFinish;
		ArgsFade argsFade;
	} args;
} queuePacket;

enum {
	QUEUE_EVENT_NOT_EMPTY,
	QUEUE_EVENT_EMPTY,
	QUEUE_EVENT_ALMOST_EMPTY,
	QUEUE_EVENT_ALLOC_OK,
	QUEUE_EVENT_FRAME_DONE
};


bool queueInit();
queuePacket *queueAllocPacket();
bool queueWaitForEvent(int mode, s64 nanoseconds);
void queueEnqueuePacket(queuePacket *packet);
bool queuePollForDequeue();
queuePacket *queueDequeuePacket();
void queueCreateCheckPoint();
void queueRestoreCheckPoint();
float queueGetUsage();
u32 queueGetFrameProgress();
void queueWaitForFrameProgress();
void queueNotifyFrameProgress();
