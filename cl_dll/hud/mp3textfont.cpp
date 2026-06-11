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

// big variant: same family from gfx/mp3/font_big.png (allfontsinone, 1024x1024, caps+digits
// at ~45 px cap height) -- used for large HUD text so it doesn't pixelate when upscaled
#define ATLAS_B   1024.0f
#define CAP_REF_B 45.0f
#define TRACK_B   3.2f
#define SPACE_W_B 13.0f

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

// big font (allfontsinone, 1024x1024): digits + caps + the few symbols big HUD text needs.
// Generated from the atlas alpha like s_glyphs (lowercase is not in this block -- lookups
// fall back to the uppercase glyph).
static const Glyph s_glyphsBig[] =
{
	{ '+',  300,  20,  334,  54,  60 }, { '-',  357,  33,  388,  42,  60 },
	{ '.',  391,  47,  409,  64,  60 }, { ':',  815,  27,  829,  60,  60 },
	{ '0',  444,  15,  480,  60,  60 }, { '1',  483,  16,  506,  59,  60 },
	{ '2',  507,  15,  543,  59,  60 }, { '3',  546,  15,  581,  60,  60 },
	{ '4',  584,  16,  622,  59,  60 }, { '5',  625,  15,  661,  60,  60 },
	{ '6',  664,  15,  700,  60,  60 }, { '7',  703,  15,  735,  59,  60 },
	{ '8',  738,  15,  773,  60,  60 }, { '9',  776,  15,  812,  60,  60 },
	{ 'A',    0,  87,   45, 131, 131 }, { 'B',   48,  87,   85, 131, 131 },
	{ 'C',   87,  87,  126, 132, 131 }, { 'D',  129,  87,  169, 131, 131 },
	{ 'E',  172,  87,  203, 131, 131 }, { 'F',  206,  87,  236, 131, 131 },
	{ 'G',  238,  87,  280, 132, 131 }, { 'H',  283,  88,  322, 131, 131 },
	{ 'I',  325,  88,  338, 131, 131 }, { 'J',  340,  88,  363, 131, 131 },
	{ 'K',  366,  88,  405, 131, 131 }, { 'L',  408,  87,  438, 131, 131 },
	{ 'M',  440,  88,  491, 131, 131 }, { 'N',  494,  88,  533, 131, 131 },
	{ 'O',  535,  87,  580, 132, 131 }, { 'P',  583,  87,  618, 131, 131 },
	{ 'Q',  619,  87,  665, 141, 131 }, { 'R',  668,  87,  707, 131, 131 },
	{ 'S',  709,  87,  746, 132, 131 }, { 'T',  748,  87,  784, 131, 131 },
	{ 'U',  786,  88,  824, 132, 131 }, { 'V',  827,  88,  869, 131, 131 },
	{ 'W',  870,  88,  934, 131, 131 }, { 'X',  936,  88,  982, 131, 131 },
	{ 'Y',    0, 160,   43, 203, 203 }, { 'Z',   46, 159,   84, 203, 203 },
};

static const Glyph *s_index[256];
static const Glyph *s_indexBig[256];

// uppercase fallback lookup for the big table (it has no lowercase glyphs)
static const Glyph *BigGlyph( unsigned char c )
{
	const Glyph *g = s_indexBig[c];
	if( !g && c >= 'a' && c <= 'z' )
		g = s_indexBig[c - 'a' + 'A'];
	return g;
}

void CMp3TextFont::Load()
{
	m_iTex = m_iTexBig = 0;
	if( g_iXash )
	{
		texFlags_t f = (texFlags_t)( TF_NOMIPMAP | TF_CLAMP | TF_HAS_ALPHA );
		m_iTex    = gRenderAPI.GL_LoadTexture( "gfx/mp3/font.png", NULL, 0, f );
		m_iTexBig = gRenderAPI.GL_LoadTexture( "gfx/mp3/font_big.png", NULL, 0, f );
	}

	memset( s_index, 0, sizeof( s_index ) );
	for( size_t k = 0; k < sizeof( s_glyphs ) / sizeof( s_glyphs[0] ); k++ )
		s_index[(unsigned char)s_glyphs[k].c] = &s_glyphs[k];

	memset( s_indexBig, 0, sizeof( s_indexBig ) );
	for( size_t k = 0; k < sizeof( s_glyphsBig ) / sizeof( s_glyphsBig[0] ); k++ )
		s_indexBig[(unsigned char)s_glyphsBig[k].c] = &s_glyphsBig[k];
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

int CMp3TextFont::DrawStringOutlined( int x, int by, int H, const char *s, int r, int g, int b, int a )
{
	if( !m_iTex || !s )
		return x;
	int t = H / 12;
	if( t < 1 ) t = 1;             // outline thickness
	for( int dy = -t; dy <= t; dy += t )
		for( int dx = -t; dx <= t; dx += t )
			if( dx || dy )
				DrawString( x + dx, by + dy, H, s, 0, 0, 0, a );
	return DrawString( x, by, H, s, r, g, b, a );
}

// --- big variant: high-res glyphs for large HUD text (banners). Caps-only atlas block;
// lowercase falls back to the uppercase glyph. Falls back to the small font entirely if
// font_big.png is missing, so the asset stays optional. -------------------------------

int CMp3TextFont::StringWidthBig( const char *s, int H ) const
{
	if( !m_iTexBig )
		return StringWidth( s, H );
	if( !s ) return 0;
	float scale = H / CAP_REF_B;
	float pen = 0;
	for( const char *p = s; *p; p++ )
	{
		if( *p == ' ' ) { pen += SPACE_W_B * scale; continue; }
		const Glyph *g = BigGlyph( (unsigned char)*p );
		if( !g ) { pen += SPACE_W_B * scale; continue; }
		pen += ( g->x1 - g->x0 + 1 ) * scale + TRACK_B * scale;
	}
	return (int)pen;
}

int CMp3TextFont::DrawStringBig( int x, int baselineY, int H, const char *s, int r, int g, int b, int a )
{
	if( !m_iTexBig )
		return DrawString( x, baselineY, H, s, r, g, b, a );
	if( !s )
		return x;

	float scale = H / CAP_REF_B;
	float pen = x;

	gRenderAPI.GL_Bind( 0, m_iTexBig );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );
	gEngfuncs.pTriAPI->Begin( TRI_QUADS );

	for( const char *p = s; *p; p++ )
	{
		if( *p == ' ' ) { pen += SPACE_W_B * scale; continue; }
		const Glyph *gl = BigGlyph( (unsigned char)*p );
		if( !gl ) { pen += SPACE_W_B * scale; continue; }

		float gw = ( gl->x1 - gl->x0 + 1 ) * scale;
		float gh = ( gl->y1 - gl->y0 + 1 ) * scale;
		float top = baselineY - ( gl->base - gl->y0 ) * scale;
		float u0 = gl->x0 / ATLAS_B,         v0 = gl->y0 / ATLAS_B;
		float u1 = ( gl->x1 + 1 ) / ATLAS_B, v1 = ( gl->y1 + 1 ) / ATLAS_B;

		gEngfuncs.pTriAPI->TexCoord2f( u0, v0 ); gEngfuncs.pTriAPI->Vertex3f( pen,      top,      0 );
		gEngfuncs.pTriAPI->TexCoord2f( u1, v0 ); gEngfuncs.pTriAPI->Vertex3f( pen + gw, top,      0 );
		gEngfuncs.pTriAPI->TexCoord2f( u1, v1 ); gEngfuncs.pTriAPI->Vertex3f( pen + gw, top + gh, 0 );
		gEngfuncs.pTriAPI->TexCoord2f( u0, v1 ); gEngfuncs.pTriAPI->Vertex3f( pen,      top + gh, 0 );

		pen += gw + TRACK_B * scale;
	}

	gEngfuncs.pTriAPI->End();
	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	return (int)pen;
}
