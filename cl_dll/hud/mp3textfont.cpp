/*
mp3textfont.cpp - proportional text rendering with Max Payne 3's HUD font.

gfx/mp3/font.png is the "allfontsintwo" atlas (512x512). The glyph table below was generated from
its alpha channel (scripts/genfont in mp3-rpf-tools side); each entry is a tight atlas rect plus the
row baseline, so glyphs from different rows line up on a common baseline. Text is white in the atlas
so it tints to any color. Cap height in the atlas is 21 px; everything scales from a target height H.
*/
#include "stdio.h"
#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "mp3textfont.h"

CMp3TextFont gMp3Text;

#define ATLAS   512.0f
#define CAP_REF 21.0f   // cap height in atlas px (0/A -> baseline)
#define TRACK   1.5f    // atlas px between glyphs
#define SPACE_W 6.0f    // space advance in atlas px

struct Glyph { char c; short x0, y0, x1, y1, base; };

static const Glyph s_glyphs[] =
{
	{ '!',     1, 135,   5, 156, 156 }, { '"',     8, 135,  15, 143, 156 },
	{ '#',    19, 135,  32, 156, 156 }, { '$',    35, 133,  46, 159, 156 },
	{ '%',    49, 135,  69, 156, 156 }, { '&',    71, 135,  85, 156, 156 },
	{ '\'',   88, 135,  90, 143, 156 }, { '(',    94, 135, 100, 158, 156 },
	{ ')',   104, 135, 110, 158, 156 }, { '+',   114, 141, 129, 156, 156 },
	{ ',',   133, 153, 137, 159, 156 }, { '-',   140, 147, 147, 149, 156 },
	{ '.',   150, 153, 154, 156, 156 }, { '/',   157, 135, 166, 156, 156 },
	{ '0',   169, 135, 179, 156, 156 }, { '1',   182, 135, 188, 156, 156 },
	{ '2',   192, 135, 203, 156, 156 }, { '3',   205, 135, 215, 156, 156 },
	{ '4',   218, 135, 230, 156, 156 }, { '5',   233, 135, 243, 156, 156 },
	{ '6',   247, 135, 257, 156, 156 }, { '7',   260, 135, 270, 156, 156 },
	{ '8',   274, 135, 285, 156, 156 }, { '9',   288, 135, 298, 156, 156 },
	{ ':',   301, 145, 305, 156, 156 }, { ';',   308, 145, 312, 159, 156 },
	{ '<',   315, 141, 330, 156, 156 }, { '=',   333, 144, 348, 153, 156 },
	{ '>',   351, 141, 366, 156, 156 }, { '?',   370, 135, 379, 156, 156 },
	{ '@',   383, 135, 404, 156, 156 }, { 'A',   407, 135, 419, 156, 156 },
	{ 'B',   422, 135, 434, 156, 156 }, { 'C',   436, 135, 448, 156, 156 },
	{ 'D',   450, 135, 462, 156, 156 }, { 'E',   466, 135, 476, 156, 156 },
	{ 'F',   480, 135, 490, 156, 156 }, { 'G',   493, 135, 505, 156, 156 },
	{ 'H',     0, 170,  12, 191, 191 }, { 'I',    15, 170,  19, 191, 191 },
	{ 'J',    23, 170,  29, 191, 191 }, { 'K',    32, 170,  44, 191, 191 },
	{ 'L',    47, 170,  57, 191, 191 }, { 'M',    59, 170,  74, 191, 191 },
	{ 'N',    78, 170,  89, 191, 191 }, { 'O',    92, 170, 104, 191, 191 },
	{ 'P',   107, 170, 119, 191, 191 }, { 'Q',   122, 170, 135, 192, 191 },
	{ 'R',   139, 170, 150, 191, 191 }, { 'S',   154, 170, 165, 191, 191 },
	{ 'T',   169, 170, 180, 190, 191 }, { 'U',   182, 170, 194, 191, 191 },
	{ 'V',   197, 170, 209, 191, 191 }, { 'W',   213, 170, 232, 191, 191 },
	{ 'X',   234, 170, 246, 191, 191 }, { 'Y',   249, 170, 261, 191, 191 },
	{ 'Z',   265, 170, 276, 191, 191 }, { '[',   279, 170, 285, 193, 191 },
	{ '\\',  288, 170, 297, 191, 191 }, { ']',   301, 170, 307, 193, 191 },
	{ '_',   311, 193, 325, 194, 191 }, { 'a',   329, 175, 339, 191, 191 },
	{ 'b',   342, 170, 352, 191, 191 }, { 'c',   355, 175, 365, 191, 191 },
	{ 'd',   368, 170, 378, 191, 191 }, { 'e',   381, 175, 391, 191, 191 },
	{ 'f',   395, 170, 402, 191, 191 }, { 'g',   405, 175, 417, 196, 191 },
	{ 'h',   420, 170, 430, 191, 191 }, { 'i',   433, 170, 437, 191, 191 },
	{ 'j',   441, 170, 446, 196, 191 }, { 'k',   449, 170, 460, 191, 191 },
	{ 'l',   463, 170, 467, 191, 191 }, { 'm',   470, 175, 487, 191, 191 },
	{ 'n',   489, 175, 499, 191, 191 }, { 'o',     0, 210,  10, 226, 226 },
	{ 'p',    13, 210,  23, 231, 226 }, { 'q',    26, 210,  36, 231, 226 },
	{ 'r',    39, 210,  47, 225, 226 }, { 's',    51, 210,  60, 226, 226 },
	{ 't',    63, 206,  70, 226, 226 }, { 'u',    73, 210,  83, 226, 226 },
	{ 'v',    86, 210,  97, 226, 226 }, { 'w',   100, 210, 116, 226, 226 },
	{ 'x',   118, 210, 128, 226, 226 }, { 'y',   131, 211, 142, 231, 226 },
	{ 'z',   145, 210, 154, 226, 226 },
};

static const Glyph *s_index[256];

void CMp3TextFont::Load()
{
	m_iTex = 0;
	if( g_iXash )
	{
		texFlags_t f = (texFlags_t)( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA );
		m_iTex = gRenderAPI.GL_LoadTexture( "gfx/mp3/font.png", NULL, 0, f );
	}

	memset( s_index, 0, sizeof( s_index ) );
	for( size_t k = 0; k < sizeof( s_glyphs ) / sizeof( s_glyphs[0] ); k++ )
		s_index[(unsigned char)s_glyphs[k].c] = &s_glyphs[k];
}

int CMp3TextFont::StringWidth( const char *s, int H ) const
{
	if( !s ) return 0;
	float scale = H / CAP_REF;
	float pen = 0;
	for( const char *p = s; *p; p++ )
	{
		if( *p == ' ' ) { pen += SPACE_W * scale; continue; }
		const Glyph *g = s_index[(unsigned char)*p];
		if( !g ) { pen += SPACE_W * scale; continue; }
		pen += ( g->x1 - g->x0 + 1 ) * scale + TRACK * scale;
	}
	return (int)pen;
}

int CMp3TextFont::DrawString( int x, int baselineY, int H, const char *s, int r, int g, int b, int a )
{
	if( !m_iTex || !s )
		return x;

	float scale = H / CAP_REF;
	float pen = x;

	gRenderAPI.GL_Bind( 0, m_iTex );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );

	for( const char *p = s; *p; p++ )
	{
		if( *p == ' ' ) { pen += SPACE_W * scale; continue; }
		const Glyph *gl = s_index[(unsigned char)*p];
		if( !gl ) { pen += SPACE_W * scale; continue; }

		float gw = ( gl->x1 - gl->x0 + 1 ) * scale;
		float gh = ( gl->y1 - gl->y0 + 1 ) * scale;
		float top = baselineY - ( gl->base - gl->y0 ) * scale;
		float u0 = gl->x0 / ATLAS,        v0 = gl->y0 / ATLAS;
		float u1 = ( gl->x1 + 1 ) / ATLAS, v1 = ( gl->y1 + 1 ) / ATLAS;

		gEngfuncs.pTriAPI->TexCoord2f( u0, v0 ); gEngfuncs.pTriAPI->Vertex3f( pen,      top,      0 );
		gEngfuncs.pTriAPI->TexCoord2f( u1, v0 ); gEngfuncs.pTriAPI->Vertex3f( pen + gw, top,      0 );
		gEngfuncs.pTriAPI->TexCoord2f( u1, v1 ); gEngfuncs.pTriAPI->Vertex3f( pen + gw, top + gh, 0 );
		gEngfuncs.pTriAPI->TexCoord2f( u0, v1 ); gEngfuncs.pTriAPI->Vertex3f( pen,      top + gh, 0 );

		pen += gw + TRACK * scale;
	}

	gEngfuncs.pTriAPI->End();
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	return (int)pen;
}
