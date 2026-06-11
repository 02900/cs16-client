/*
mp3postfx.cpp - true grayscale post-process for the MP3 death screen.

The engine's render API exposes no shaders, but the GL context is current during HUD_Redraw,
so we use the classic Paranoia-mod technique: copy the backbuffer into an engine texture
(GL_Bind + raw glCopyTexSubImage2D) and redraw it fullscreen through an ARB fragment program
that computes real luminance (0.299R + 0.587G + 0.114B). Everything is loaded dynamically from
opengl32.dll and probed once; on any failure (different renderer, missing extension) the module
reports unavailable and the caller falls back to its cheap approximation.
*/
#ifdef _WIN32
// windows.h declares a GDI HSPRITE handle that collides with the HL SDK's `typedef int HSPRITE`;
// rename it out of the way (same workaround as the classic HLSDK win32 sources)
#define WIN32_LEAN_AND_MEAN
#define HSPRITE WINDOWS_HSPRITE
#include <windows.h>
#undef HSPRITE
#endif

#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "mp3postfx.h"

#ifdef _WIN32

// minimal GL types/constants (avoid linking opengl32; everything goes through pointers)
typedef unsigned int  GLenum;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLubyte;

#define GLC_TEXTURE_2D               0x0DE1
#define GLC_EXTENSIONS               0x1F03
#define GLC_FRAGMENT_PROGRAM_ARB     0x8804
#define GLC_PROGRAM_FORMAT_ASCII_ARB 0x8875

typedef const GLubyte *( APIENTRY *PFNGLGETSTRING )( GLenum );
typedef void ( APIENTRY *PFNGLENABLE )( GLenum );
typedef void ( APIENTRY *PFNGLDISABLE )( GLenum );
typedef GLenum ( APIENTRY *PFNGLGETERROR )( void );
typedef void ( APIENTRY *PFNGLCOPYTEXSUBIMAGE2D )( GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei );
typedef void ( APIENTRY *PFNGLGENPROGRAMSARB )( GLsizei, unsigned int * );
typedef void ( APIENTRY *PFNGLBINDPROGRAMARB )( GLenum, unsigned int );
typedef void ( APIENTRY *PFNGLPROGRAMSTRINGARB )( GLenum, GLenum, GLsizei, const void * );
typedef HGLRC ( WINAPI *PFNWGLGETCURRENTCONTEXT )( void );
typedef PROC ( WINAPI *PFNWGLGETPROCADDRESS )( LPCSTR );

static PFNGLGETSTRING          pglGetString;
static PFNGLENABLE             pglEnable;
static PFNGLDISABLE            pglDisable;
static PFNGLGETERROR           pglGetError;
static PFNGLCOPYTEXSUBIMAGE2D  pglCopyTexSubImage2D;
static PFNGLGENPROGRAMSARB     pglGenProgramsARB;
static PFNGLBINDPROGRAMARB     pglBindProgramARB;
static PFNGLPROGRAMSTRINGARB   pglProgramStringARB;

static int s_iStatus = 0;       // 0 = unprobed, 1 = ready, -1 = unavailable
static unsigned int s_uProgram; // ARB fragment program handle
static int s_iScreenTex;        // engine texture the backbuffer is copied into
static int s_iTexW, s_iTexH;    // its size (recreated when the resolution changes)

static const char s_szGrayFP[] =
	"!!ARBfp1.0\n"
	"TEMP c;\n"
	"TEX c, fragment.texcoord[0], texture[0], 2D;\n"
	"DP3 result.color.rgb, c, {0.299, 0.587, 0.114};\n"
	"MOV result.color.a, c.a;\n"
	"END\n";

static bool PostFX_Probe( void )
{
	if( !g_iXash )
		return false;

	HMODULE gl = GetModuleHandleA( "opengl32.dll" );
	if( !gl )
		return false; // not the GL renderer

	PFNWGLGETCURRENTCONTEXT pwglGetCurrentContext =
		(PFNWGLGETCURRENTCONTEXT)GetProcAddress( gl, "wglGetCurrentContext" );
	PFNWGLGETPROCADDRESS pwglGetProcAddress =
		(PFNWGLGETPROCADDRESS)GetProcAddress( gl, "wglGetProcAddress" );
	if( !pwglGetCurrentContext || !pwglGetProcAddress || !pwglGetCurrentContext() )
		return false; // no current GL context on this thread

	pglGetString         = (PFNGLGETSTRING)GetProcAddress( gl, "glGetString" );
	pglEnable            = (PFNGLENABLE)GetProcAddress( gl, "glEnable" );
	pglDisable           = (PFNGLDISABLE)GetProcAddress( gl, "glDisable" );
	pglGetError          = (PFNGLGETERROR)GetProcAddress( gl, "glGetError" );
	pglCopyTexSubImage2D = (PFNGLCOPYTEXSUBIMAGE2D)GetProcAddress( gl, "glCopyTexSubImage2D" );
	if( !pglGetString || !pglEnable || !pglDisable || !pglGetError || !pglCopyTexSubImage2D )
		return false;

	const char *ext = (const char *)pglGetString( GLC_EXTENSIONS );
	if( !ext || !strstr( ext, "GL_ARB_fragment_program" ) )
		return false;

	pglGenProgramsARB   = (PFNGLGENPROGRAMSARB)pwglGetProcAddress( "glGenProgramsARB" );
	pglBindProgramARB   = (PFNGLBINDPROGRAMARB)pwglGetProcAddress( "glBindProgramARB" );
	pglProgramStringARB = (PFNGLPROGRAMSTRINGARB)pwglGetProcAddress( "glProgramStringARB" );
	if( !pglGenProgramsARB || !pglBindProgramARB || !pglProgramStringARB )
		return false;

	// compile the luminance program once
	pglGetError(); // clear
	pglGenProgramsARB( 1, &s_uProgram );
	pglBindProgramARB( GLC_FRAGMENT_PROGRAM_ARB, s_uProgram );
	pglProgramStringARB( GLC_FRAGMENT_PROGRAM_ARB, GLC_PROGRAM_FORMAT_ASCII_ARB,
		(GLsizei)strlen( s_szGrayFP ), s_szGrayFP );
	if( pglGetError() != 0 )
		return false;

	return true;
}

bool Mp3PostFX_GrayscaleScreen( void )
{
	if( s_iStatus == 0 )
		s_iStatus = PostFX_Probe() ? 1 : -1;
	if( s_iStatus < 0 )
		return false;

	// (re)create the capture texture at the current resolution
	if( !s_iScreenTex || s_iTexW != ScreenWidth || s_iTexH != ScreenHeight )
	{
		if( s_iScreenTex )
			gRenderAPI.GL_FreeTexture( s_iScreenTex );
		s_iTexW = ScreenWidth;
		s_iTexH = ScreenHeight;
		s_iScreenTex = gRenderAPI.GL_CreateTexture( "*mp3grayfb", s_iTexW, s_iTexH, NULL,
			(texFlags_t)( TF_NOMIPMAP | TF_CLAMP ) );
		if( !s_iScreenTex )
		{
			s_iStatus = -1;
			return false;
		}
	}

	// grab the frame rendered so far (engine bind selects the texture object for the raw copy)
	gRenderAPI.GL_Bind( 0, s_iScreenTex );
	pglCopyTexSubImage2D( GLC_TEXTURE_2D, 0, 0, 0, 0, 0, s_iTexW, s_iTexH );

	// redraw it fullscreen through the luminance program. glCopyTexSubImage2D fills the
	// texture bottom-up, so V is flipped (screen top = v 1).
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( 255, 255, 255, 255 );

	pglEnable( GLC_FRAGMENT_PROGRAM_ARB );
	pglBindProgramARB( GLC_FRAGMENT_PROGRAM_ARB, s_uProgram );

	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	gEngfuncs.pTriAPI->TexCoord2f( 0, 1 ); gEngfuncs.pTriAPI->Vertex3f( 0,                  0,                   0 );
	gEngfuncs.pTriAPI->TexCoord2f( 1, 1 ); gEngfuncs.pTriAPI->Vertex3f( (float)ScreenWidth, 0,                   0 );
	gEngfuncs.pTriAPI->TexCoord2f( 1, 0 ); gEngfuncs.pTriAPI->Vertex3f( (float)ScreenWidth, (float)ScreenHeight, 0 );
	gEngfuncs.pTriAPI->TexCoord2f( 0, 0 ); gEngfuncs.pTriAPI->Vertex3f( 0,                  (float)ScreenHeight, 0 );
	gEngfuncs.pTriAPI->End();

	pglDisable( GLC_FRAGMENT_PROGRAM_ARB );
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	return true;
}

#else // !_WIN32

bool Mp3PostFX_GrayscaleScreen( void )
{
	return false; // win32-only for now; callers fall back to the cheap approximation
}

#endif // _WIN32
