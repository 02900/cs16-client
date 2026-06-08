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
//  hud_update.cpp
//

#include <math.h>
#include "hud.h"
#include "cl_util.h"
#include "camera.h"
#include <stdlib.h>
#include <memory.h>

int CL_ButtonBits( int );
void CL_ResetButtonBits( int bits );

extern float v_idlescale;
extern void HUD_SetCmdBits( int bits );
extern bool g_bAimAssistKey; // aim button hold state (input_xash3d.cpp)

int CHud::UpdateClientData(client_data_t *cdata, float time)
{
	m_vecOrigin = cdata->origin;
	m_vecAngles = cdata->viewangles;

	m_iKeyBits = CL_ButtonBits( 0 );
	m_iWeaponBits = cdata->iWeaponBits;

	Think();

	cdata->fov = m_iFOV;

	// OTS aim zoom: optionally narrow the FOV while aiming in third person.
	// Off by default (cam_ots_aim_fov 0); guarded so it never overrides a scope's tighter FOV.
	if( cam_thirdperson_enable && cam_thirdperson_enable->value && g_bAimAssistKey
		&& cam_ots_aim_fov && cam_ots_aim_fov->value > 0
		&& ( m_iFOV == 0 || m_iFOV >= cam_ots_aim_fov->value ) )
	{
		cdata->fov = cam_ots_aim_fov->value;
	}

	v_idlescale = m_iConcussionEffect;

	CL_ResetButtonBits( m_iKeyBits );

	// return 1 if in anything in the client_data struct has been changed, 0 otherwise
	return 1;
}


