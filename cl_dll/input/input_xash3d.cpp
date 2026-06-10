#define _USE_MATH_DEFINES // for M_PI (used by DEG2RAD/RAD2DEG on MSVC)
#include "hud.h"
#include "usercmd.h"
#include "cvardef.h"
#include "kbutton.h"
#include "keydefs.h"
#include "input.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "const.h"
#include "pmtrace.h"
#include "event_api.h"
#include "pm_defs.h"
#include <math.h>
#include <stdio.h> // aim_assist_debug>=2 file logging (.devnotes/deadzonedebug.txt)

#define	PITCH	0
#define	YAW		1
#define	ROLL	2 

cvar_t	*cl_laddermode;
cvar_t	*sensitivity;
cvar_t	*in_joystick;
cvar_t	*evdev_grab;

// Aim assist (gamepad soft-lock, Max Payne 3 style)
#define AA_TARGET_HALF_HEIGHT	32.0f	// player half-extent; widens the cone for close targets
#define AA_LOS_CLEAR		0.95f	// world-trace fraction that still counts as "visible"
#define AA_PULL_REF_FPS		60.0f	// pull strength is normalized to this framerate

bool g_bAimAssistKey = false;		// hold state of the dedicated aim button (shared)
cvar_t	*aim_assist;			// master on/off
cvar_t	*aim_assist_lock_fov;		// acquisition cone half-angle while the button is held (degrees)
cvar_t	*aim_assist_pull;		// magnetism strength 0..1
cvar_t	*aim_assist_slow;		// sticky slowdown factor applied to stick input
cvar_t	*aim_assist_cap;		// hard clamp (outer ring) as a multiple of the free-aim zone
cvar_t	*aim_assist_width;		// inner ring horizontal half-extent, as a multiple of the target's body size
cvar_t	*aim_assist_height;		// inner ring vertical half-extent, as a multiple of the target's body size
cvar_t	*aim_assist_range;		// max target distance (units)
cvar_t	*aim_assist_wallcheck;		// require line of sight to the target
cvar_t	*aim_assist_debug;		// debug overlay (text + head marker)
cvar_t	*aim_assist_highlight;		// glow shell over the target model
cvar_t	*aim_assist_highlight_color;	// glow color "r g b"
cvar_t	*aim_assist_highlight_amt;	// glow shell thickness/intensity

// Shared state for the debug overlay (cl_dll/hud/aimassist.cpp) and highlight (entity.cpp)
int   g_iAimAssistTarget = 0;		// entity index of the chosen target (0 = none)
bool  g_bAimAssistApplying = false;	// assist actively steering (key held + target)
float g_flAimAssistDist = 0.0f;		// distance to the chosen target
float g_flAimAssistAngle = 0.0f;	// angular separation (deg) to the chosen target
int   g_iAimAssistNearestIdx = 0;	// nearest visible enemy by angle, ignoring the cone
float g_flAimAssistNearestAngle = 0.0f;	// its angle (diagnose a too-tight cone)
// Soft-lock ellipse half-extents (deg) this frame; 0 = no lock. W = horizontal (yaw), H = vertical
// (pitch). Inner ring = free-aim zone, outer ring = hard clamp. Consumed by the debug gizmo.
float g_flAimAssistDeadW = 0.0f;	// inner ring horizontal half-extent
float g_flAimAssistDeadH = 0.0f;	// inner ring vertical half-extent
float g_flAimAssistCapW  = 0.0f;	// outer ring horizontal half-extent
float g_flAimAssistCapH  = 0.0f;	// outer ring vertical half-extent
bool  g_bAimAssistPulling = false;	// view is outside the inner ellipse (magnet correcting)

// view basis used by the assist, stored each frame for the debug cone visualization
vec3_t g_vecAimEye   = { 0, 0, 0 };
vec3_t g_vecAimFwd   = { 0, 0, 0 };
vec3_t g_vecAimRight = { 0, 0, 0 };
vec3_t g_vecAimUp    = { 0, 0, 0 };


float ac_forwardmove;
float ac_sidemove;
int ac_movecount;
float rel_yaw;
float rel_pitch;
bool bMouseInUse = false;

extern Vector dead_viewangles;
extern bool evdev_open;

#define F 1U<<0	// Forward
#define B 1U<<1	// Back
#define L 1U<<2	// Left
#define R 1U<<3	// Right
#define T 1U<<4	// Forward stop
#define S 1U<<5	// Side stop

#define BUTTON_DOWN		1
#define IMPULSE_DOWN	2
#define IMPULSE_UP		4

bool CL_IsDead();

void IN_ToggleButtons( float forwardmove, float sidemove )
{
	static unsigned int moveflags = T | S;

	if( forwardmove )
		moveflags &= ~T;
	else
	{
		//if( in_forward.state || in_back.state ) gEngfuncs.Con_Printf("Buttons pressed f%d b%d\n", in_forward.state, in_back.state);
		if( !( moveflags & T ) )
		{
			//IN_ForwardUp();
			//IN_BackUp();
			//gEngfuncs.Con_Printf("Reset forwardmove state f%d b%d\n", in_forward.state, in_back.state);
			in_forward.state &= ~BUTTON_DOWN;
			in_back.state &= ~BUTTON_DOWN;
			moveflags |= T;
		}
	}
	if( sidemove )
		moveflags &= ~S;
	else
	{
		//gEngfuncs.Con_Printf("l%d r%d\n", in_moveleft.state, in_moveright.state);
		//if( in_moveleft.state || in_moveright.state ) gEngfuncs.Con_Printf("Buttons pressed l%d r%d\n", in_moveleft.state, in_moveright.state);
		if( !( moveflags & S ) )
		{
			//IN_MoverightUp();
			//IN_MoveleftUp();
			//gEngfuncs.Con_Printf("Reset sidemove state f%d b%d\n", in_moveleft.state, in_moveright.state);
			in_moveleft.state &= ~BUTTON_DOWN;
			in_moveright.state &= ~BUTTON_DOWN;
			moveflags |= S;
		}
	}

	if ( forwardmove > 0.7 && !( moveflags & F ))
	{
		moveflags |= F;
		in_forward.state |= BUTTON_DOWN;
	}
	if ( forwardmove < 0.7 && ( moveflags & F ))
	{
		moveflags &= ~F;
		in_forward.state &= ~BUTTON_DOWN;
	}
	if ( forwardmove < -0.7 && !( moveflags & B ))
	{
		moveflags |= B;
		in_back.state |= BUTTON_DOWN;
	}
	if ( forwardmove > -0.7 && ( moveflags & B ))
	{
		moveflags &= ~B;
		in_back.state &= ~BUTTON_DOWN;
	}
	if ( sidemove > 0.9 && !( moveflags & R ))
	{
		moveflags |= R;
		in_moveright.state |= BUTTON_DOWN;
	}
	if ( sidemove < 0.9 && ( moveflags & R ))
	{
		moveflags &= ~R;
		in_moveright.state &= ~BUTTON_DOWN;
	}
	if ( sidemove < -0.9 && !( moveflags & L ))
	{
		moveflags |= L;
		in_moveleft.state |= BUTTON_DOWN;
	}
	if ( sidemove > -0.9 && ( moveflags & L ))
	{
		moveflags &= ~L;
		in_moveleft.state &= ~BUTTON_DOWN;
	}

}

void IN_ClientMoveEvent( float forwardmove, float sidemove )
{
	//gEngfuncs.Con_Printf("IN_MoveEvent\n");

	ac_forwardmove += forwardmove;
	ac_sidemove += sidemove;
	ac_movecount++;
}

void IN_ClientLookEvent( float relyaw, float relpitch )
{
	rel_yaw += relyaw;
	rel_pitch += relpitch;
}

// Console commands bound through the Controls menu (+aimassist / -aimassist)
void IN_AimAssistDown( void ) { g_bAimAssistKey = true;  }
void IN_AimAssistUp( void )   { g_bAimAssistKey = false; }

// Returns true if no world geometry blocks the line from start to end.
// We trace against the world ONLY (PM_WORLD_ONLY): tracing against solid players
// would stop the ray at the very enemy we are checking and report "not visible".
// The PM state push/hull setup is done once by the caller around the candidate loop.
static bool AimAssist_Visible( float *start, float *end )
{
	pmtrace_t tr;
	gEngfuncs.pEventAPI->EV_PlayerTrace( start, end, PM_STUDIO_BOX | PM_WORLD_ONLY, -1, &tr );
	return tr.fraction >= AA_LOS_CLEAR; // ~1.0 means nothing solid in the world blocks the view
}

// Returns true if ANY of a few sample points on the target's body (center, head, feet, both
// shoulders) has a clear line from the eye -- so a partially-exposed enemy (only an arm/head/
// shoulder poking past cover) still counts as visible instead of being rejected wholesale because
// the center of mass is behind the wall. `origin` is the entity center (curstate.origin).
static bool AimAssist_BodyVisible( float *eye, float *origin )
{
	// horizontal perpendicular to the eye->target line, for the shoulder/arm samples
	vec3_t flat = { origin[0] - eye[0], origin[1] - eye[1], 0.0f };
	float fl = sqrt( flat[0] * flat[0] + flat[1] * flat[1] );
	vec3_t perp = { 0.0f, 0.0f, 0.0f };
	if( fl > 0.001f ) { perp[0] = -flat[1] / fl; perp[1] = flat[0] / fl; }

	// z offset + lateral offset (units) per sample: center, head, feet, left & right shoulder
	static const float zoff[]  = {  0.0f, 24.0f, -24.0f, 16.0f, 16.0f };
	static const float latoff[] = { 0.0f,  0.0f,   0.0f, -16.0f, 16.0f };
	for( int s = 0; s < 5; s++ )
	{
		vec3_t p;
		p[0] = origin[0] + perp[0] * latoff[s];
		p[1] = origin[1] + perp[1] * latoff[s];
		p[2] = origin[2] + zoff[s];
		if( AimAssist_Visible( eye, p ) )
			return true;
	}
	return false;
}

// Picks the enemy closest to the crosshair within the assist cone, alive and visible.
static cl_entity_t *AimAssist_FindTarget( float *eye, float *fwd )
{
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	int maxc = gEngfuncs.GetMaxClients();
	int me = local ? local->index : 0;
	float fov = aim_assist_lock_fov->value; // wide acquisition cone (grab nearest target in front)
	float maxRange = aim_assist_range->value;
	float bestAngle = 9999.0f;
	float nearestAngle = 9999.0f;
	cl_entity_t *best = NULL;
	bool wallcheck = aim_assist_wallcheck->value != 0.0f;

	// Target anyone we can actually damage. In free-for-all (or with friendly fire on)
	// teammates are valid targets too, so skip the team filter. These are server cvars,
	// readable on a listen server; cached lazily since they only exist once a server runs.
	static cvar_t *ffaCvar = NULL, *ffCvar = NULL;
	if( !ffaCvar ) ffaCvar = gEngfuncs.pfnGetCvarPointer( "mp_freeforall" );
	if( !ffCvar )  ffCvar  = gEngfuncs.pfnGetCvarPointer( "mp_friendlyfire" );
	bool everyoneEnemy = ( ffaCvar && ffaCvar->value != 0.0f ) || ( ffCvar && ffCvar->value != 0.0f );

	g_iAimAssistNearestIdx = 0;
	g_flAimAssistNearestAngle = 0.0f;

	// Set up the player-trace state once for the whole scan (cheaper than per-candidate).
	if( wallcheck )
	{
		gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
		gEngfuncs.pEventAPI->EV_PushPMStates();
		gEngfuncs.pEventAPI->EV_SetSolidPlayers( -1 );
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	}

	for( int i = 1; i <= maxc; i++ )
	{
		if( i == me )
			continue;

		cl_entity_t *e = gEngfuncs.GetEntityByIndex( i );
		if( !e || !e->player )
			continue;
		// Skip stale "ghost" entities: a player who left our PVS keeps its last-known
		// curstate (old origin, solid still set) but stops being updated. Entities refreshed
		// this frame share the local player's messagenum; mismatched ones are stale.
		if( local && e->curstate.messagenum != local->curstate.messagenum )
			continue;
		if( e->curstate.solid == SOLID_NOT || g_PlayerExtraInfo[i].dead )
			continue; // dead / non-solid
		if( !everyoneEnemy && g_iTeamNumber != 0 && g_PlayerExtraInfo[i].teamnumber == g_iTeamNumber )
			continue; // teammate (only skip when we can't damage them)

		vec3_t dir;
		VectorSubtract( e->curstate.origin, eye, dir );
		float dist = sqrt( dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2] );
		if( dist < 1.0f || dist > maxRange )
			continue;

		float inv = 1.0f / dist;
		dir[0] *= inv; dir[1] *= inv; dir[2] *= inv;

		float d = DotProduct( dir, fwd );
		if( d > 1.0f ) d = 1.0f;
		float angle = RAD2DEG( acos( d ) );

		// diagnostics: nearest enemy by angle, ignoring the cone and LOS
		if( angle < nearestAngle )
		{
			nearestAngle = angle;
			g_iAimAssistNearestIdx = i;
			g_flAimAssistNearestAngle = angle;
		}

		// distance-adjusted cone: a closer enemy subtends a larger angle, so the
		// assist window grows when you're near them (fixes "target none" up close).
		float effFov = fov + RAD2DEG( atan2( AA_TARGET_HALF_HEIGHT, dist ) );
		if( angle > effFov )
			continue; // crosshair not on the enemy
		if( angle >= bestAngle )
			continue; // keep the one closest to the crosshair

		if( wallcheck )
		{
			// sample several body points, not just the center -- a partially-exposed enemy
			// (only an arm/shoulder/head visible) should still be acquirable
			if( !AimAssist_BodyVisible( eye, e->curstate.origin ) )
				continue;
		}

		bestAngle = angle;
		best = e;
	}

	if( wallcheck )
		gEngfuncs.pEventAPI->EV_PopPMStates();

	return best;
}

// --- Deadzone debug logging (aim_assist_debug>=2) ---------------------------------
// Dumps soft-lock telemetry to .devnotes/deadzonedebug.txt (gitignored). See the recap
// in .devnotes/aim-debug-to-file.md for why we log to a file instead of an overlay.
#define AA_DBG_PATH	"C:/Users/ortiz/Documents/repositories/cs16-client/.devnotes/deadzonedebug.txt"
#define AA_DBG_HZ	0.1f	// sample period (~10 Hz) so the file does not flood

static bool  s_aaPrevLocked = false;	// was the soft-lock active last frame (for the end summary)
static float s_aaLogAccum   = 0.0f;	// time accumulator for the sample throttle
static float s_aaPeakNin    = 0.0f;	// max normalized deviation reached this session (1 = inner edge)
static float s_aaLastDist   = 0.0f;	// last seen values, for the session summary
static float s_aaLastDeadW  = 0.0f, s_aaLastDeadH = 0.0f; // inner ellipse half-extents (deg)

static void AimAssist_LogDeadzone( float frametime, float dist, float deadW, float deadH, float capW, float capH, float nin, bool pulling, bool clamped )
{
	if( nin > s_aaPeakNin ) s_aaPeakNin = nin; // track the peak even on throttled frames
	s_aaLastDist  = dist;
	s_aaLastDeadW = deadW;
	s_aaLastDeadH = deadH;

	s_aaLogAccum += frametime;
	if( s_aaLogAccum < AA_DBG_HZ )
		return;
	s_aaLogAccum = 0.0f;

	const char *state = clamped ? "CLAMPED" : ( pulling ? "PULLING" : "free" );
	FILE *fp = fopen( AA_DBG_PATH, "a" );
	if( !fp )
		return;
	// nin/peak are normalized to the inner ellipse: <1 free, 1 = on the edge, up to cap-multiple at the wall
	fprintf( fp, "dist %5.0f  free %4.2fx%4.2f deg  cap %4.2fx%4.2f deg  dev %4.2f  peak %4.2f  %s\n",
		dist, deadW, deadH, capW, capH, nin, s_aaPeakNin, state );
	fclose( fp );
}

static void AimAssist_LogDeadzoneEnd( void )
{
	FILE *fp = fopen( AA_DBG_PATH, "a" );
	if( fp )
	{
		fprintf( fp, "=== lock end === dist %.0f  free %.2fx%.2f deg  peakDev %.2f (x inner)  %s\n\n",
			s_aaLastDist, s_aaLastDeadW, s_aaLastDeadH, s_aaPeakNin,
			s_aaPeakNin <= 1.01f ? "stayed in free zone" : "held between rings" );
		fclose( fp );
	}
	s_aaPeakNin = 0.0f;
	s_aaLogAccum = 0.0f;
}

// Rotate camera and add move values to usercmd
void IN_Move( float frametime, usercmd_t *cmd )
{
	Vector viewangles;
	bool bLadder = false;

	if( gHUD.m_iIntermission )
		return; // we can't move during intermission


	if( cl_laddermode->value != 2 )
	{
		cl_entity_t *pplayer = gEngfuncs.GetLocalPlayer();
		if( pplayer )
			bLadder = pplayer->curstate.movetype == MOVETYPE_FLY;
	}
	//if(ac_forwardmove || ac_sidemove)
	//gEngfuncs.Con_Printf("Move: %f %f %f %f\n", ac_forwardmove, ac_sidemove, rel_pitch, rel_yaw);
#if 0
	if( in_mlook.state & 1 )
	{
		V_StopPitchDrift();
	}
#endif

	if( CL_IsDead( ) )
	{
		viewangles = dead_viewangles; // HACKHACK: see below
	}
	else
	{
		gEngfuncs.GetViewAngles( viewangles );
	}

	if( gHUD.GetSensitivity() != 0 )
	{
		rel_yaw *= gHUD.GetSensitivity();
		rel_pitch *= gHUD.GetSensitivity();
	}
	else
	{
		rel_yaw *= sensitivity->value;
		rel_pitch *= sensitivity->value;
	}

	// --- Aim assist: find target (for steering and/or the debug overlay) ---
	cl_entity_t *aaTarget = NULL;
	vec3_t aaDesired = { 0, 0, 0 };
	g_iAimAssistTarget = 0;
	g_iAimAssistNearestIdx = 0;
	g_bAimAssistApplying = false;
	g_flAimAssistDist = g_flAimAssistAngle = 0.0f;

	// Everything below runs only when the feature is on, so the default path is untouched.
	bool aaEnabled = aim_assist->value && !CL_IsDead()
		&& !( gHUD.m_MOTD.cl_hide_motd->value == 0.0f && gHUD.m_MOTD.m_bShow );
	cl_entity_t *local = aaEnabled ? gEngfuncs.GetLocalPlayer() : NULL;
	if( local )
	{
		// Eye = the player's real shooting origin (origin + view offset), like the weapons use.
		// This is camera-independent, so it stays correct in third person (v_origin would be
		// the orbit camera there) and respects ducking via the predicted view height.
		vec3_t viewofs = { 0, 0, 0 };
		gEngfuncs.pEventAPI->EV_LocalPlayerViewheight( viewofs );
		g_vecAimEye[0] = local->origin[0] + viewofs[0];
		g_vecAimEye[1] = local->origin[1] + viewofs[1];
		g_vecAimEye[2] = local->origin[2] + viewofs[2];
		AngleVectors( viewangles, g_vecAimFwd, g_vecAimRight, g_vecAimUp );

		// scan when steering (key held) OR when a debug/highlight view wants the target
		if( g_bAimAssistKey || aim_assist_debug->value || aim_assist_highlight->value )
		{
			aaTarget = AimAssist_FindTarget( g_vecAimEye, g_vecAimFwd );
			if( aaTarget )
			{
				vec3_t dir;
				VectorSubtract( aaTarget->curstate.origin, g_vecAimEye, dir );
				float dist = sqrt( dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2] );
				float hyp = sqrt( dir[0] * dir[0] + dir[1] * dir[1] );
				aaDesired[YAW]   = RAD2DEG( atan2( dir[1], dir[0] ) );
				aaDesired[PITCH] = -RAD2DEG( atan2( dir[2], hyp ) ); // GoldSrc: positive pitch looks down

				g_iAimAssistTarget = aaTarget->index;
				g_flAimAssistDist  = dist;
				if( dist > 0.0f )
				{
					float dot = DotProduct( dir, g_vecAimFwd ) / dist;
					g_flAimAssistAngle = RAD2DEG( acos( dot > 1.0f ? 1.0f : dot ) );
				}

				// sticky: dampen manual rotation, but only while actually steering (key held)
				if( g_bAimAssistKey )
				{
					g_bAimAssistApplying = true;
					rel_yaw   *= aim_assist_slow->value;
					rel_pitch *= aim_assist_slow->value;
				}
			}
		}
	}

	if(gHUD.m_MOTD.cl_hide_motd->value == 0.0f && gHUD.m_MOTD.m_bShow)
	{
		gHUD.m_MOTD.scroll += rel_pitch;
	}
	else
	{
		viewangles[PITCH] += rel_pitch;
		viewangles[YAW] += rel_yaw;
		if( bLadder )
		{
			if( ( cl_laddermode->value == 1 ) )
				viewangles[YAW] -= ac_sidemove * 5;
			ac_sidemove = 0;
		}
	}

	// --- Aim assist: soft-lock magnetism (Max Payne 3 style) ---
	// Free aim inside a cone the size of the target's body (aim at head/feet/hands); only pull the
	// aim back when it drifts OUTSIDE that cone, and only gradually -- so when the target moves the
	// aim lags slightly and the player nudges to re-align.
	if( aaTarget && g_bAimAssistKey )
	{
		float dyaw   = aaDesired[YAW]   - viewangles[YAW];
		float dpitch = aaDesired[PITCH] - viewangles[PITCH];
		while( dyaw > 180.0f )  dyaw -= 360.0f; // shortest way around
		while( dyaw < -180.0f ) dyaw += 360.0f;

		float dist = g_flAimAssistDist > 1.0f ? g_flAimAssistDist : 1.0f;
		float body = RAD2DEG( atan2( AA_TARGET_HALF_HEIGHT, dist ) ); // body angular half-size

		// Elliptical free-aim zone (human silhouette: narrower than tall). W = horizontal (yaw), H =
		// vertical (pitch), each a multiple of the body size. A tiny floor avoids divide-by-zero.
		float deadW = body * aim_assist_width->value;  if( deadW < 0.01f ) deadW = 0.01f;
		float deadH = body * aim_assist_height->value; if( deadH < 0.01f ) deadH = 0.01f;
		float capW  = deadW * aim_assist_cap->value;
		float capH  = deadH * aim_assist_cap->value;

		// Two ellipses: free aim inside the inner one (no pull), soft spring between them, and a HARD
		// wall at the outer one -- the aim can never drift past it, so it never loses the target.
		// `nin` is the normalized distance on the inner ellipse (1 = on the edge); for a circle this
		// reduces to ang/dead, so the spring math (keep = excess fraction) is unchanged.
		float nin = sqrt( ( dyaw / deadW ) * ( dyaw / deadW ) + ( dpitch / deadH ) * ( dpitch / deadH ) );
		bool pulling = ( nin > 1.0f );
		if( pulling )
		{
			float keep = ( nin - 1.0f ) / nin;                   // pull back only the excess
			float t = aim_assist_pull->value * frametime * AA_PULL_REF_FPS;
			if( t > 1.0f ) t = 1.0f;
			if( t < 0.0f ) t = 0.0f;
			viewangles[YAW]   += dyaw   * keep * t;
			viewangles[PITCH] += dpitch * keep * t;
		}
		// inside the ellipse: no pull -> free aim within the target's silhouette

		// hard clamp on the outer ellipse: recompute the offset after the spring and pin it to the ring
		bool clamped = false;
		if( capW > 0.0f && capH > 0.0f )
		{
			float ovyaw   = viewangles[YAW]   - aaDesired[YAW];  // view offset from target (deg)
			float ovpitch = viewangles[PITCH] - aaDesired[PITCH];
			while( ovyaw > 180.0f )  ovyaw -= 360.0f;
			while( ovyaw < -180.0f ) ovyaw += 360.0f;
			float nout = sqrt( ( ovyaw / capW ) * ( ovyaw / capW ) + ( ovpitch / capH ) * ( ovpitch / capH ) );
			if( nout > 1.0f )
			{
				float s = 1.0f / nout;                           // project radially onto the ellipse
				viewangles[YAW]   = aaDesired[YAW]   + ovyaw   * s;
				viewangles[PITCH] = aaDesired[PITCH] + ovpitch * s;
				clamped = true;
			}
		}

		g_flAimAssistDeadW = deadW; // expose both ellipses to the debug gizmo
		g_flAimAssistDeadH = deadH;
		g_flAimAssistCapW  = capW;
		g_flAimAssistCapH  = capH;
		g_bAimAssistPulling = pulling;

		// --- DEBUG (aim_assist_debug>=2): dump deadzone telemetry to disk ---
		// Same "numbers to a file" trick as the decal bug (see .devnotes/aim-debug-to-file.md):
		// log how far the aim can drift before the magnet pulls, vs how far it actually drifted.
		if( aim_assist_debug->value >= 2.0f )
		{
			AimAssist_LogDeadzone( frametime, dist, deadW, deadH, capW, capH, nin, pulling, clamped );
			s_aaPrevLocked = true;
		}
	}
	else if( s_aaPrevLocked && aim_assist_debug->value >= 2.0f )
	{
		AimAssist_LogDeadzoneEnd(); // lock released or target lost: write the session summary
		s_aaPrevLocked = false;
	}

	if( !( aaTarget && g_bAimAssistKey ) )
	{
		g_flAimAssistDeadW = g_flAimAssistDeadH = 0.0f; // no lock -> hide the gizmo
		g_flAimAssistCapW  = g_flAimAssistCapH  = 0.0f;
		g_bAimAssistPulling = false;
	}

	if (viewangles[PITCH] > cl_pitchdown->value)
		viewangles[PITCH] = cl_pitchdown->value;
	if (viewangles[PITCH] < -cl_pitchup->value)
		viewangles[PITCH] = -cl_pitchup->value;


	if( !CL_IsDead( ) )
	{
		gEngfuncs.SetViewAngles( viewangles );
	}

	dead_viewangles = viewangles;

	if( ac_movecount )
	{
		IN_ToggleButtons( ac_forwardmove / ac_movecount, ac_sidemove / ac_movecount );
		if( ac_forwardmove ) cmd->forwardmove  = ac_forwardmove * cl_forwardspeed->value / ac_movecount;
		if( ac_sidemove ) cmd->sidemove  = ac_sidemove * cl_sidespeed->value / ac_movecount;
		if (in_speed.state & 1)
		{
			cmd->forwardmove *= cl_movespeedkey->value;
			cmd->sidemove *= cl_movespeedkey->value;
		}
	}
	
	ac_sidemove = ac_forwardmove = rel_pitch = rel_yaw = 0;
	ac_movecount = 0;
}

void DLLEXPORT IN_MouseEvent( int mstate )
{
	static int mouse_oldbuttonstate;
	// perform button actions
	for( int i = 0; i < 5; i++ )
	{
		if(( mstate & (1 << i)) && !( mouse_oldbuttonstate & (1 << i)))
		{
			gEngfuncs.Key_Event( K_MOUSE1 + i, 1 );
		}

		if( !( mstate & (1 << i)) && ( mouse_oldbuttonstate & (1 << i)))
		{
			gEngfuncs.Key_Event( K_MOUSE1 + i, 0 );
		}
	}	
	
	mouse_oldbuttonstate = mstate;
	bMouseInUse = true;
}

// Stubs

void DLLEXPORT IN_ClearStates ( void )
{
	//gEngfuncs.Con_Printf("IN_ClearStates\n");
}

void  DLLEXPORT IN_ActivateMouse ( void )
{
	//gEngfuncs.Con_Printf("IN_ActivateMouse\n");
}

void DLLEXPORT  IN_DeactivateMouse ( void )
{
	//gEngfuncs.Con_Printf("IN_DeactivateMouse\n");
}

void DLLEXPORT IN_Accumulate ( void )
{
	//gEngfuncs.Con_Printf("IN_Accumulate\n");
}

void IN_Commands ( void )
{
	//gEngfuncs.Con_Printf("IN_Commands\n");
}

void IN_Shutdown ( void )
{
}
// Register cvars and reset data
void IN_Init( void )
{
	sensitivity = gEngfuncs.pfnRegisterVariable ( "sensitivity", "3", FCVAR_ARCHIVE );
	in_joystick = gEngfuncs.pfnRegisterVariable ( "joystick", "0", FCVAR_ARCHIVE );
	cl_laddermode = gEngfuncs.pfnRegisterVariable ( "cl_laddermode", "2", FCVAR_ARCHIVE );
	evdev_grab = gEngfuncs.pfnGetCvarPointer("evdev_grab");

	// Aim assist (bindable via Controls menu as "+aimassist")
	gEngfuncs.pfnAddCommand( "+aimassist", IN_AimAssistDown );
	gEngfuncs.pfnAddCommand( "-aimassist", IN_AimAssistUp );
	aim_assist                 = gEngfuncs.pfnRegisterVariable( "aim_assist",                 "0",       FCVAR_ARCHIVE );
	aim_assist_lock_fov        = gEngfuncs.pfnRegisterVariable( "aim_assist_lock_fov",        "45",      FCVAR_ARCHIVE );
	aim_assist_pull            = gEngfuncs.pfnRegisterVariable( "aim_assist_pull",            "0.25",    FCVAR_ARCHIVE );
	aim_assist_slow            = gEngfuncs.pfnRegisterVariable( "aim_assist_slow",            "0.4",     FCVAR_ARCHIVE );
	aim_assist_cap             = gEngfuncs.pfnRegisterVariable( "aim_assist_cap",             "2.0",     FCVAR_ARCHIVE );
	aim_assist_width           = gEngfuncs.pfnRegisterVariable( "aim_assist_width",           "0.25",    FCVAR_ARCHIVE );
	aim_assist_height          = gEngfuncs.pfnRegisterVariable( "aim_assist_height",          "0.5",     FCVAR_ARCHIVE );
	aim_assist_range           = gEngfuncs.pfnRegisterVariable( "aim_assist_range",           "2250",    FCVAR_ARCHIVE );
	aim_assist_wallcheck       = gEngfuncs.pfnRegisterVariable( "aim_assist_wallcheck",       "1",       FCVAR_ARCHIVE );
	aim_assist_debug           = gEngfuncs.pfnRegisterVariable( "aim_assist_debug",           "0",       FCVAR_ARCHIVE );
	aim_assist_highlight       = gEngfuncs.pfnRegisterVariable( "aim_assist_highlight",       "0",       FCVAR_ARCHIVE );
	aim_assist_highlight_color = gEngfuncs.pfnRegisterVariable( "aim_assist_highlight_color", "0 255 0", FCVAR_ARCHIVE );
	aim_assist_highlight_amt   = gEngfuncs.pfnRegisterVariable( "aim_assist_highlight_amt",   "75",      FCVAR_ARCHIVE );

	ac_forwardmove = ac_sidemove = rel_yaw = rel_pitch = 0;
}
