/*
playernames.cpp - draw names + health bars over on-screen players (Max Payne 3 style).

Enemies show in red, teammates in green. Health comes from g_PlayerExtraInfo[i].sb_health,
which the server only broadcasts for everyone when mp_scoreboard_showhealth is 4 (the dm/tdm
configs set it). Players are only labelled when in PVS and (optionally) with a clear line of
sight, so it doesn't reveal people through walls by default.
*/

#define _USE_MATH_DEFINES
#include "math.h"
#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "cl_entity.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "const.h"
#include "playernames.h"
#include "mp3textfont.h"
#include "mp3palette.h"

#define PN_HEAD_OFFSET   46.0f   // height above the entity origin for the name
#define PN_CHEST_OFFSET  18.0f   // LOS trace target / distance reference
#define PN_LOS_CLEAR     0.95f   // trace fraction that counts as "visible"
#define PN_GHOST_TIME    0.5f    // how long the just-lost health chunk stays gray on the bar

// Damage flash state per player: when a bar's health drops, the lost fraction is frozen in
// gray for PN_GHOST_TIME and then snapped away (Max Payne 3 hit feedback).
static int   s_iPrevHealth[MAX_PLAYERS + 1];
static int   s_iGhostHealth[MAX_PLAYERS + 1];
static float s_flGhostTime[MAX_PLAYERS + 1];

int CHudPlayerNames::Init( void )
{
	gHUD.AddHudElem( this );
	m_iFlags = HUD_DRAW;

	cl_playernames           = CVAR_CREATE( "cl_playernames",           "1",    FCVAR_ARCHIVE );
	cl_playernames_wallcheck = CVAR_CREATE( "cl_playernames_wallcheck", "1",    FCVAR_ARCHIVE );
	cl_playernames_dist      = CVAR_CREATE( "cl_playernames_dist",      "2500", FCVAR_ARCHIVE );
	return 1;
}

int CHudPlayerNames::VidInit( void )
{
	return 1;
}

int CHudPlayerNames::Draw( float flTime )
{
	if( !cl_playernames->value || gHUD.m_iIntermission )
		return 1;

	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if( !local )
		return 1;

	int meIdx = gHUD.m_Scoreboard.m_iPlayerNum;
	bool wall = cl_playernames_wallcheck->value != 0.0f;
	float maxDist = cl_playernames_dist->value;

	static cvar_t *ffaCvar = NULL;
	if( !ffaCvar ) ffaCvar = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );
	bool ffa = ffaCvar && ffaCvar->value != 0.0f;

	vec3_t eye;
	VectorCopy( gHUD.m_vecOrigin, eye ); // camera origin (== eye in first person)

	if( wall )
	{
		gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
		gEngfuncs.pEventAPI->EV_PushPMStates();
		gEngfuncs.pEventAPI->EV_SetSolidPlayers( -1 );
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	}

	for( int i = 1; i <= gEngfuncs.GetMaxClients(); i++ )
	{
		if( i == meIdx )
			continue;

		// damage-flash bookkeeping runs for every live player (even when the label is culled)
		// so a hit on a briefly-unseen player doesn't flash stale gray later
		int hNow = g_PlayerExtraInfo[i].sb_health;
		if( g_PlayerExtraInfo[i].dead || hNow <= 0 )
		{
			s_iPrevHealth[i] = s_iGhostHealth[i] = 0;
			s_flGhostTime[i] = 0.0f;
			continue;
		}
		if( hNow < s_iPrevHealth[i] )
		{
			// took damage: freeze the lost chunk; chained hits keep the older (higher) edge
			bool active = s_iGhostHealth[i] > s_iPrevHealth[i] && flTime < s_flGhostTime[i] + PN_GHOST_TIME;
			if( !active )
				s_iGhostHealth[i] = s_iPrevHealth[i];
			s_flGhostTime[i] = flTime;
		}
		else if( hNow > s_iPrevHealth[i] )
		{
			s_iGhostHealth[i] = 0; // healed/respawned: drop the ghost
		}
		s_iPrevHealth[i] = hNow;

		cl_entity_t *e = gEngfuncs.GetEntityByIndex( i );
		if( !e || !e->player || e->curstate.solid == SOLID_NOT )
			continue;
		if( e->curstate.messagenum != local->curstate.messagenum )
			continue; // not networked/in PVS this frame

		vec3_t center;
		VectorCopy( e->curstate.origin, center );
		center[2] += PN_CHEST_OFFSET;

		vec3_t d;
		VectorSubtract( center, eye, d );
		float dist = sqrt( d[0] * d[0] + d[1] * d[1] + d[2] * d[2] );
		if( dist > maxDist )
			continue;

		if( wall )
		{
			pmtrace_t tr;
			gEngfuncs.pEventAPI->EV_PlayerTrace( eye, center, PM_STUDIO_BOX | PM_WORLD_ONLY, -1, &tr );
			if( tr.fraction < PN_LOS_CLEAR )
				continue; // blocked by world geometry -> not visible
		}

		vec3_t head, screen;
		VectorCopy( e->curstate.origin, head );
		head[2] += PN_HEAD_OFFSET;
		if( gEngfuncs.pTriAPI->WorldToScreen( head, screen ) != 0 )
			continue; // behind the camera

		int sx = (int)XPROJECT( screen[0] );
		int sy = (int)YPROJECT( screen[1] );

		GetPlayerInfo( i, &g_PlayerInfoList[i] );
		const char *name = g_PlayerInfoList[i].name;
		if( !name || !name[0] )
			continue;

		bool enemy = ffa || ( g_iTeamNumber != 0 && g_PlayerExtraInfo[i].teamnumber != g_iTeamNumber );
		int r = enemy ? MP3_RED_R : 90;   // allies keep their green (not part of the chip palette)
		int g = enemy ? MP3_RED_G : 210;
		int b = enemy ? MP3_RED_B : 90;

		// name in the Max Payne 3 text font (fallback to the CS HUD font)
		int H = YRES( 9 ) / 2;   // over-head name cap height
		int len, nameBottom;
		if( gMp3Text.Ready() )
		{
			len = gMp3Text.StringWidth( name, H );
			gMp3Text.DrawStringOutlined( sx - len / 2, sy + H, H, name, r, g, b, 255 );  // baseline = sy + H
			nameBottom = sy + (int)( H * 1.35f );
		}
		else
		{
			len = DrawUtils::HudStringLen( name );
			DrawUtils::DrawHudString( sx - len / 2, sy, ScreenWidth, name, r, g, b );
			nameBottom = sy + gHUD.m_iFontHeight;
		}

		// thin health bar right under the name, as wide as the name (Max Payne 3 underline look).
		int h = g_PlayerExtraInfo[i].sb_health;
		if( h > 0 )
		{
			if( h > 100 ) h = 100;
			int bw = len < XRES( 18 ) ? XRES( 18 ) : len;
			int bh = YRES( 2 );
			int bx = sx - bw / 2;
			int by = nameBottom;
			FillRGBABlend( bx - 1, by - 1, bw + 2, bh + 2, 0, 0, 0, 160 );
			FillRGBABlend( bx, by, ( bw * h ) / 100, bh, r, g, b, 220 );

			// hit feedback: the just-lost fraction stays GRAY for a moment, then snaps away
			int gh = s_iGhostHealth[i] > 100 ? 100 : s_iGhostHealth[i];
			if( gh > h && flTime < s_flGhostTime[i] + PN_GHOST_TIME )
			{
				int x0 = bx + ( bw * h ) / 100;
				int x1 = bx + ( bw * gh ) / 100;
				FillRGBABlend( x0, by, x1 - x0, bh, MP3_GRAY_LT, 230 );
			}
		}
	}

	if( wall )
		gEngfuncs.pEventAPI->EV_PopPMStates();

	return 1;
}
