/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// death notice
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include <string.h>
#include <stdio.h>
#include "draw_util.h"
#include "strl.h"
#include "mp3textfont.h"
#include "mp3palette.h"
#include "mp3postfx.h"

float color[3];

struct DeathNoticeItem {
	char szKiller[MAX_PLAYER_NAME_LENGTH*2];
	char szVictim[MAX_PLAYER_NAME_LENGTH*2];
	char szWeapon[32];	// weapon name as text (Max Payne 3 kill feed)
	int iId;	// the index number of the associated sprite
	bool bSuicide;
	bool bTeamKill;
	bool bNonPlayerKill;
	float flDisplayTime;
	float *KillerColor;
	float *VictimColor;
	int iHeadShotId;
};

#define MAX_DEATHNOTICES	4
static int DEATHNOTICE_DISPLAY_TIME = 6;

// Max Payne 3 respawn flow -- MUST match the MP3_RESPAWN_* constants in the ReGameDLL fork
// (regamedll/dlls/game.h): the death screen shows a countdown until the respawn button
// unlocks, and the server forces the respawn at the deadline.
#define MP3_RESPAWN_MIN_WAIT	3.0f	// seconds after death before the respawn button works
#define MP3_RESPAWN_MAX_WAIT	6.0f	// seconds after death when the server forces respawn

#define DEATHNOTICE_TOP		32

DeathNoticeItem rgDeathNoticeList[ MAX_DEATHNOTICES + 1 ];

cvar_t *cl_killsound;
cvar_t *cl_killsound_path;

int CHudDeathNotice :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_DeathNotice, DeathMsg );

	hud_deathnotice_time = CVAR_CREATE( "hud_deathnotice_time", "6", FCVAR_ARCHIVE );
	cl_killsound = CVAR_CREATE( "cl_killsound", "0", FCVAR_ARCHIVE );
	cl_killsound_path = CVAR_CREATE( "cl_killsound_path", "buttons/bell1.wav", FCVAR_ARCHIVE );
	m_iFlags = 0;

	return 1;
}


void CHudDeathNotice :: InitHUDData( void )
{
	memset( rgDeathNoticeList, 0, sizeof(rgDeathNoticeList) );

	m_szMyKillerName[0] = '\0';
	m_szMyKillerWeapon[0] = '\0';
	m_iMyKillerSprite = 0;
	m_bMySuicide = false;
	m_iMyLastKillerIdx = 0;
	m_iKilledAgainCount = 0;
	m_flMyDeathTime = 0.0f;
}


int CHudDeathNotice :: VidInit( void )
{
	m_HUD_d_skull = gHUD.GetSpriteIndex( "d_skull" );
	m_HUD_d_headshot = gHUD.GetSpriteIndex("d_headshot");

	return 1;
}

// Max Payne 3 death screen: CRT scanline wash over the scene, clean letterbox bands, plus the
// left-side stack (red "KILLED BY" chip, killer name, weapon icon + name) while the local
// player is dead. Returns true when shown (the kill feed is hidden underneath it).
bool CHudDeathNotice :: DrawDeathScreen( void )
{
	static cvar_t *cl_hud_mp3 = NULL;
	if( !cl_hud_mp3 ) cl_hud_mp3 = gEngfuncs.pfnRegisterVariable( "cl_hud_mp3", "1", FCVAR_ARCHIVE );
	if( !cl_hud_mp3->value || !gMp3Text.Ready() )
		return false;
	if( gHUD.m_Health.m_iHealth > 0 || m_iKilledAgainCount <= 0 )
		return false; // alive, or no recorded death yet

	// True grayscale via the GL post-process when available (the chips drawn after this keep
	// their color, like MP3 where the red KILLED BY chip is the only saturated thing on
	// screen); otherwise approximate with a gray wash. Scanlines go on top either way.
	if( !Mp3PostFX_GrayscaleScreen() )
		FillRGBABlend( 0, 0, ScreenWidth, ScreenHeight, 200, 200, 200, 28 );
	for( int sl = 0; sl < ScreenHeight; sl += 3 )
		FillRGBABlend( 0, sl, ScreenWidth, 1, 0, 0, 0, 110 );

	// MP3 letterbox: clean opaque bands (the classic spectator bars + their info are
	// suppressed in spectator_gui.cpp while the MP3 HUD is on)
	int bandTop = YRES( 8 );
	int bandBot = YRES( 20 );
	FillRGBABlend( 0, 0, ScreenWidth, bandTop, 0, 0, 0, 255 );
	FillRGBABlend( 0, ScreenHeight - bandBot, ScreenWidth, bandBot, 0, 0, 0, 255 );

	int x = XRES( 24 );
	int y = (int)( ScreenHeight * 0.55f );

	// title: black text on the red chip (roomy padding like the reference)
	const char *title = m_bMySuicide ? "YOU DIED"
	                  : ( m_iKilledAgainCount >= 2 ? "KILLED AGAIN BY" : "KILLED BY" );
	int H1 = YRES( 15 );
	int p1x = H1, p1y = H1 / 3;
	int tw = gMp3Text.StringWidthBig( title, H1 );
	FillRGBABlend( x - p1x, y - p1y, tw + 2 * p1x, H1 + 2 * p1y, MP3_RED, 255 );
	gMp3Text.DrawStringBig( x, y + H1, H1, title, MP3_BLACK, 255 );
	y += H1 + 2 * p1y + YRES( 4 );

	if( !m_bMySuicide && m_szMyKillerName[0] )
	{
		// killer name: red on black
		int H2 = YRES( 10 );
		int p2x = ( H2 * 2 ) / 3, p2y = H2 / 3;
		tw = gMp3Text.StringWidthBig( m_szMyKillerName, H2 );
		FillRGBABlend( x - p2x, y - p2y, tw + 2 * p2x, H2 + 2 * p2y, MP3_BLACK, 255 );
		gMp3Text.DrawStringBig( x, y + H2, H2, m_szMyKillerName, MP3_RED, 255 );
		y += H2 + 2 * p2y + YRES( 10 );

		// weapon: gray d_* sprite + white name on a black chip
		int H3 = YRES( 8 );
		int p3x = H3;
		int sprW = 0, sprH = 0;
		if( m_iMyKillerSprite )
		{
			sprW = gHUD.GetSpriteRect( m_iMyKillerSprite ).Width();
			sprH = gHUD.GetSpriteRect( m_iMyKillerSprite ).Height();
		}
		tw = gMp3Text.StringWidthBig( m_szMyKillerWeapon, H3 );
		int chipH = max( H3 * 2 + YRES( 2 ), sprH + YRES( 6 ) );
		int chipW = p3x + sprW + ( sprW ? XRES( 10 ) : 0 ) + tw + p3x;
		FillRGBABlend( x - p3x, y, chipW, chipH, MP3_BLACK, 255 );
		int cx = x;
		if( m_iMyKillerSprite )
		{
			SPR_Set( gHUD.GetSprite( m_iMyKillerSprite ), 190, 190, 190 );
			SPR_DrawAdditive( 0, cx, y + ( chipH - sprH ) / 2, &gHUD.GetSpriteRect( m_iMyKillerSprite ) );
			cx += sprW + XRES( 10 );
		}
		gMp3Text.DrawStringBig( cx, y + ( chipH + H3 ) / 2, H3, m_szMyKillerWeapon, MP3_WHITE, 255 );
	}

	// prompts inside the bottom band, right-aligned (MP3 style). The respawn prompt counts
	// down while the button is locked (server enforces the same window, see the ReGameDLL
	// fork's MP3_RESPAWN_* constants) and switches to the button hint once it unlocks.
	{
		int Hh = YRES( 8 );
		int hy = ScreenHeight - ( bandBot - Hh ) / 2; // baseline centered in the band
		int hx = ScreenWidth - XRES( 16 );
		int w2 = gMp3Text.StringWidthBig( "VIEW SCORES  TAB", Hh );
		gMp3Text.DrawStringBig( hx - w2, hy, Hh, "VIEW SCORES  TAB", MP3_WHITE, 235 );
		hx -= w2 + XRES( 24 );

		float elapsed = gHUD.m_flTime - m_flMyDeathTime;
		if( elapsed >= 0.0f && elapsed < MP3_RESPAWN_MIN_WAIT )
		{
			// locked: show the seconds left until the respawn button unlocks
			int secsLeft = (int)( MP3_RESPAWN_MIN_WAIT - elapsed ) + 1;
			char cnt[24];
			snprintf( cnt, sizeof( cnt ), "RESPAWN %d", secsLeft );
			int w1 = gMp3Text.StringWidthBig( cnt, Hh );
			gMp3Text.DrawStringBig( hx - w1, hy, Hh, cnt, MP3_GRAY_DK, 235 );
		}
		else
		{
			int w1 = gMp3Text.StringWidthBig( "RESPAWN  FIRE", Hh );
			gMp3Text.DrawStringBig( hx - w1, hy, Hh, "RESPAWN  FIRE", MP3_WHITE, 235 );
		}
	}

	return true;
}

int CHudDeathNotice :: Draw( float flTime )
{
	int x, y, r, g, b, i;

	if( DrawDeathScreen() )
		return 1; // the death screen owns the view; the kill feed would clutter the panel

	for( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;  // we've gone through them all

		if ( rgDeathNoticeList[i].flDisplayTime < flTime )
		{ // display time has expired
			// remove the current item from the list
			memmove( &rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES - i) );
			i--;  // continue on the next item;  stop the counter getting incremented
			continue;
		}

		rgDeathNoticeList[i].flDisplayTime = min( rgDeathNoticeList[i].flDisplayTime, flTime + DEATHNOTICE_DISPLAY_TIME );

		// Hide when scoreboard drawing. It will break triapi
		//if ( gViewPort && gViewPort->AllowedToPrintText() )
		//if ( !gHUD.m_iNoConsolePrint )
		{
			// Max Payne 3 kill feed: two lines in the MP3 text font, stacked above the radar.
			//   line 1 (white):  killer
			//   line 2:          weapon (gray)  victim (red)
			// NOTE: CS sends no assist info in the death message, so the assister can't be shown
			// on line 1 without server-side changes.
			char weap[40];
			strlcpy( weap, rgDeathNoticeList[i].szWeapon, sizeof( weap ) );
			if ( rgDeathNoticeList[i].iHeadShotId )
				strlcat( weap, " (HS)", sizeof( weap ) );

			int wr = rgDeathNoticeList[i].bTeamKill ? 30  : MP3_GRAY_DK_R;
			int wg = rgDeathNoticeList[i].bTeamKill ? 230 : MP3_GRAY_DK_G;
			int wb = rgDeathNoticeList[i].bTeamKill ? 30  : MP3_GRAY_DK_B;
			x = XRES( 8 );

			if ( gMp3Text.Ready() )
			{
				int H = YRES( 11 ) / 2;             // cap height (half of before)
				int lineH = (int)( H * 1.7f );      // line spacing (room for descenders)
				int blockH = 2 * lineH + YRES( 6 );
				int blockBottom = gHUD.m_Radar.RadarTopY() - YRES( 10 ) - i * blockH;
				int base2 = blockBottom - YRES( 5 );   // weapon + victim baseline
				int base1 = base2 - lineH;             // killer baseline

				if ( !rgDeathNoticeList[i].bSuicide )
					gMp3Text.DrawStringOutlined( x, base1, H, rgDeathNoticeList[i].szKiller, MP3_WHITE, 255 );

				int vx = gMp3Text.DrawStringOutlined( x, base2, H, weap, wr, wg, wb, 255 );
				vx += XRES( 6 );
				if ( !rgDeathNoticeList[i].bNonPlayerKill )
					gMp3Text.DrawStringOutlined( vx, base2, H, rgDeathNoticeList[i].szVictim, MP3_RED, 255 );
			}
			else
			{
				// fallback: CS HUD font
				float sc = 1.4f;
				int lineH = (int)( gHUD.m_iFontHeight * sc ) + YRES( 2 );
				int blockH = 2 * lineH + YRES( 6 );
				int blockBottom = gHUD.m_Radar.RadarTopY() - YRES( 12 ) - i * blockH;
				int y1 = blockBottom - 2 * lineH;
				int y2 = blockBottom - lineH;
				if ( !rgDeathNoticeList[i].bSuicide )
					DrawUtils::DrawHudString( x, y1, ScreenWidth, rgDeathNoticeList[i].szKiller, 255, 255, 255, sc );
				int vx = DrawUtils::DrawHudString( x, y2, ScreenWidth, weap, wr, wg, wb, sc );
				vx += XRES( 6 );
				if ( !rgDeathNoticeList[i].bNonPlayerKill )
					DrawUtils::DrawHudString( vx, y2, ScreenWidth, rgDeathNoticeList[i].szVictim, 235, 60, 60, sc );
			}
		}
	}

	if( i == 0 )
		m_iFlags &= ~HUD_DRAW; // disable hud item

	return 1;
}

// This message handler may be better off elsewhere
int CHudDeathNotice :: MsgFunc_DeathMsg( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );

	int killer = reader.ReadByte();
	int victim = reader.ReadByte();
	int headshot = reader.ReadByte();

	char killedwith[32];
	strlcpy( killedwith, "d_", sizeof( killedwith ) );
	strlcat( killedwith, reader.ReadString(), sizeof( killedwith ) );

	//if (gViewPort)
	//	gViewPort->DeathMsg( killer, victim );
	gHUD.m_Scoreboard.DeathMsg( killer, victim );

	gHUD.m_Spectator.DeathMessage(victim);
	int i;
	for ( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;
	}
	if ( i == MAX_DEATHNOTICES )
	{ // move the rest of the list forward to make room for this item
		memmove( rgDeathNoticeList, rgDeathNoticeList+1, sizeof(DeathNoticeItem) * MAX_DEATHNOTICES );
		i = MAX_DEATHNOTICES - 1;
	}

	//if (gViewPort)
		//gViewPort->GetAllPlayersInfo();
	gHUD.m_Scoreboard.GetAllPlayersInfo();

	// Get the Killer's name
	const char *killer_name = NULL;
	bool killer_this_player = false;
	if ( killer >= 1 && killer <= MAX_PLAYERS )
	{
		killer_name = g_PlayerInfoList[killer].name;
		killer_this_player = g_PlayerInfoList[killer].thisplayer;
	}

	if ( !killer_name )
	{
		killer_name = "";
		rgDeathNoticeList[i].szKiller[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].KillerColor = GetClientColor( killer );
		strlcpy( rgDeathNoticeList[i].szKiller, killer_name, sizeof( rgDeathNoticeList[i].szKiller ) );
	}

	// Get the Victim's name
	const char *victim_name = NULL;

	if ( victim >= 1 && victim <= MAX_PLAYERS )
		victim_name = g_PlayerInfoList[ victim ].name;

	if ( !victim_name )
	{
		victim_name = "";
		rgDeathNoticeList[i].szVictim[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].VictimColor = GetClientColor( victim );
		strlcpy( rgDeathNoticeList[i].szVictim, victim_name, sizeof( rgDeathNoticeList[i].szVictim ) );
	}

	// Is it a non-player object kill?
	// If victim is 255, the killer killed a specific, non-player object (like a sentrygun)
	if( victim == 255 )
	{
		rgDeathNoticeList[i].bNonPlayerKill = true;

		// Store the object's name in the Victim slot (skip the d_ bit)
		strlcpy( rgDeathNoticeList[i].szVictim, killedwith+2, sizeof( rgDeathNoticeList[i].szVictim ) );
	}
	else
	{
		if ( killer == victim || killer == 0 )
			rgDeathNoticeList[i].bSuicide = true;

		if ( !strncmp( killedwith, "d_teammate", sizeof(killedwith)  ) )
			rgDeathNoticeList[i].bTeamKill = true;
	}

	rgDeathNoticeList[i].iHeadShotId = headshot;

	// Find the sprite in the list
	int spr = gHUD.GetSpriteIndex( killedwith );

	rgDeathNoticeList[i].iId = spr;

	// plain weapon name (strip the "d_" prefix) for the MP3 text kill feed
	strlcpy( rgDeathNoticeList[i].szWeapon, killedwith + 2, sizeof( rgDeathNoticeList[i].szWeapon ) );

	rgDeathNoticeList[i].flDisplayTime = gHUD.m_flTime + hud_deathnotice_time->value;

	// Max Payne 3 kill banners next to the score panel: friendly kills (you/teammates) on the
	// left, enemy kills on the right; each shows "KILL +N" then the killer's name. (thisplayer
	// can lag behind in bot games, so also match the killer index against the local player
	// number directly.) In FFA everyone but you counts as the enemy side.
	{
		static cvar_t *ffa = NULL;
		if( !ffa ) ffa = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );
		bool isFFA = ffa && ffa->value != 0.0f;

		bool killerIsMe = killer_this_player || killer == gHUD.m_Scoreboard.m_iPlayerNum;
		bool killerIsTeammate = !isFFA && killer >= 1 && killer <= MAX_PLAYERS && g_iTeamNumber != 0 &&
			g_PlayerExtraInfo[killer].teamnumber == g_iTeamNumber;
		if( killer >= 1 && killer <= MAX_PLAYERS &&
		    !rgDeathNoticeList[i].bNonPlayerKill &&
		    !rgDeathNoticeList[i].bSuicide &&
		    !rgDeathNoticeList[i].bTeamKill )
		{
			gHUD.m_Timer.NotifyTeamKillScored( !( killerIsMe || killerIsTeammate ), killer_name );
			if( killerIsMe )
				gHUD.m_Ammo.NotifyKillConfirm(); // crosshair flashes the MP3 kill X
		}
	}

	// Max Payne 3 death screen: remember who killed ME and with what (drawn while dead)
	if( victim >= 1 && victim <= MAX_PLAYERS &&
	    ( g_PlayerInfoList[victim].thisplayer || victim == gHUD.m_Scoreboard.m_iPlayerNum ) &&
	    !rgDeathNoticeList[i].bNonPlayerKill )
	{
		m_bMySuicide = rgDeathNoticeList[i].bSuicide;
		snprintf( m_szMyKillerName, sizeof( m_szMyKillerName ), "%s", killer_name ? killer_name : "" );
		snprintf( m_szMyKillerWeapon, sizeof( m_szMyKillerWeapon ), "%s", rgDeathNoticeList[i].szWeapon );
		m_iMyKillerSprite = rgDeathNoticeList[i].iId;
		m_flMyDeathTime = gHUD.m_flTime;
		// consecutive deaths to the same player -> "KILLED AGAIN BY"
		if( !m_bMySuicide && killer == m_iMyLastKillerIdx )
			m_iKilledAgainCount++;
		else
			m_iKilledAgainCount = 1;
		m_iMyLastKillerIdx = m_bMySuicide ? 0 : killer;
	}

	// Play kill sound
	if ((killer_this_player || g_iUser2 == killer) &&
		!rgDeathNoticeList[i].bNonPlayerKill &&
		!rgDeathNoticeList[i].bSuicide &&
		cl_killsound->value > 0.0f)
	{
		PlaySound(cl_killsound_path->string, cl_killsound->value);
	}

	// MP3 HUD: skip the console death notices -- the engine echoes them as notify text in the
	// top-left corner, and the MP3 kill feed already shows the same info.
	static cvar_t *cl_hud_mp3 = NULL;
	if( !cl_hud_mp3 ) cl_hud_mp3 = gEngfuncs.pfnRegisterVariable( "cl_hud_mp3", "1", FCVAR_ARCHIVE );
	if( cl_hud_mp3->value )
		return 1;

	if (rgDeathNoticeList[i].bNonPlayerKill)
	{
		ConsolePrint( rgDeathNoticeList[i].szKiller );
		ConsolePrint( " killed a " );
		ConsolePrint( rgDeathNoticeList[i].szVictim );
		ConsolePrint( "\n" );
	}
	else
	{
		// record the death notice in the console
		if ( rgDeathNoticeList[i].bSuicide )
		{
			ConsolePrint( rgDeathNoticeList[i].szVictim );

			if ( !strncmp( killedwith, "d_world", sizeof(killedwith)  ) )
			{
				ConsolePrint( " died" );
			}
			else
			{
				ConsolePrint( " killed self" );
			}
		}
		else if ( rgDeathNoticeList[i].bTeamKill )
		{
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed his teammate " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}
		else
		{
			if( headshot )
				ConsolePrint( "*** ");
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}

		if ( *killedwith && (*killedwith > 13 ) && strncmp( killedwith, "d_world", sizeof(killedwith) ) && !rgDeathNoticeList[i].bTeamKill )
		{
			if ( headshot )
				ConsolePrint(" with a headshot from ");
			else
				ConsolePrint(" with ");

			ConsolePrint( killedwith+2 ); // skip over the "d_" part
		}

		if( headshot ) ConsolePrint( " ***");
		ConsolePrint( "\n" );
	}

	return 1;
}

