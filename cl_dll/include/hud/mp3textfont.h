/*
mp3textfont.h - proportional text rendering with Max Payne 3's HUD font
                (gfx/mp3/font.png == allfontsintwo, imported by import_mp3_assets.py).
*/
#pragma once
#ifndef MP3TEXTFONT_H
#define MP3TEXTFONT_H

class CMp3TextFont
{
public:
	void Load();                                  // (re)load the atlas + build the lookup; from a VidInit
	bool Ready() const { return m_iTex != 0; }

	int  StringWidth( const char *s, int H ) const;   // on-screen width at cap height H (px)
	// draw a string with its baseline at y; returns the pen x after the string
	int  DrawString( int x, int baselineY, int H, const char *s, int r, int g, int b, int a );
	// same as DrawString but with a dark outline behind it for readability
	int  DrawStringOutlined( int x, int baselineY, int H, const char *s, int r, int g, int b, int a );

private:
	int m_iTex;
};

extern CMp3TextFont gMp3Text;

#endif // MP3TEXTFONT_H
