/*
mp3palette.h - the Max Payne 3 HUD replica color palette (single source of truth).

Five tones only: black, white, red, and two grays. Every MP3-styled HUD element
(timer/score panel, kill banners, kill feed, ammo, health/armor, over-head names,
minimap ring) picks from here so the replica stays consistent.

The *_R/_G/_B macros exist for code that assigns channels separately (conditional
colors); the bare macro expands to the "r, g, b" triplet for direct use in calls:
    FillRGBABlend( x, y, w, h, MP3_BLACK, 255 );
*/
#pragma once
#ifndef MP3PALETTE_H
#define MP3PALETTE_H

// chip backgrounds, dark digits/text on light chips
#define MP3_BLACK_R 15
#define MP3_BLACK_G 15
#define MP3_BLACK_B 15
#define MP3_BLACK   MP3_BLACK_R, MP3_BLACK_G, MP3_BLACK_B

// light chips, primary text/digits
#define MP3_WHITE_R 235
#define MP3_WHITE_G 235
#define MP3_WHITE_B 235
#define MP3_WHITE   MP3_WHITE_R, MP3_WHITE_G, MP3_WHITE_B

// enemy score/text, victim names, red chips, panic timer
#define MP3_RED_R   220
#define MP3_RED_G   55
#define MP3_RED_B   55
#define MP3_RED     MP3_RED_R, MP3_RED_G, MP3_RED_B

// blood/damage tint — deep saturated red, near-zero green and blue
#define MP3_DARK_RED_R  190
#define MP3_DARK_RED_G  18
#define MP3_DARK_RED_B  18
#define MP3_DARK_RED    MP3_DARK_RED_R, MP3_DARK_RED_G, MP3_DARK_RED_B

// light gray: timer chip, armor meter fill, damage-flash (ghost) bar chunk
#define MP3_GRAY_LT_R 205
#define MP3_GRAY_LT_G 205
#define MP3_GRAY_LT_B 205
#define MP3_GRAY_LT   MP3_GRAY_LT_R, MP3_GRAY_LT_G, MP3_GRAY_LT_B

// dark gray: secondary text (ammo reserve, weapon names), silhouette health fill
#define MP3_GRAY_DK_R 155
#define MP3_GRAY_DK_G 155
#define MP3_GRAY_DK_B 155
#define MP3_GRAY_DK   MP3_GRAY_DK_R, MP3_GRAY_DK_G, MP3_GRAY_DK_B

#endif // MP3PALETTE_H
