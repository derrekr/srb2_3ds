#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>

void N3DS_Panic(const char *s, ...);
void N3DS_Print(const char *s, ...);
void NDS3D_driverLog(const char *s, ...);
void NDS3D_driverMemDump(void*buf, size_t size);
void NDS3D_driverPanic(const char *s, ...);
void NDS3D_driverHeapStatus();

#define arrayEntries(array)	sizeof(array)/sizeof(*array)

static inline void __dmb(void)
{
	__asm__ __volatile__("mcr p15, 0, %[val], c7, c10, 5" :: [val] "r" (0) : "memory");
}

__attribute__((always_inline, optimize(3))) inline u32 NDS3D_Reverse32(u32 val)   
{  
	__asm("REV %0, %1" : "=r" (val) : "r" (val));
  
	return val;  
}
