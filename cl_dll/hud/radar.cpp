/*
radar.cpp - Radar
Copyright (C) 2016 a1batross

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

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "draw_util.h"
#include "mp3palette.h"
#include "triangleapi.h"
#include "vgui_parser.h"
#include "hud_spectator.h"
#include "const.h"
#include "com_model.h"
#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

// Max Payne 3 minimap zoom: higher = closer. Shared by the player dots and the overview tiles
// so they always stay aligned. World units per radar pixel = 32 / zoom.
static cvar_t *cl_radar_zoom = NULL;
static float RadarScale()
{
	float z = ( cl_radar_zoom && cl_radar_zoom->value > 0.1f ) ? cl_radar_zoom->value : 1.0f;
	return 32.0f / z;
}

// Overall radar size multiplier over the native CS radar sprite (Max Payne 3 has a big radar).
static cvar_t *cl_radar_size = NULL;
static float RadarSizeMul()
{
	float s = ( cl_radar_size && cl_radar_size->value > 0.25f ) ? cl_radar_size->value : 1.0f;
	if( s > 4.0f ) s = 4.0f;
	return s;
}

static byte	r_RadarCross[8][8] =
{
{1,1,0,0,0,0,1,1},
{1,1,1,0,0,1,1,1},
{0,1,1,1,1,1,1,0},
{0,0,1,1,1,1,0,0},
{0,0,1,1,1,1,0,0},
{0,1,1,1,1,1,1,0},
{1,1,1,0,0,1,1,1},
{1,1,0,0,0,0,1,1}
};

static byte	r_RadarT[8][8] =
{
{1,1,1,1,1,1,1,1},
{1,1,1,1,1,1,1,1},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0}
};

static byte	r_RadarFlippedT[8][8] =
{
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{0,0,0,1,1,0,0,0},
{1,1,1,1,1,1,1,1},
{1,1,1,1,1,1,1,1}
};

#define BLOCK_SIZE_MAX 1024

static byte	data2D[BLOCK_SIZE_MAX*4];	// intermediate texbuffer

int CHudRadar::Init()
{
	HOOK_MESSAGE( gHUD.m_Radar, Radar );
	HOOK_COMMAND( gHUD.m_Radar, "drawradar", ShowRadar );
	HOOK_COMMAND( gHUD.m_Radar, "hideradar", HideRadar );
	HOOK_MESSAGE( gHUD.m_Radar, HostageK );
	HOOK_MESSAGE( gHUD.m_Radar, HostagePos );
	HOOK_MESSAGE( gHUD.m_Radar, BombDrop );
	HOOK_MESSAGE( gHUD.m_Radar, BombPickup );
	HOOK_MESSAGE( gHUD.m_Radar, Location );

	m_iFlags = HUD_DRAW;

	cl_radartype = CVAR_CREATE( "cl_radartype", "0", FCVAR_ARCHIVE );
	cl_radar_zoom = CVAR_CREATE( "cl_radar_zoom", "2.5", FCVAR_ARCHIVE );
	cl_radar_size = CVAR_CREATE( "cl_radar_size", "1.8", FCVAR_ARCHIVE );

	bTexturesInitialized = bUseRenderAPI = false;

	gHUD.AddHudElem( this );
	return 1;
}

void CHudRadar::Reset()
{
	// make radar don't draw old players after new map
	for( int i = 0; i < 34; i++ )
	{
		g_PlayerExtraInfo[i].radarflashes = 0;

		if( i <= MAX_HOSTAGES ) g_HostageInfo[i].radarflashes = 0;
	}
}

static void Radar_InitBitmap( int w, int h, byte *buf )
{
	for( int x = 0; x < w; x++ )
	{
		for( int y = 0; y < h; y++ )
		{
			data2D[(y * 8 + x) * 4 + 0] = 255;
			data2D[(y * 8 + x) * 4 + 1] = 255;
			data2D[(y * 8 + x) * 4 + 2] = 255;
			data2D[(y * 8 + x) * 4 + 3] = buf[y*h + x]  * 255;
		}
	}
}

int CHudRadar::InitBuiltinTextures( void )
{
	texFlags_t defFlags = (texFlags_t)(TF_NOMIPMAP |TF_NEAREST | TF_CLAMP | TF_HAS_ALPHA);

	if( bTexturesInitialized )
		return 1;

	const struct
	{
		const char	*name;
		byte	*buf;
		int		*texnum;
		int		w, h;
		void	(*init)( int w, int h, byte *buf );
	}
	textures[] =
	{
	{ "radarT",		   (byte*)r_RadarT,        &hT,	       8, 8, Radar_InitBitmap },
	{ "radarcross",    (byte*)r_RadarCross,    &hCross,    8, 8, Radar_InitBitmap },
	{ "radarflippedT", (byte*)r_RadarFlippedT, &hFlippedT, 8, 8, Radar_InitBitmap }
	};
	size_t	i, num_builtin_textures = sizeof( textures ) / sizeof( textures[0] );

	for( i = 0; i < num_builtin_textures; i++ )
	{
		textures[i].init( textures[i].w, textures[i].h, textures[i].buf );
		*textures[i].texnum = gRenderAPI.GL_CreateTexture( textures[i].name, textures[i].w, textures[i].h, data2D, defFlags );
		if( *textures[i].texnum == 0 )
		{
			// it's maybe safer to leave texture render uninitialized and use classic fillrgba
			for( size_t j = 0; j < i; j++ )
			{
				gRenderAPI.GL_FreeTexture( *textures[j].texnum );
			}
			return 0;
		}
	}

	bTexturesInitialized = true;

	return 1;
}

void CHudRadar::Shutdown( void )
{
	// GL_FreeTexture( hDot ); engine inner texture
	if( bTexturesInitialized )
	{
		gRenderAPI.GL_FreeTexture( hT );
		gRenderAPI.GL_FreeTexture( hFlippedT );
		gRenderAPI.GL_FreeTexture( hCross );
	}
	if( m_iRingTex )   gRenderAPI.GL_FreeTexture( m_iRingTex );
	if( m_iPlayerTex ) gRenderAPI.GL_FreeTexture( m_iPlayerTex );
	if( m_iBlipTex )   gRenderAPI.GL_FreeTexture( m_iBlipTex );
	m_iRingTex = m_iPlayerTex = m_iBlipTex = 0;
}

void CHudRadar::InitHUDData( void )
{
	UserCmd_ShowRadar();
	Reset();
}

int CHudRadar::VidInit(void)
{
	bUseRenderAPI = g_iXash && InitBuiltinTextures();

	m_hRadar.SetSpriteByName( "radar" );
	m_hRadarOpaque.SetSpriteByName( "radaropaque" );
	iMaxRadius = (m_hRadar.rect.Width()) / 2.0f;

	// force the overview minimap to (re)load for the new map on next Draw
	m_pMiniMap = NULL;
	m_szMiniLevel[0] = 0;
	m_iRadarY = ScreenHeight; // placeholder until the first Draw computes the real anchor

	// Max Payne 3 radar art (imported via scripts/import_mp3_assets.py). Optional: if the
	// files aren't present GL_LoadTexture returns 0 and we fall back to procedural drawing.
	m_iRingTex = m_iPlayerTex = m_iBlipTex = 0;
	if( bUseRenderAPI )
	{
		texFlags_t f = (texFlags_t)( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA );
		m_iRingTex   = gRenderAPI.GL_LoadTexture( "gfx/mp3/radar_ring.png",   NULL, 0, f );
		m_iPlayerTex = gRenderAPI.GL_LoadTexture( "gfx/mp3/radar_player.png", NULL, 0, f );
		m_iBlipTex   = gRenderAPI.GL_LoadTexture( "gfx/mp3/radar_blip.png",   NULL, 0, f );
	}
	return 1;
}

void CHudRadar::UserCmd_HideRadar()
{
	m_iFlags &= ~HUD_DRAW;
}

void CHudRadar::UserCmd_ShowRadar()
{
	m_iFlags |= HUD_DRAW;
}

int CHudRadar::MsgFunc_Radar(const char *pszName,  int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );

	int index = reader.ReadByte();

	if( index < 1 || index > MAX_PLAYERS )
		return 1;

	g_PlayerExtraInfo[index].origin.x = reader.ReadCoord();
	g_PlayerExtraInfo[index].origin.y = reader.ReadCoord();
	g_PlayerExtraInfo[index].origin.z = reader.ReadCoord();
	return 1;
}

bool CHudRadar::FlashTime( float flTime, extra_player_info_t *pplayer )
{
	// radar flashing
	if( pplayer->radarflashes )
	{
		if( flTime > pplayer->radarflashtime )
		{
			pplayer->nextflash = !pplayer->nextflash;
			pplayer->radarflashtime += pplayer->radarflashtimedelta;
			pplayer->radarflashes--;
		}
	}
	else
	{
		return true;
	}

	return pplayer->nextflash;
}

bool CHudRadar::HostageFlashTime( float flTime, hostage_info_t *pplayer )
{
	// radar flashing
	if( pplayer->radarflashes )
	{
		if( flTime > pplayer->radarflashtime )
		{
			pplayer->nextflash = !pplayer->nextflash;
			pplayer->radarflashtime += pplayer->radarflashtimedelta;
			pplayer->radarflashes--;
		}
	}
	else
	{
		return false; // non-flashing hostage must be never drawn on radar!
	}

	return pplayer->nextflash;
}

void CHudRadar::DrawZAxis( Vector pos, int r, int g, int b, int a )
{
	const float diff = 128;

	if( pos.z > -diff && pos.z < diff )
	{
		DrawRadarDot( pos.x, pos.y, r, g, b, a );
	}
	else if( pos.z <= -diff )
	{
		// higher than player
		DrawT( pos.x, pos.y, r, g, b, a );
	}
	else
	{
		// lower than player
		DrawFlippedT( pos.x, pos.y, r, g, b, a );
	}
}

int CHudRadar::Draw(float flTime)
{
	if ( (gHUD.m_iHideHUDDisplay & HIDEHUD_HEALTH) ||
		 gEngfuncs.IsSpectateOnly() ||
		 !(gHUD.m_iWeaponBits & (1<<(WEAPON_SUIT))) ||
		 gHUD.m_fPlayerDead )
		return 1;

	int iTeamNumber = g_PlayerExtraInfo[ gHUD.m_Scoreboard.m_iPlayerNum ].teamnumber;
	int r, g, b;

	// Anchor the radar to the bottom-left corner (Max Payne 3 layout). All radar drawing
	// below is offset by (m_iRadarX, m_iRadarY). Leave a little room under it for the
	// location label.
	// Scale the whole radar (minimap, dots, ring, pointer all key off iMaxRadius).
	iMaxRadius = (int)( ( m_hRadar.rect.Width() / 2.0f ) * RadarSizeMul() );
	m_iRadarX = 0;
	m_iRadarY = ScreenHeight - 2 * iMaxRadius - YRES( 26 );

	// Max Payne 3: draw the level geometry minimap (gray walls) when the map has an overview;
	// otherwise fall back to the classic translucent radar circle.
	UpdateMiniMap();
	bool drewMini = DrawMiniMap();
	if( !drewMini )
	{
		if( cl_radartype->value )
		{
			SPR_Set(m_hRadarOpaque.spr, 200, 200, 200);
			SPR_DrawHoles(0, m_iRadarX, m_iRadarY, &m_hRadarOpaque.rect);
		}
		else
		{
			SPR_Set( m_hRadar.spr, 25, 75, 25 );
			SPR_DrawAdditive( 0, m_iRadarX, m_iRadarY, &m_hRadarOpaque.rect );
		}
	}

	if( bUseRenderAPI )
	{
		gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		gEngfuncs.pTriAPI->Brightness( 1 );
	}

	for(int i = 0; i < 33; i++)
	{
		// skip local player and dead players
		if( i == gHUD.m_Scoreboard.m_iPlayerNum || g_PlayerExtraInfo[i].dead )
			continue;

		// skip non-teammates
		if( g_PlayerExtraInfo[i].teamnumber != iTeamNumber )
			continue;

		// decide should player draw at this time. For flashing.
		// Always true for non-flashing players
		if( !FlashTime( flTime, &g_PlayerExtraInfo[i]) )
			continue;

		// player with C4 or VIP must be red
		if( g_PlayerExtraInfo[i].has_c4 || g_PlayerExtraInfo[i].vip )
		{
			DrawUtils::UnpackRGB( r, g, b, RGB_REDISH );
		}
		else
		{
			// white
			DrawUtils::UnpackRGB( r, g, b, RGB_WHITE );
		}

		// calc radar position
		Vector pos = WorldToRadar(gHUD.m_vecOrigin, g_PlayerExtraInfo[i].origin, gHUD.m_vecAngles);

		DrawZAxis( pos, r, g, b, 255 );
	}

	// Max Payne 3: also draw enemies as RED dots. The server doesn't send enemy positions
	// (the Radar message is teammates only), so we read entity origins for enemies that are
	// currently in our PVS (networked/visible), the same way the aim assist does.
	{
		static cvar_t *aa_ffa = NULL;
		if( !aa_ffa ) aa_ffa = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );
		bool ffa = aa_ffa && aa_ffa->value != 0.0f;
		cl_entity_t *localPlayer = gEngfuncs.GetLocalPlayer();

		for( int i = 1; i <= gEngfuncs.GetMaxClients(); i++ )
		{
			if( i == gHUD.m_Scoreboard.m_iPlayerNum || g_PlayerExtraInfo[i].dead )
				continue;
			// teammates are already drawn (white) from the Radar message
			if( !ffa && g_PlayerExtraInfo[i].teamnumber == iTeamNumber )
				continue;

			cl_entity_t *e = gEngfuncs.GetEntityByIndex( i );
			if( !e || !e->player || e->curstate.solid == SOLID_NOT )
				continue;
			if( localPlayer && e->curstate.messagenum != localPlayer->curstate.messagenum )
				continue; // not in our PVS this frame -> position unknown/stale

			Vector pos = WorldToRadar( gHUD.m_vecOrigin, e->curstate.origin, gHUD.m_vecAngles );
			DrawZAxis( pos, 255, 16, 16, 255 ); // red
		}
	}

	// Terrorist specific code( C4 Bomb )
	if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
	{
		if ( !g_PlayerExtraInfo[33].dead &&
			 g_PlayerExtraInfo[33].radarflashes &&
			 FlashTime( flTime, &g_PlayerExtraInfo[33] ))
		{
			Vector pos = WorldToRadar(gHUD.m_vecOrigin, g_PlayerExtraInfo[33].origin, gHUD.m_vecAngles);
			if( g_PlayerExtraInfo[33].playerclass ) // bomb planted
			{
				DrawCross( pos.x, pos.y, 255, 0, 0, 255 );
			}
			else
			{
				DrawZAxis( pos, 255, 0, 0, 255 );
			}
		}
	}
	// Counter-Terrorist specific code( hostages )
	else if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_CT )
	{
		// draw hostages for CT
		for( int i = 1; i <= MAX_HOSTAGES; i++ )
		{
			if( !HostageFlashTime( flTime, g_HostageInfo + i ) )
			{
				continue;
			}

			Vector pos = WorldToRadar(gHUD.m_vecOrigin, g_HostageInfo[i].origin, gHUD.m_vecAngles);
			if( g_HostageInfo[i].dead )
			{
				DrawZAxis( pos, 255, 0, 0, 255 );
			}
			else
			{
				DrawZAxis( pos, 4, 25, 110, 255 );
			}
		}
	}

	// Max Payne 3: circular frame around the minimap + a white pointer for the local player
	// at the center (the map is centered on the player and rotates around them).
	if( drewMini )
		DrawMiniMapFrame();
	DrawPlayerArrow();

	DrawPlayerLocation( m_iRadarY + 2 * iMaxRadius + 4 );

	return 0;
}

void CHudRadar::DrawPlayerLocation( int y )
{
	const char *szLocation = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].location;
	if( szLocation[0] )
	{
		// Localize the location string
		const char *szLocalizedLocation = Localize( szLocation );

		// don't draw unlocalized location
		if( szLocalizedLocation[0] == '#' )
			return;

		int x = (m_hRadarOpaque.rect.Width()) / 2;
		int len = DrawUtils::ConsoleStringLen( szLocalizedLocation );

		x = x - len / 2;
		if( x < 0 ) x = 0;

		DrawUtils::SetConsoleTextColor( g_ColorGreen[0], g_ColorGreen[1], g_ColorGreen[2] );
		DrawUtils::DrawConsoleString( x, y, szLocalizedLocation );
	}
}

inline void CHudRadar::DrawColoredTexture( int x, int y, int size, byte r, byte g, byte b, byte a, int texHandle )
{
	gRenderAPI.GL_Bind( 0, texHandle );

	// gEngfuncs.pTriAPI->Begin( TRI_QUADS );

	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );
	DrawUtils::Draw2DQuad( (m_iRadarX + iMaxRadius + x - size * 2) * gHUD.m_flScale,
						   (m_iRadarY + iMaxRadius + y - size * 2) * gHUD.m_flScale,
						   (m_iRadarX + iMaxRadius + x + size * 2) * gHUD.m_flScale,
						   (m_iRadarY + iMaxRadius + y + size * 2) * gHUD.m_flScale);
	
	// gEngfuncs.pTriAPI->End();
}


void CHudRadar::DrawRadarDot( int x, int y, int r, int g, int b, int a )
{
	// Max Payne 3 blip texture, tinted by team color (red enemy / white teammate).
	if( bUseRenderAPI && m_iBlipTex )
	{
		int cx = m_iRadarX + iMaxRadius + x;
		int cy = m_iRadarY + iMaxRadius + y;
		int bs = XRES( 3 );
		gRenderAPI.GL_Bind( 0, m_iBlipTex );
		gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		gEngfuncs.pTriAPI->Color4ub( r, g, b, a );
		DrawUtils::Draw2DQuad( cx - bs, cy - bs, cx + bs, cy + bs );
		gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
		return;
	}

	const int size = 1;
	if( bUseRenderAPI )
	{
		DrawColoredTexture( x, y, size, r, g, b, a, gHUD.m_WhiteTex );
	}
	else
	{
		FillRGBA(m_iRadarX + iMaxRadius + x - size*2, m_iRadarY + iMaxRadius + y - size*2, size*4, size*4, r, g, b, a);
	}
}


void CHudRadar::DrawCross( int x, int y, int r, int g, int b, int a )
{
	const int size = 2;
	if( bUseRenderAPI )
	{
		DrawColoredTexture( x, y, size, r, g, b, a, hCross );
	}
	else
	{
		FillRGBA(m_iRadarX + iMaxRadius + x, m_iRadarY + iMaxRadius + y, size, size, r, g, b, a);
		FillRGBA(m_iRadarX + iMaxRadius + x - size, m_iRadarY + iMaxRadius + y - size, size, size, r, g, b, a);
		FillRGBA(m_iRadarX + iMaxRadius + x - size, m_iRadarY + iMaxRadius + y + size, size, size, r, g, b, a);
		FillRGBA(m_iRadarX + iMaxRadius + x + size, m_iRadarY + iMaxRadius + y - size, size, size, r, g, b, a);
		FillRGBA(m_iRadarX + iMaxRadius + x + size, m_iRadarY + iMaxRadius + y + size, size, size, r, g, b, a);
	}
}

void CHudRadar::DrawT( int x, int y, int r, int g, int b, int a )
{
	const int size = 2;

	if( bUseRenderAPI )
	{
		DrawColoredTexture( x, y, size, r, g, b, a, hT );
	}
	else
	{
		FillRGBA( m_iRadarX + iMaxRadius + x - size, m_iRadarY + iMaxRadius + y - size, size * 3, size, r, g, b, a);
		FillRGBA( m_iRadarX + iMaxRadius + x, m_iRadarY + iMaxRadius + y, size, size * 2, r, g, b, a);
	}
}

void CHudRadar::DrawFlippedT( int x, int y, int r, int g, int b, int a )
{
	const int size = 2;
	if( bUseRenderAPI )
	{
		DrawColoredTexture( x, y, size, r, g, b, a, hFlippedT );
	}
	else
	{
		FillRGBA( m_iRadarX + iMaxRadius + x, m_iRadarY + iMaxRadius + y - size, size, size*2, r, g, b, a);
		FillRGBA( m_iRadarX + iMaxRadius + x - size, m_iRadarY + iMaxRadius + y + size, size*3, size, r, g, b, a);
	}
}


Vector CHudRadar::WorldToRadar(const Vector vPlayerOrigin, const Vector vObjectOrigin, const Vector vAngles  )
{
	Vector2D diff = vObjectOrigin.Make2D() - vPlayerOrigin.Make2D();
	const float RADAR_SCALE = RadarScale();

	// Supply epsilon values to avoid divide-by-zero
	if( diff.x == 0 )
		diff.x = 0.00001f;
	if( diff.y == 0 )
		diff.y = 0.00001f;

	float flOffset = DEG2RAD( vAngles.y - RAD2DEG( atan2( diff.y, diff.x ) ) );

	// this magic 32.0f just scales position on radar
	float iRadius = min( diff.Length() / RADAR_SCALE, iMaxRadius );

	// transform origin difference to radar source
	Vector ret( (float)(iRadius * sin(flOffset)),
				(float)(iRadius * -cos(flOffset)),
				(float)(vPlayerOrigin.z - vObjectOrigin.z) );

	return ret;
}

// Project a world XY onto the minimap, matching WorldToRadar's rotation/scale but WITHOUT the
// radius clamp (so the map fills the area). cy/sy = cos/sin of the player's yaw (precomputed).
void CHudRadar::WorldToMini( float wx, float wy, float cy, float sy, float &px, float &py )
{
	const float RADAR_SCALE = RadarScale();
	float dx = wx - gHUD.m_vecOrigin.x;
	float dy = wy - gHUD.m_vecOrigin.y;
	px = m_iRadarX + iMaxRadius + ( sy * dx - cy * dy ) / RADAR_SCALE;
	py = m_iRadarY + iMaxRadius - ( cy * dx + sy * dy ) / RADAR_SCALE;
}

// (Re)load the overview for the current map, only when the level changes. Reuses the spectator's
// overview parser. Returns true if a geometry minimap is available.
bool CHudRadar::UpdateMiniMap()
{
	const char *level = gEngfuncs.pfnGetLevelName();
	if( !level || !level[0] )
	{
		m_pMiniMap = NULL;
		m_szMiniLevel[0] = 0;
		return false;
	}

	if( strcmp( level, m_szMiniLevel ) )
	{
		strncpy( m_szMiniLevel, level, sizeof( m_szMiniLevel ) - 1 );
		m_szMiniLevel[sizeof( m_szMiniLevel ) - 1] = 0;

		gHUD.m_Spectator.ParseOverviewFile();
		if( gHUD.m_Spectator.m_OverviewData.layers > 0 )
			m_pMiniMap = gEngfuncs.LoadMapSprite( gHUD.m_Spectator.m_OverviewData.layersImages[0] );
		else
			m_pMiniMap = NULL;
	}

	return m_pMiniMap != NULL;
}

// Draw the overview map tiles into the radar area, rotated to the player's heading and tinted gray
// (Max Payne 3 wall view). Mirrors CHudSpectator::DrawOverviewLayer's tiling/UVs, but projects each
// tile corner into the radar rectangle via WorldToMini instead of world space. Returns false if no
// overview is loaded (caller falls back to the classic radar circle).
bool CHudRadar::DrawMiniMap()
{
	if( !m_pMiniMap )
		return false;

	overviewInfo_t &ov = gHUD.m_Spectator.m_OverviewData;
	const float screenaspect = 4.0f / 3.0f;

	int t = m_pMiniMap->numframes / ( 4 * 3 );
	t = (int)sqrt( (double)t );
	int xTiles = t * 4, yTiles = t * 3;
	if( xTiles <= 0 || yTiles <= 0 )
		return false;

	float yaw = DEG2RAD( gHUD.m_vecAngles.y );
	float cy = cos( yaw ), sy = sin( yaw );

	// dark backing disc so the circle interior reads as the MP3 night-blueprint even where the
	// overview tiles don't reach (scanline strips: the render API has no scissor/stencil mask)
	{
		int ccx = m_iRadarX + iMaxRadius;
		int ccy = m_iRadarY + iMaxRadius;
		for( int row = -iMaxRadius; row <= iMaxRadius; row++ )
		{
			int half = (int)sqrt( (float)( iMaxRadius * iMaxRadius - row * row ) );
			FillRGBABlend( ccx - half, ccy + row, 2 * half, 1, 12, 14, 16, 210 );
		}
	}

	// only draw tiles whose center falls within the radar circle (world radius = pixels * scale);
	// keeps the map roughly circular and stops it spilling across the screen.
	float cullRange = iMaxRadius * RadarScale() * 1.15f;
	float cullRange2 = cullRange * cullRange;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4f( 0.30f, 0.32f, 0.35f, 1.0f ); // dark cool walls (MP3 night map)

	float xs = ov.origin[0], ys = ov.origin[1];
	float x, y, xStep, yStep;
	float px0, py0, px1, py1, px2, py2, px3, py3;
	int frame = 0, ix, iy;

	if( ov.rotated )
	{
		xStep =  ( 2 * 4096.0f / ov.zoom ) / xTiles;
		yStep = -( 2 * 4096.0f / ( ov.zoom * screenaspect ) ) / yTiles;
		y = ys + ( 4096.0f / ( ov.zoom * screenaspect ) );

		for( iy = 0; iy < yTiles; iy++ )
		{
			x = xs - ( 4096.0f / ov.zoom );
			for( ix = 0; ix < xTiles; ix++ )
			{
				float ddx = ( x + xStep * 0.5f ) - gHUD.m_vecOrigin.x;
				float ddy = ( y + yStep * 0.5f ) - gHUD.m_vecOrigin.y;
				if( ddx * ddx + ddy * ddy <= cullRange2 )
				{
					WorldToMini( x,         y,         cy, sy, px0, py0 );
					WorldToMini( x + xStep,  y,         cy, sy, px1, py1 );
					WorldToMini( x + xStep,  y + yStep, cy, sy, px2, py2 );
					WorldToMini( x,          y + yStep, cy, sy, px3, py3 );

					gEngfuncs.pTriAPI->SpriteTexture( m_pMiniMap, frame );
					gEngfuncs.pTriAPI->Begin( TRI_QUADS );
					gEngfuncs.pTriAPI->TexCoord2f( 0, 0 ); gEngfuncs.pTriAPI->Vertex3f( px0, py0, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 1, 0 ); gEngfuncs.pTriAPI->Vertex3f( px1, py1, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 1, 1 ); gEngfuncs.pTriAPI->Vertex3f( px2, py2, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 0, 1 ); gEngfuncs.pTriAPI->Vertex3f( px3, py3, 0 );
					gEngfuncs.pTriAPI->End();
				}
				frame++;
				x += xStep;
			}
			y += yStep;
		}
	}
	else
	{
		xStep = -( 2 * 4096.0f / ov.zoom ) / xTiles;
		yStep = -( 2 * 4096.0f / ( ov.zoom * screenaspect ) ) / yTiles;
		x = xs + ( 4096.0f / ( ov.zoom * screenaspect ) );

		for( ix = 0; ix < yTiles; ix++ )
		{
			y = ys + ( 4096.0f / ov.zoom );
			for( iy = 0; iy < xTiles; iy++ )
			{
				float ddx = ( x + xStep * 0.5f ) - gHUD.m_vecOrigin.x;
				float ddy = ( y + yStep * 0.5f ) - gHUD.m_vecOrigin.y;
				if( ddx * ddx + ddy * ddy <= cullRange2 )
				{
					WorldToMini( x,         y,         cy, sy, px0, py0 );
					WorldToMini( x + xStep,  y,         cy, sy, px1, py1 );
					WorldToMini( x + xStep,  y + yStep, cy, sy, px2, py2 );
					WorldToMini( x,          y + yStep, cy, sy, px3, py3 );

					gEngfuncs.pTriAPI->SpriteTexture( m_pMiniMap, frame );
					gEngfuncs.pTriAPI->Begin( TRI_QUADS );
					gEngfuncs.pTriAPI->TexCoord2f( 0, 0 ); gEngfuncs.pTriAPI->Vertex3f( px0, py0, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 0, 1 ); gEngfuncs.pTriAPI->Vertex3f( px1, py1, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 1, 1 ); gEngfuncs.pTriAPI->Vertex3f( px2, py2, 0 );
					gEngfuncs.pTriAPI->TexCoord2f( 1, 0 ); gEngfuncs.pTriAPI->Vertex3f( px3, py3, 0 );
					gEngfuncs.pTriAPI->End();
				}
				frame++;
				y += yStep;
			}
			x += xStep;
		}
	}

	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	gEngfuncs.pTriAPI->Color4f( 1, 1, 1, 1 );
	return true;
}

// Circular frame around the minimap. Uses the Max Payne 3 radar ring texture when available;
// otherwise a procedural dark ring (a true pixel-perfect circular mask isn't possible: the
// client render API exposes no scissor/stencil).
void CHudRadar::DrawMiniMapFrame()
{
	int cx = m_iRadarX + iMaxRadius;
	int cy = m_iRadarY + iMaxRadius;

	if( bUseRenderAPI && m_iRingTex )
	{
		gRenderAPI.GL_Bind( 0, m_iRingTex );
		gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		gEngfuncs.pTriAPI->Color4f( 1, 1, 1, 1 );
		DrawUtils::Draw2DQuad( cx - iMaxRadius, cy - iMaxRadius, cx + iMaxRadius, cy + iMaxRadius );
		gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
		return;
	}

	const int seg = 96; // denser so the white ring reads as a solid line (MP3 border)
	for( int k = 0; k < seg; k++ )
	{
		float a = ( 2.0f * M_PI * k ) / seg;
		int px = cx + (int)( iMaxRadius * cos( a ) );
		int py = cy + (int)( iMaxRadius * sin( a ) );
		FillRGBABlend( px - 1, py - 1, 3, 3, MP3_WHITE, 240 );
	}
}

// Pointer for the local player at the center of the radar. The map rotates around the player,
// so the pointer always faces "up". Uses the Max Payne 3 chevron texture when available.
void CHudRadar::DrawPlayerArrow()
{
	int cx = m_iRadarX + iMaxRadius;
	int cy = m_iRadarY + iMaxRadius;

	if( bUseRenderAPI && m_iPlayerTex )
	{
		int s = XRES( 2 ); // half-size of the pointer (~1/4 of the original)
		gRenderAPI.GL_Bind( 0, m_iPlayerTex );
		gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		gEngfuncs.pTriAPI->Color4f( 1, 1, 1, 1 );
		DrawUtils::Draw2DQuad( cx - s, cy - s, cx + s, cy + s );
		gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
		return;
	}

	const int h = 6; // half-height; filled triangle, apex up
	// thin dark outline behind the arrow for contrast
	for( int r = 0; r <= h; r++ )
		FillRGBA( cx - r - 1, cy - h + r, 2 * r + 3, 1, 0, 0, 0, 200 );
	// white triangle
	for( int r = 0; r <= h; r++ )
		FillRGBA( cx - r, cy - h + 1 + r, 2 * r + 1, 1, 255, 255, 255, 255 );
}

int CHudRadar::MsgFunc_BombDrop(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	g_PlayerExtraInfo[33].origin.x = reader.ReadCoord();
	g_PlayerExtraInfo[33].origin.y = reader.ReadCoord();
	g_PlayerExtraInfo[33].origin.z = reader.ReadCoord();

	g_PlayerExtraInfo[33].radarflashes = 99999;
	g_PlayerExtraInfo[33].radarflashtime = gHUD.m_flTime;
	g_PlayerExtraInfo[33].radarflashtimedelta = 0.5f;
	strncpy(g_PlayerExtraInfo[33].teamname, "TERRORIST", MAX_TEAM_NAME);
	g_PlayerExtraInfo[33].dead = false;
	g_PlayerExtraInfo[33].nextflash = true;

	int Flag = reader.ReadByte();
	g_PlayerExtraInfo[33].playerclass = Flag;

	if( Flag ) // bomb planted
	{
		gHUD.m_SpectatorGui.m_bBombPlanted = 0;
		gHUD.m_Timer.m_iFlags = 0;
	}
	return 1;
}

int CHudRadar::MsgFunc_BombPickup(const char *pszName, int iSize, void *pbuf)
{
	g_PlayerExtraInfo[33].radarflashes = false;
	g_PlayerExtraInfo[33].dead = true;

	return 1;
}

int CHudRadar::MsgFunc_HostagePos(const char *pszName, int iSize, void *pbuf)
{

	BufferReader reader( pszName, pbuf, iSize );
	int Flag = reader.ReadByte();
	int idx = reader.ReadByte();
	if( idx >= 1 && idx <= MAX_HOSTAGES )
	{
		g_HostageInfo[idx].origin.x = reader.ReadCoord();
		g_HostageInfo[idx].origin.y = reader.ReadCoord();
		g_HostageInfo[idx].origin.z = reader.ReadCoord();
		g_HostageInfo[idx].dead = false;

		if( Flag == 1 ) // first message about this hostage, start flashing
		{
			g_HostageInfo[idx].radarflashes = 99999;
			g_HostageInfo[idx].radarflashtime = gHUD.m_flTime;
			g_HostageInfo[idx].radarflashtimedelta = 0.5f;
		}
	}

	return 1;
}

int CHudRadar::MsgFunc_HostageK(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );
	int idx = reader.ReadByte();
	if ( idx >= 1 && idx <= MAX_HOSTAGES )
	{
		g_HostageInfo[idx].dead = true;
		g_HostageInfo[idx].radarflashtime = gHUD.m_flTime;
		g_HostageInfo[idx].radarflashes = 15;
		g_HostageInfo[idx].radarflashtimedelta = 0.1f;
	}

	return 1;
}

int CHudRadar::MsgFunc_Location(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	int player = reader.ReadByte();
	if( player >= 1 && player <= MAX_PLAYERS )
	{
		const char *location = reader.ReadString();

		strncpy( g_PlayerExtraInfo[player].location, location, sizeof( g_PlayerExtraInfo[player].location ) );
		g_PlayerExtraInfo[player].location[31] = 0;

		GetClientVoiceHud()->UpdateLocation( player, g_PlayerExtraInfo[player].location );
	}
	return 0;
}
