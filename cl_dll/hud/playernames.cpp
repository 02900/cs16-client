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

#define PN_HEAD_OFFSET   46.0f   // height above the entity origin for the name
#define PN_CHEST_OFFSET  18.0f   // LOS trace target / distance reference
#define PN_LOS_CLEAR     0.95f   // trace fraction that counts as "visible"

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
		if( i == meIdx || g_PlayerExtraInfo[i].dead )
			continue;

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
		int r = enemy ? 220 : 90;
		int g = enemy ? 60  : 210;
		int b = enemy ? 60  : 90;

		int len = DrawUtils::HudStringLen( name );
		DrawUtils::DrawHudString( sx - len / 2, sy, ScreenWidth, name, r, g, b );

		// thin health bar right under the name, as wide as the name (Max Payne 3 underline look).
		int h = g_PlayerExtraInfo[i].sb_health;
		if( h > 0 )
		{
			if( h > 100 ) h = 100;
			int bw = len < XRES( 18 ) ? XRES( 18 ) : len;
			int bh = YRES( 2 );
			int bx = sx - bw / 2;
			int by = sy + gHUD.m_iFontHeight;
			FillRGBA( bx - 1, by - 1, bw + 2, bh + 2, 0, 0, 0, 160 );
			FillRGBA( bx, by, ( bw * h ) / 100, bh, r, g, b, 220 );
		}
	}

	if( wall )
		gEngfuncs.pEventAPI->EV_PopPMStates();

	return 1;
}
