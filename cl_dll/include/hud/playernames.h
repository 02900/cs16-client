/*
playernames.h - over-head player names + health bars (Max Payne 3 style)
*/
#pragma once
#ifndef PLAYERNAMES_H
#define PLAYERNAMES_H

class CHudPlayerNames : public CHudBase
{
public:
	int Init( void );
	int VidInit( void );
	int Draw( float flTime );
private:
	cvar_t *cl_playernames;            // master on/off
	cvar_t *cl_playernames_wallcheck;  // only show players with a clear line of sight
	cvar_t *cl_playernames_dist;       // max distance (units)
};

#endif // PLAYERNAMES_H
