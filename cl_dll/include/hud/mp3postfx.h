/*
mp3postfx.h - Paranoia-style GL post-processing for the MP3 death screen.
*/
#pragma once
#ifndef MP3POSTFX_H
#define MP3POSTFX_H

// Desaturate the whole frame rendered so far (true luminance grayscale) by copying the
// backbuffer into a texture and redrawing it through an ARB fragment program. Returns false
// when unavailable (non-GL renderer, missing extension) -- caller should fall back to a
// cheaper approximation. Safe to call every frame; all setup is lazy and cached.
bool Mp3PostFX_GrayscaleScreen( void );

#endif // MP3POSTFX_H
