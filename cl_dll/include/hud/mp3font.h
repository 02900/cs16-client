/*
mp3font.h - render numbers with Max Payne 3's white HUD digit strip
            (gfx/mp3/death_numbers.png, imported by scripts/import_mp3_assets.py).
*/
#pragma once
#ifndef MP3FONT_H
#define MP3FONT_H

class CMp3Font
{
public:
	void Load();                                  // (re)load the strip texture; call from a VidInit
	bool Ready() const { return m_iTex != 0; }

	int  DigitWidth( int digit, int h ) const;    // on-screen width of one digit at pixel height h
	int  NumberWidth( int number, int h ) const;  // on-screen width of a whole number at height h
	void DrawDigit( int x, int y, int h, int digit, int r, int g, int b, int a );
	void DrawNumber( int x, int y, int h, int number, int r, int g, int b, int a );
	// same as DrawNumber but with a dark outline behind it for readability
	void DrawNumberOutlined( int x, int y, int h, int number, int r, int g, int b, int a );

private:
	int m_iTex;
};

extern CMp3Font gMp3Font;

#endif // MP3FONT_H
