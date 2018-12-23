#include <3ds.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include "../doomdef.h"
#include "../m_fixed.h"
#include "../s_sound.h"
#include "../i_sound.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../byteptr.h"
#include "nds_utils.h"

#define SAMPLERATE			44100
#define MAX_SAVED_CACHES	64
typedef struct 
{
	Mix_Chunk *chunk;
	void *cache;
} SavedCacheEntry;

//static SavedCacheEntry savedCaches[MAX_SAVED_CACHES];

UINT8 sound_started = false;

static bool initialized;
static Mix_Music *music;
static UINT8 music_volume, sfx_volume;
static double loop_point;
static boolean songpaused;


static void SoundDriverInit(void)
{
	if (initialized)
		return;

	printf("SoundDriverInit!\n");

	// Initialize SDL.
	if (SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		NDS3D_driverPanic("SDL_Init failed!");
	}

	// Initialize SDL_mixer 
	if (Mix_OpenAudio(SAMPLERATE, AUDIO_S16LSB, 2, 1024*4) < 0)
	{
		NDS3D_driverPanic("Mix_OpenAudio failed!");
	}

	// Set number of channels being mixed
	Mix_AllocateChannels(16);

	initialized = true;

	sound_started = 1;
}

static void ensureDriverInitialized()
{
	if(!initialized)
		SoundDriverInit();
}
/*
static void registerCacheForChunk(void *cache, Mix_Chunk *chunk)
{
	for(size_t i=0; i<MAX_SAVED_CACHES; i++)
	{
		if(savedCaches[i].cache == NULL)
		{
			savedCaches[i].cache = cache;
			savedCaches[i].chunk = chunk;
			return;
		}
	}

	NDS_Sound_driverPanic("Out of free saved caches!");
}

static void *findRemoveCacheForChunk(Mix_Chunk *chunk)
{
	void *cache;

	for(size_t i=0; i<MAX_SAVED_CACHES; i++)
	{
		if(savedCaches[i].chunk == chunk)
		{
			cache = savedCaches[i].cache;
			savedCaches[i].cache = NULL;
			savedCaches[i].chunk = NULL;
			return cache;
		}
	}

	NDS_Sound_driverPanic("Could not find chunk in cache!");

	return NULL;
}
*/
static inline bool isDoomSound(void *stream)
{
	UINT16 ver;

	// lump header
	ver = READUINT16(stream);
	
	// It should be 3 if it's a doomsound...
	return ver == 3 ? true : false;
}

static Mix_Chunk *ds2chunk(void *stream)
{
	u16 freq;
	size_t samples, i, newsamples;
	u8 *sound;
	SINT8 *s;
	s16 *d;
	INT16 o;
	fixed_t step, frac;
	Mix_Chunk *chunk;

	(void) READUINT16(stream);	// skip lump header
	freq = READUINT16(stream);
	samples = READUINT32(stream);

	switch(freq)
	{
		case 44100:
			newsamples = samples;
			break;

		case 22050:
			newsamples = samples<<1;
			break;

		case 11025:
			newsamples = samples<<2;
			break;

		default:
			frac = (44100 << FRACBITS) / (UINT32)freq;
			if (!(frac & 0xFFFF)) // other solid multiples (change if FRACBITS != 16)
				newsamples = samples * (frac >> FRACBITS);
			else // strange and unusual fractional frequency steps, plus anything higher than 44100hz.
				newsamples = FixedMul(FixedDiv(samples, freq), 44100) + 1; // add 1 to counter truncation.
			if (newsamples >= UINT32_MAX>>2)
				return NULL; // would and/or did wrap, can't store.
			break;
	}

	if(newsamples > (1024 * 1024 * 8)>>2)
	{
		printf("ds2chunk: too many samples (%i)\n", newsamples);
		return NULL;
	}

	sound = Z_Malloc(newsamples<<2, PU_SOUND, NULL);	// newsamples * sizeof(S16) * numchannels
	if(!sound)
	{
		printf("ds2chunk: alloc failed, samples (%i)\n", newsamples);
		return NULL;
	}

	s = (SINT8 *)stream;
	d = (s16 *)sound;

	// NOTE: we cannot assume "samples" is aligned to 4!

	switch(freq)
	{
		case 44100:
			for(i=0; i<samples; i++)
			{
				o = ((s16)(*s++)+0x80)<<8;
				*d++ = o; // left channel
				*d++ = o; // right channel
			}
			break;

		case 22050:
			for(i=0; i<samples; i++)
			{
				o = ((s16)(*s++)+0x80)<<8;
				*d++ = o; // left channel
				*d++ = o; // right channel
				*d++ = o; // left channel
				*d++ = o; // right channel
			}
			break;

		case 11025:
			// The compiler will optimze this. Trust me, I checked it.
			for(i=0; i<samples; i++)
			{
				o = ((s16)(*s++)+0x80)<<8;
				*d++ = o; // left channel
				*d++ = o; // right channel
				*d++ = o; // left channel
				*d++ = o; // right channel
				*d++ = o; // left channel
				*d++ = o; // right channel
				*d++ = o; // left channel
				*d++ = o; // right channel
			}
			break;

		default: // convert arbitrary hz to 44100.
			step = 0;
			i = 0;
			frac = ((UINT32)freq << FRACBITS) / 44100;
			while (i < samples)
			{
				o = (s16)(*s+0x80)<<8; // changed signedness and shift up to 16 bits
				while (step < FRACUNIT) // this is as fast as I can make it.
				{
					*d++ = o; // left channel
					*d++ = o; // right channel
					step += frac;
				}
				do {
					i++; s++;
					step -= FRACUNIT;
				} while (step >= FRACUNIT);
			}
			break;
	}

#ifdef DIAGNOSTIC
	//printf("Got DoomSound freq %i\n", freq);
#endif

	chunk = Mix_QuickLoad_RAW(sound, (UINT8*)d-sound);
	if(!chunk)
	{
		free(sound);
		chunk = NULL;
	}

	// return Mixer Chunk.
	return chunk;
}

void I_StartupSound(void)
{
	ensureDriverInitialized();
}

void I_ShutdownSound(void){}

//
//  SFX I/O
//

void *I_GetSfx(sfxinfo_t *sfx)
{
	void *lump;
	Mix_Chunk *chunk;
	SDL_RWops *rw;

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);
	sfx->length = W_LumpLength(sfx->lumpnum);

	lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);
	if (!lump)
		return NULL;

	if(isDoomSound(lump))
	{
		// convert from standard DoomSound format.
		chunk = ds2chunk(lump);
		if (chunk)
		{
			Z_Free(lump);
			return chunk;
		}
		else return NULL;
	}

	// Try to load it as a WAVE or OGG using Mixer.
	rw = SDL_RWFromMem(lump, sfx->length);
	if (rw != NULL)
	{
		chunk = Mix_LoadWAV_RW(rw, 1);
		return chunk;
	}

	//Z_Free(lump);		XXX

	return NULL; // haven't been able to get anything
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	Mix_Chunk *chunk = sfx->data;

	if (chunk)
	{
		UINT8 *abufdata = NULL;
		if (chunk->allocated == 0)
		{
			// We allocated the data in this chunk, so get the abuf from mixer, then let it free the chunk, THEN we free the data
			// I believe this should ensure the sound is not playing when we free it
			abufdata = chunk->abuf;
		}
		Mix_FreeChunk(chunk);
		if (abufdata)
		{
			// I'm going to assume we used Z_Malloc to allocate this data.
			Z_Free(abufdata);
		}

		sfx->data = NULL;
	}

	sfx->lumpnum = LUMPERROR;
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, UINT8 pitch, UINT8 priority, INT32 channel)
{
	UINT8 volume = (((UINT16)vol + 1) * (UINT16)sfx_volume) / 62; // (256 * 31) / 62 == 127
	INT32 handle = Mix_PlayChannel(channel, S_sfx[id].data, 0);
	Mix_Volume(handle, volume);
	Mix_SetPanning(handle, min((UINT16)(0xff-sep)<<1, 0xff), min((UINT16)(sep)<<1, 0xff));
	(void)pitch; // Mixer can't handle pitch
	(void)priority; // priority and channel management is handled by SRB2...
	return handle;
}

void I_StopSound(INT32 handle)
{
	Mix_HaltChannel(handle);
}

boolean I_SoundIsPlaying(INT32 handle)
{
	return Mix_Playing(handle);
}

void I_UpdateSound(void){};

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep, UINT8 pitch)
{
	UINT8 volume = (((UINT16)vol + 1) * (UINT16)sfx_volume) / 62; // (256 * 31) / 62 == 127
	Mix_Volume(handle, volume);
	Mix_SetPanning(handle, min((UINT16)(0xff-sep)<<1, 0xff), min((UINT16)(sep)<<1, 0xff));
	(void)pitch;
}

void I_SetSfxVolume(UINT8 volume)
{
	sfx_volume = volume;
}

/// ------------------------
/// Music Hooks
/// ------------------------

static void music_loop(void)
{
	Mix_PlayMusic(music, 0);
	if (Mix_SetMusicPosition(loop_point * 1000.0) != 0)
	{
		printf("Mix_SetMusicPosition: %s\n", Mix_GetError());
	}
}


/// ------------------------
/// Music System
/// ------------------------

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
	I_UnloadSong();
}

/// ------------------------
/// Music Properties
/// ------------------------

musictype_t I_SongType(void)
{
	if (!music)
		return MU_NONE;
	else if (Mix_GetMusicType(music) == MUS_MID)
		return MU_MID;
	else if (Mix_GetMusicType(music) == MUS_MOD || Mix_GetMusicType(music) == MUS_MODPLUG)
		return MU_MOD;
	else if (Mix_GetMusicType(music) == MUS_MP3 || Mix_GetMusicType(music) == MUS_MP3_MAD)
		return MU_MP3;
	else
		return (musictype_t)Mix_GetMusicType(music);
}

boolean I_SongPlaying(void)
{
	return music != NULL;
}

boolean I_SongPaused(void)
{
	return songpaused;
}


void I_PauseSong()
{
	Mix_PauseMusic();
	songpaused = true;
}

void I_ResumeSong()
{
	Mix_ResumeMusic();
	songpaused = false;
}

/// ------------------------
/// Music Effects
/// ------------------------

boolean I_SetSongSpeed(float speed)
{
	return false;
}

boolean I_PlaySong(boolean looping)
{
	if (!music)
		return false;

#ifdef DIAGNOSTIC
	//printf("I_PlaySong: loop_point %f, looping: %i\n", loop_point, (int) looping);
#endif

	Mix_VolumeMusic((UINT32)music_volume*128/31);

	if (Mix_PlayMusic(music, looping && loop_point == 0.0 ? -1 : 0) == -1)
	{
		CONS_Alert(CONS_ERROR, "Mix_PlayMusic: %s\n", Mix_GetError());
		return false;
	}

	if (loop_point != 0.0)
		Mix_HookMusicFinished(music_loop);

	return true;
}

void I_StopSong(void)
{
	if (!music)
		return;

	Mix_HaltMusic();
	//music = NULL;
	if (music)
	{
		Mix_HookMusicFinished(NULL);
		Mix_HaltMusic();
	}
}

void I_UnloadSong()
{
	if (music)
	{
		Mix_FreeMusic(music);
		music = NULL;
	}
}

//
//  DIGMUSIC I/O
//

void I_InitDigMusic(void)
{
	SoundDriverInit();
}

void I_ShutdownDigMusic(void)
{
	if (!initialized)
		return;

	Mix_HaltMusic();
	Mix_HookMusicFinished(NULL);
	Mix_FreeMusic(music);
	music = NULL;

	// quit SDL_mixer
	Mix_CloseAudio();

	initialized = false;
}

/// ------------------------
/// Music Playback
/// ------------------------

boolean I_LoadSong(char *data, size_t len)
{
	SDL_RWops *rw;
	ensureDriverInitialized();

	if (music)
		I_UnloadSong();

	rw = SDL_RWFromMem(data, len);
	if (rw != NULL)
	{
		music = Mix_LoadMUS_RW(rw);
	}
	if (!music)
	{
		CONS_Alert(CONS_ERROR, "Mix_LoadMUS_RW: %s\n", Mix_GetError());
		return false;
	}

	// printf("I_LoadSong dump:\n%02X %02X %02X %02X %02X %02X %02X %02X\n", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

	// Find the OGG loop point.
	loop_point = 0.0;
	{
		const char *key1 = "LOOP";
		const char *key2 = "POINT=";
		const char *key3 = "MS=";
		const UINT8 key1len = strlen(key1);
		const UINT8 key2len = strlen(key2);
		const UINT8 key3len = strlen(key3);
		char *p = data;
		while ((UINT32)(p - data) < len)
		{
			if (strncmp(p++, key1, key1len))
				continue;
			p += key1len-1; // skip OOP (the L was skipped in strncmp)
			if (!strncmp(p, key2, key2len)) // is it LOOPPOINT=?
			{
				p += key2len; // skip POINT=
				loop_point = (double)((44.1L+atoi(p)) / 44100.0L); // LOOPPOINT works by sample count.
				// because SDL_Mixer is USELESS and can't even tell us
				// something simple like the frequency of the streaming music,
				// we are unfortunately forced to assume that ALL MUSIC is 44100hz.
				// This means a lot of tracks that are only 22050hz for a reasonable downloadable file size will loop VERY badly.
			}
			else if (!strncmp(p, key3, key3len)) // is it LOOPMS=?
			{
				p += key3len; // skip MS=
				loop_point = atoi(p) / 1000.0L; // LOOPMS works by real time, as miliseconds.
				// Everything that uses LOOPMS will work perfectly with SDL_Mixer.
			}
			// Neither?! Continue searching.
		}
	}

	/*
#ifdef DIAGNOSTIC
	if (loop_point)
		printf("Found loop_point\n");
#endif
	*/

	return true;
}

void I_SetMusicVolume(UINT8 volume)
{
	music_volume = volume;
	if (!music)
		return;
	Mix_VolumeMusic((UINT32)volume*128/31);
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	return false;
}
