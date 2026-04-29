// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  p_tick.h
/// \brief Thinkers, Tickers

#ifndef __P_TICK__
#define __P_TICK__

#ifdef __GNUG__
#pragma interface
#endif

extern tic_t leveltime;

// Called by G_Ticker. Carries out all thinking of enemies and players.
void Command_Numthinkers_f(void);
void Command_CountMobjs_f(void);
#ifdef __3DS__
void Command_Thinkertimes_f(void);
extern boolean thinker_prof_enabled;
unsigned long long P_MobjProfNow(void);
void P_MobjProfHit(INT32 type, unsigned long long dt_ticks);
// Player-position cache, refreshed once per tic in P_RunThinkers.
// Used by P_MobjThinker's global distance gate to skip far-away mobjs.
extern INT32 mp_active_count;
extern fixed_t mp_active_x[]; // sized MAXPLAYERS
extern fixed_t mp_active_y[];
#endif

void P_Ticker(boolean run);
void P_PreTicker(INT32 frames);
void P_DoTeamscrambling(void);
void P_RemoveThinkerDelayed(void *pthinker); //killed
mobj_t *P_SetTarget(mobj_t **mo, mobj_t *target);   // killough 11/98

#endif
