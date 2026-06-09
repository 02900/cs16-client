/*
timer.cpp -- HUD timer, progress bars, etc
Copyright (C) 2015-2016 a1batross
This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, as a special exception, the author gives permission to
link the code of this program with the Half-Life Game Engine ("HL
Engine") and Modified Game Libraries ("MODs") developed by Valve,
L.L.C ("Valve").  You must obey the GNU General Public License in all
respects for all of the code used other than the HL Engine and MODs
from Valve.  If you modify this file, you may extend this exception
to your version of the file, but you are not obligated to do so.  If
you do not wish to do so, delete this exception statement from your
version.
*/

#include "stdio.h"
#include "stdlib.h"
#include "math.h"

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "vgui_parser.h"
#include <string.h>
#include "draw_util.h"
#include "mp3font.h"

// Max Payne 3 style chip behind a HUD number cluster (score chips solid black, timer light gray)
static void Chip( int x, int y, int w, int h, int cr, int cg, int cb, int ca )
{
	FillRGBA( x, y, w, h, cr, cg, cb, ca );
}

int CHudTimer::Init()
{
	HOOK_MESSAGE( gHUD.m_Timer, RoundTime );
	HOOK_MESSAGE( gHUD.m_Timer, ShowTimer );
	m_iFlags = 0;
	m_bPanicColorChange = false;
	gHUD.AddHudElem(this);
	return 1;
}

int CHudTimer::VidInit()
{
	m_HUD_timer = gHUD.GetSpriteIndex( "stopwatch" );
	gMp3Font.Load();
	return 1;
}

int CHudTimer::Draw( float fTime )
{
	if( gHUD.m_iHideHUDDisplay & ( HIDEHUD_TIMER | HIDEHUD_ALL ) )
		return 1;

	if (!(gHUD.m_iWeaponBits & (1<<(WEAPON_SUIT)) ))
		return 1;
	int r, g, b;
	// time must be positive
	int minutes = max( 0, (int)( m_iTime + m_fStartTime - gHUD.m_flTime ) / 60);
	int seconds = max( 0, (int)( m_iTime + m_fStartTime - gHUD.m_flTime ) - (minutes * 60));

	if( minutes * 60 + seconds > 20 )
	{
		DrawUtils::UnpackRGB( r, g, b, gHUD.m_iDefaultHUDColor );
	}
	else
	{
		m_flPanicTime += gHUD.m_flTimeDelta;
		// add 0.1 sec, so it's not flicker fast
		if( m_flPanicTime > ((float)seconds / 40.0f) + 0.1f)
		{
			m_flPanicTime = 0;
			m_bPanicColorChange = !m_bPanicColorChange;
		}
		DrawUtils::UnpackRGB( r, g, b, m_bPanicColorChange ? gHUD.m_iDefaultHUDColor : RGB_REDISH );
	}

	// --- Max Payne 3 number font + chips: score (white) | timer | score (red) ---
	static cvar_t *cl_hud_mp3 = NULL;
	if( !cl_hud_mp3 ) cl_hud_mp3 = gEngfuncs.pfnRegisterVariable( "cl_hud_mp3", "1", FCVAR_ARCHIVE );

	if( gMp3Font.Ready() && cl_hud_mp3->value )
	{
		int H    = YRES( 20 );
		int colW = H / 4;
		int padX = H / 4, padY = H / 6;
		int gapC = H / 5;            // gap between chips (MP3 chips sit snug together)
		int gd   = max( 1, H / 12 ); // inter-digit gap
		int ty   = YRES( 10 );

		int s10 = seconds / 10, s1 = seconds % 10;
		int minW = gMp3Font.NumberWidth( minutes, H );
		int secW = gMp3Font.DigitWidth( s10, H ) + gd + gMp3Font.DigitWidth( s1, H );
		int timerW = minW + colW + secW;

		int tx0 = ScreenWidth / 2 - timerW / 2;

		// timer chip is LIGHT GRAY with dark digits (Max Payne 3); digits turn red when time is low
		bool panic = ( minutes * 60 + seconds ) <= 20;
		int tcr = panic ? 210 : 35;
		int tcg = panic ? 40  : 35;
		int tcb = panic ? 40  : 35;
		Chip( tx0 - padX, ty - padY, timerW + 2 * padX, H + 2 * padY, 205, 205, 205, 255 );
		int tx = tx0;
		gMp3Font.DrawNumber( tx, ty, H, minutes, tcr, tcg, tcb, 255 );
		tx += minW;
		int ds = max( 2, H / 8 );
		FillRGBA( tx + colW / 2 - ds / 2, ty + (int)( H * 0.30f ), ds, ds, tcr, tcg, tcb, 255 );
		FillRGBA( tx + colW / 2 - ds / 2, ty + (int)( H * 0.58f ), ds, ds, tcr, tcg, tcb, 255 );
		tx += colW;
		gMp3Font.DrawDigit( tx, ty, H, s10, tcr, tcg, tcb, 255 );
		tx += gMp3Font.DigitWidth( s10, H ) + gd;
		gMp3Font.DrawDigit( tx, ty, H, s1, tcr, tcg, tcb, 255 );

		// scores: FFA -> your frags (left only); teams -> your team (left) vs enemy (right)
		static cvar_t *ffa = NULL;
		if( !ffa ) ffa = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );
		bool isFFA = ffa && ffa->value != 0.0f;

		int leftScore = 0, rightScore = 0;
		bool hasRight = false;
		if( isFFA )
		{
			leftScore = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].frags;
		}
		else
		{
			for( int i = 1; i <= MAX_PLAYERS; i++ )
			{
				int tn = g_PlayerExtraInfo[i].teamnumber;
				if( tn == g_iTeamNumber )      leftScore  += g_PlayerExtraInfo[i].frags;
				else if( tn == 1 || tn == 2 )  rightScore += g_PlayerExtraInfo[i].frags;
			}
			hasRight = true;
		}

		int lw = gMp3Font.NumberWidth( leftScore, H );
		int lChipR = tx0 - padX - gapC;
		Chip( lChipR - lw - 2 * padX, ty - padY, lw + 2 * padX, H + 2 * padY, 0, 0, 0, 255 );
		gMp3Font.DrawNumber( lChipR - lw - padX, ty, H, leftScore, 255, 255, 255, 255 );

		if( hasRight )
		{
			int rw = gMp3Font.NumberWidth( rightScore, H );
			int rChipL = tx0 + timerW + padX + gapC;
			Chip( rChipL, ty - padY, rw + 2 * padX, H + 2 * padY, 0, 0, 0, 255 );
			gMp3Font.DrawNumber( rChipL + padX, ty, H, rightScore, 245, 70, 70, 255 );
		}

		return 1;
	}

	DrawUtils::ScaleColors( r, g, b, MIN_ALPHA );

	int iWatchWidth = gHUD.GetSpriteRect(m_HUD_timer).Width();
	int iDigitWidth = gHUD.GetSpriteRect(gHUD.m_HUD_number_0).Width();
	int iColonWidth = iDigitWidth / 2;

	// Always reserve space for 2 digits for minutes to keep layout consistent
	int totalWidth = iWatchWidth + 2 * iDigitWidth + iColonWidth + 2 * iDigitWidth;

	int x = (ScreenWidth - totalWidth) / 2;
	int xStart = x;                  // left edge of the clock (for the score on the left)
	int y = YRES( 10 );              // top-center, Max Payne 3 style

	SPR_Set(gHUD.GetSprite(m_HUD_timer), r, g, b);
	SPR_DrawAdditive(0, x, y, &gHUD.GetSpriteRect(m_HUD_timer));
	x += iWatchWidth;

	if (minutes < 10)
		// Shift x to the right by one digit width to reserve space for the leading zero, then draw 1 digit without the leading zero
		x = DrawUtils::DrawHudNumber2(x + iDigitWidth, y, false, 1, minutes, r, g, b);
	else
		// Draw 2 digits, including the leading zero if needed
		x = DrawUtils::DrawHudNumber2(x, y, true, 2, minutes, r, g, b);

	// Draw colon (":")
	FillRGBA(x + iColonWidth / 2, y + gHUD.m_iFontHeight / 4, 2, 2, r, g, b, 100);
	FillRGBA(x + iColonWidth / 2, y + gHUD.m_iFontHeight - gHUD.m_iFontHeight / 4, 2, 2, r, g, b, 100);
	x += iColonWidth;

	m_right = DrawUtils::DrawHudNumber2(x, y, true, 2, seconds, r, g, b);

	// --- score next to the timer (Max Payne 3 style) ---
	// FFA: your own frags on the left. Teams: your team score (left) and the enemy's (right, red).
	static cvar_t *aa_ffa = NULL;
	if( !aa_ffa ) aa_ffa = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );

	int sr, sg, sb;
	DrawUtils::UnpackRGB( sr, sg, sb, gHUD.m_iDefaultHUDColor );
	int gap = iDigitWidth;

	if( aa_ffa && aa_ffa->value != 0.0f )
	{
		int frags = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].frags;
		int w = DrawUtils::GetNumWidth( frags, DHN_DRAWZERO );
		DrawUtils::DrawHudNumber2( xStart - gap - w * iDigitWidth, y, true, w, frags, sr, sg, sb );
	}
	else
	{
		// Sum player frags per team every frame. g_TeamInfo[].frags is only refreshed while the
		// scoreboard (Tab) is open, so it can't be used here; g_PlayerExtraInfo[].frags updates live.
		int myScore = 0, enemyScore = 0;
		for( int i = 1; i <= MAX_PLAYERS; i++ )
		{
			int tn = g_PlayerExtraInfo[i].teamnumber;
			if( tn == g_iTeamNumber )            myScore += g_PlayerExtraInfo[i].frags;
			else if( tn == 1 || tn == 2 )        enemyScore += g_PlayerExtraInfo[i].frags;
		}
		int wl = DrawUtils::GetNumWidth( myScore, DHN_DRAWZERO );
		DrawUtils::DrawHudNumber2( xStart - gap - wl * iDigitWidth, y, true, wl, myScore, sr, sg, sb );
		DrawUtils::DrawHudNumber2( m_right + gap, y, true, DrawUtils::GetNumWidth( enemyScore, DHN_DRAWZERO ), enemyScore, 250, 60, 60 );
	}

	return 1;
}

int CHudTimer::MsgFunc_RoundTime(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );
	m_iTime = reader.ReadShort();
	m_fStartTime = gHUD.m_flTime;
	m_iFlags = HUD_DRAW;
	return 1;
}

int CHudTimer::MsgFunc_ShowTimer(const char *pszName, int iSize, void *pbuf)
{
	m_iFlags = HUD_DRAW;
	return 1;
}

#define UPDATE_BOTPROGRESS 0
#define CREATE_BOTPROGRESS 1
#define REMOVE_BOTPROGRESS 2

int CHudProgressBar::Init()
{
	HOOK_MESSAGE( gHUD.m_ProgressBar, BarTime );
	HOOK_MESSAGE( gHUD.m_ProgressBar, BarTime2 );
	HOOK_MESSAGE( gHUD.m_ProgressBar, BotProgress );
	Reset( );
	gHUD.AddHudElem(this);
	return 1;
}

int CHudProgressBar::VidInit()
{
	return 1;
}

void CHudProgressBar::Reset( void )
{
	m_iFlags = 0;
	m_szLocalizedHeader = NULL;
	m_szHeader[0] = '\0';
	m_fStartTime = m_fPercent = 0.0f;
}

int CHudProgressBar::Draw( float flTime )
{
	// allow only 0.0..1.0
	if( (m_fPercent < 0.0f) || (m_fPercent > 1.0f) )
	{
		m_iFlags = 0;
		m_fPercent = 0.0f;
		return 1;
	}

	if( m_szLocalizedHeader && m_szLocalizedHeader[0] )
	{
		int r, g, b;
		DrawUtils::UnpackRGB( r, g, b, gHUD.m_iDefaultHUDColor );
		DrawUtils::DrawHudString( ScreenWidth / 4, ScreenHeight / 2, ScreenWidth, (char*)m_szLocalizedHeader, r, g, b );

		DrawUtils::DrawRectangle( ScreenWidth/ 4, ScreenHeight / 2 + gHUD.GetCharHeight(), ScreenWidth/2, ScreenHeight/30 );
		FillRGBA( ScreenWidth/4+2, ScreenHeight/2 + gHUD.GetCharHeight() + 2, m_fPercent * (ScreenWidth/2-4), ScreenHeight/30-4, 255, 140, 0, 255 );
		return 1;
	}

	// prevent SIGFPE
	if( m_iDuration != 0.0f )
	{
		m_fPercent = ((flTime - m_fStartTime) / m_iDuration);
	}
	else
	{
		m_fPercent = 0.0f;
		m_iFlags = 0;
		return 1;
	}

	DrawUtils::DrawRectangle( ScreenWidth/4, ScreenHeight*2/3, ScreenWidth/2, 10 );
	FillRGBA( ScreenWidth/4+2, ScreenHeight*2/3+2, m_fPercent * (ScreenWidth/2-4), 6, 255, 140, 0, 255 );

	return 1;
}

int CHudProgressBar::MsgFunc_BarTime(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	m_iDuration = reader.ReadShort();
	m_fPercent = 0.0f;

	m_fStartTime = gHUD.m_flTime;

	m_iFlags = HUD_DRAW;
	return 1;
}

int CHudProgressBar::MsgFunc_BarTime2(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	m_iDuration = reader.ReadShort();
	m_fPercent = m_iDuration * (float)reader.ReadShort() / 100.0f;

	m_fStartTime = gHUD.m_flTime;

	m_iFlags = HUD_DRAW;
	return 1;
}

int CHudProgressBar::MsgFunc_BotProgress(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );
	m_iDuration = 0.0f; // don't update our progress bar
	m_iFlags = HUD_DRAW;

	float fNewPercent;
	int flag = reader.ReadByte();
	switch( flag )
	{
	case CREATE_BOTPROGRESS:
		m_fPercent = 0.0f;
		break;
	case UPDATE_BOTPROGRESS:
		fNewPercent = (float)reader.ReadByte() / 100.0f;
		// cs behavior:
		// just don't decrease percent values
		if( m_fPercent < fNewPercent )
		{
			m_fPercent = fNewPercent;
		}
		strncpy(m_szHeader, reader.ReadString(), sizeof(m_szHeader));
		m_szHeader[sizeof(m_szHeader)-1] = 0;

		if( m_szHeader[0] == '#' )
			m_szLocalizedHeader = Localize(m_szHeader + 1);
		else
			m_szLocalizedHeader = m_szHeader;
		break;
	case REMOVE_BOTPROGRESS:
	default:
		m_fPercent = 0.0f;
		m_szHeader[0] = '\0';
		m_iFlags = 0;
		m_szLocalizedHeader = NULL;
		break;
	}

	return 1;
}
