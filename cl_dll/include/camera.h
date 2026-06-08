//========= Copyright © 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

// Camera.h  --  defines and such for a 3rd person camera
// NOTE: must include quakedef.h first
#pragma once
#ifndef _CAMERA_H_
#define _CAMEA_H_

// pitch, yaw, dist
extern vec3_t cam_ofs;
// Using third person camera
extern int cam_thirdperson;

// Over-the-shoulder (Max Payne 3 style) third person camera
extern cvar_t *cam_thirdperson_enable; // master toggle for the OTS camera
extern cvar_t *cam_ots_dist;           // camera distance behind the player
extern cvar_t *cam_ots_side;           // lateral shoulder offset (right)
extern cvar_t *cam_ots_up;             // vertical offset
extern cvar_t *cam_ots_aim_dist;       // camera distance while aiming (zoom in)
extern cvar_t *cam_ots_aim_fov;        // optional FOV while aiming (0 = unchanged)

void CAM_Init( void );
void CAM_ClearStates( void );
void CAM_StartMouseMove(void);
void CAM_EndMouseMove(void);

#endif		// _CAMERA_H_
