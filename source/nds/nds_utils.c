#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <malloc.h>
#include <3ds.h>

//#define NDS3D_DEBUG_PRINT

//#define NDS3D_DEBUG_LOG

volatile bool paniced = false;

void N3DS_Panic(const char *s, ...)
{
	char tmp[512];

	paniced = true;

	va_list args;
	va_start(args, s);
	vsnprintf(tmp, sizeof tmp, s, args);
	va_end(args);
	
	printf("\n\n********* PANIC *********\n");
	printf("%s\n\n", tmp);
	
	for(;;);
}

void N3DS_Print(const char *s, ...)
{
	char tmp[512];

	if (paniced)
	{
		for(;;);	// hang
	}

	va_list args;
	va_start(args, s);
	vsnprintf(tmp, sizeof tmp, s, args);
	va_end(args);

	printf("%s", tmp);
}

void NDS3D_driverLog(const char *s, ...)
{
#ifndef NDS3D_DEBUG_PRINT
#ifndef NDS3D_DEBUG_LOG
	return;
#endif
#endif
	char tmp[512];
	
	va_list args;
	va_start(args, s);
	vsnprintf(tmp, sizeof tmp, s, args);
	va_end(args);

#ifdef NDS3D_DEBUG_PRINT
	printf("%s", tmp);
#endif

#ifdef	NDS3D_DEBUG_LOG
	static FILE *f;
	static bool fileOpen;
	if(!fileOpen)
	{
		f = fopen("srb2_nds3d_log.txt", "a");
		fileOpen = true;
	}
	
	fwrite(tmp, strlen(tmp), 1, f);
	
	fclose(f);
	fileOpen = false;
#endif
}

void NDS3D_driverMemDump(void*buf, size_t size)
{
	char tmp[512];
	static size_t findex;
	FILE *f;
	
	snprintf(tmp, sizeof tmp, "dumpfile_%i_.raw", findex);
	
	f = fopen(tmp, "wb");
	
	fwrite(buf, size, 1, f);
	
	fclose(f);
	
	NDS3D_driverLog("File %s saved.\n", tmp);
	
	findex++;
}

__attribute__((noreturn))
void NDS3D_driverPanic(const char *s, ...)
{
	char tmp[512];

	paniced = true;

	va_list args;
	va_start(args, s);
	vsnprintf(tmp, sizeof tmp, s, args);
	va_end(args);

	NDS3D_driverLog("\n\n********* PANIC *********\n");
	NDS3D_driverLog("%s\n", tmp);
	
	printf("\n\n********* PANIC *********\n");
	printf("%s\n", tmp);
	
	printf("Shutting down in 10 seconds...\n");
	svcSleepThread(1000LL * 1000LL * 1000LL * 10);
	exit(-1);
}

size_t heapGetFreeSpace()
{
	struct mallinfo mi = mallinfo();

	return (size_t) mi.fordblks;
}

void NDS3D_driverHeapStatus()
{
	NDS3D_driverLog("HEAP STATUS:\nLINEAR: 0x%x,\nVRAM: 0x%x,\nLIBC: 0x%x\n\n",
				linearSpaceFree(), vramSpaceFree(), heapGetFreeSpace());
}

int stricmp(const char *str1, const char *str2)
{
	int diff;
	
	for(;; str1++, str2++)
	{
		diff = tolower(*str1) - tolower(*str2);
		if(diff != 0 || *str1 == '\0')
			return diff;
	}
	
	return 0;
}

int strnicmp(const char *str1, const char *str2, size_t len)
{
	int diff;
	
	if(len == 0)
		return 0;
	
	for(;; str1++, str2++)
	{	
		if(len != 0) len --;
		else return 0;
		diff = tolower(*str1) - tolower(*str2);
		if(diff != 0 || *str1 == '\0')
			return diff;
	}
	
	return 0;
}
