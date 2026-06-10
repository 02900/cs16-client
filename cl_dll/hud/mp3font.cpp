/*
mp3font.cpp - render numbers using Max Payne 3's white HUD digit strip.

gfx/mp3/death_numbers.png is a 1024x128 image holding the 10 digits 0-9 (white -> tintable).
Each digit is drawn as a UV sub-quad via TriAPI, so a number scales to any pixel height and can be
tinted to any color (white * color = color). Shared instance gMp3Font; load it from a VidInit.
*/
#include "stdio.h"
#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "mp3font.h"

CMp3Font gMp3Font;

#define STRIP_W 1024.0f
#define STRIP_H 128.0f

// inclusive x range of each digit glyph in death_numbers.png, measured from its alpha channel.
static const int s_digitX[10][2] =
{
	{   5, 100 }, // 0
	{ 121, 181 }, // 1
	{ 209, 304 }, // 2
	{ 309, 406 }, // 3
	{ 412, 508 }, // 4
	{ 514, 610 }, // 5
	{ 617, 712 }, // 6
	{ 728, 814 }, // 7
	{ 819, 916 }, // 8
	{ 923, 1018 } // 9
};

void CMp3Font::Load()
{
	m_iTex = 0;
	if( g_iXash )
	{
		texFlags_t f = (texFlags_t)( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA );
		m_iTex = gRenderAPI.GL_LoadTexture( "gfx/mp3/death_numbers.png", NULL, 0, f );
	}
}

int CMp3Font::DigitWidth( int digit, int h ) const
{
	if( digit < 0 || digit > 9 )
		return 0;
	int pw = s_digitX[digit][1] - s_digitX[digit][0] + 1;
	return (int)( h * pw / STRIP_H );
}

int CMp3Font::NumberWidth( int number, int h ) const
{
	if( number < 0 ) number = 0;
	char buf[16];
	snprintf( buf, sizeof( buf ), "%d", number );
	int gap = max( 1, h / 12 );
	int w = 0;
	for( const char *p = buf; *p; p++ )
		w += DigitWidth( *p - '0', h ) + gap;
	return w > 0 ? w - gap : 0;
}

void CMp3Font::DrawDigit( int x, int y, int h, int digit, int r, int g, int b, int a )
{
	if( !m_iTex || digit < 0 || digit > 9 )
		return;

	float u0 = s_digitX[digit][0] / STRIP_W;
	float u1 = ( s_digitX[digit][1] + 1 ) / STRIP_W;
	int w = DigitWidth( digit, h );

	gRenderAPI.GL_Bind( 0, m_iTex );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	gEngfuncs.pTriAPI->TexCoord2f( u0, 0 ); gEngfuncs.pTriAPI->Vertex3f( x,     y,     0 );
	gEngfuncs.pTriAPI->TexCoord2f( u1, 0 ); gEngfuncs.pTriAPI->Vertex3f( x + w, y,     0 );
	gEngfuncs.pTriAPI->TexCoord2f( u1, 1 ); gEngfuncs.pTriAPI->Vertex3f( x + w, y + h, 0 );
	gEngfuncs.pTriAPI->TexCoord2f( u0, 1 ); gEngfuncs.pTriAPI->Vertex3f( x,     y + h, 0 );
	gEngfuncs.pTriAPI->End();
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
}

void CMp3Font::DrawNumber( int x, int y, int h, int number, int r, int g, int b, int a )
{
	if( !m_iTex )
		return;
	if( number < 0 ) number = 0;
	char buf[16];
	snprintf( buf, sizeof( buf ), "%d", number );
	int gap = max( 1, h / 12 );
	for( const char *p = buf; *p; p++ )
	{
		int d = *p - '0';
		DrawDigit( x, y, h, d, r, g, b, a );
		x += DigitWidth( d, h ) + gap;
	}
}

void CMp3Font::DrawNumberOutlined( int x, int y, int h, int number, int r, int g, int b, int a )
{
	if( !m_iTex )
		return;
	int t = max( 1, h / 12 );   // outline thickness
	for( int dy = -t; dy <= t; dy += t )
		for( int dx = -t; dx <= t; dx += t )
			if( dx || dy )
				DrawNumber( x + dx, y + dy, h, number, 0, 0, 0, a );
	DrawNumber( x, y, h, number, r, g, b, a );
}
