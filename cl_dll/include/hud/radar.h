/*
radar.h
Copyright (C) 2015 a1batross
*/
#pragma once
#ifndef RADAR_H
#define RADAR_H

class CClientSprite;

class CHudRadar: public CHudBase
{
public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw( float flTime );
	virtual void InitHUDData();
	virtual void Reset();
	virtual void Shutdown();

	int MsgFunc_Radar(const char *pszName,  int iSize, void *pbuf);

	void UserCmd_ShowRadar();
	void UserCmd_HideRadar();
	CClientSprite m_hRadar;
	CClientSprite m_hRadarOpaque;

	// top edge (in screen px) of the radar box, so other HUD elements can sit above it
	int RadarTopY() const { return m_iRadarY; }

	int MsgFunc_BombDrop(const char *pszName, int iSize, void *pbuf);
	int MsgFunc_BombPickup(const char *pszName, int iSize, void *pbuf);
	int MsgFunc_HostagePos(const char *pszName, int iSize, void *pbuf);
	int MsgFunc_HostageK(const char *pszName, int iSize, void *pbuf);
	int MsgFunc_Location(const char *pszName, int iSize, void *pbuf);
private:

	cvar_t *cl_radartype;

	int InitBuiltinTextures();
	void DrawPlayerLocation(int y);
	void DrawRadarDot(int x, int y, int r, int g, int b, int a);
	void DrawCross(int x, int y, int r, int g, int b, int a );

	// Call DrawT, DrawFlippedT or DrawRadarDot considering z value
	inline void DrawZAxis( Vector pos, int r, int g, int b, int a );

	void DrawT( int x, int y, int r, int g, int b, int a );
	void DrawFlippedT( int x, int y, int r, int g, int b, int a );
	bool HostageFlashTime( float flTime, struct hostage_info_t *pplayer );
	bool FlashTime( float flTime, struct extra_player_info_t *pplayer );
	Vector WorldToRadar(const Vector vPlayerOrigin, const Vector vObjectOrigin, const Vector vAngles );
	inline void DrawColoredTexture( int x, int y, int size, byte r, byte g, byte b, byte a, int texHandle );

	// Max Payne 3 minimap: draw the level overview geometry inside the radar when available.
	bool UpdateMiniMap();                                         // (re)load overview on map change
	bool DrawMiniMap();                                           // draw it; false if unavailable
	void WorldToMini( float wx, float wy, float cy, float sy, float &px, float &py );
	void DrawMiniMapFrame();                                      // dark circular border around the map
	void DrawPlayerArrow();                                       // white pointer for the local player (center)

	bool bUseRenderAPI, bTexturesInitialized;
	int hCross, hT, hFlippedT;
	int iMaxRadius;
	int m_iRadarX, m_iRadarY; // top-left anchor of the radar box (Max Payne 3: bottom-left)
	struct model_s *m_pMiniMap;   // overview map sprite (tiled), NULL if none for this map
	char m_szMiniLevel[64];       // level the overview was loaded for (change detection)
	int m_iBlueprintTex;          // gfx/mp3/minimaps/<map>.png blueprint (0 = use the overview)
	float m_flBPBounds[4];        // blueprint world extent: minx miny maxx maxy (from the sidecar)
	int m_iRingTex;               // gfx/mp3/radar_ring.png (0 = use procedural frame)
	int m_iPlayerTex;             // gfx/mp3/radar_player.png (0 = use procedural arrow)
	int m_iBlipTex;               // gfx/mp3/radar_blip.png (0 = use procedural dot)
};

#endif // RADAR_H
