#pragma once

#include <map>

#define RIPPLES_CACHEWIDTH_BITS 7
#define RIPPLES_CACHEWIDTH (1 << RIPPLES_CACHEWIDTH_BITS)
#define RIPPLES_CACHEWIDTH_MASK ((RIPPLES_CACHEWIDTH)-1)
#define RIPPLES_TEXSIZE (RIPPLES_CACHEWIDTH * RIPPLES_CACHEWIDTH)
#define RIPPLES_TEXSIZE_MASK (RIPPLES_TEXSIZE - 1)

static_assert(RIPPLES_TEXSIZE == 0x4000, "fix the algorithm to work with custom resolution");

typedef struct ripple_s
{
	bool enabled;

	double time;
	double oldtime;

	short *curbuf, *oldbuf;
	short buf[2][RIPPLES_TEXSIZE];
	bool update;

	uint32_t texture[RIPPLES_TEXSIZE];
	int gl_texturenum;
	int rippletexturenum;
	float texturescale; // not all textures are 128x128, scale the texcoords down

	std::map<GLuint, uint32_t*> texbuffers;
} ripple_t;

extern ripple_t g_ripple;
extern cvar_t *r_ripple_waves, *r_ripple_world_waveheight;

void R_ResetRipples(void);
void R_InitRipples(void);
void R_AnimateRipples(void);
void R_UpdateRippleTexParams(void);
float R_GetRippleTextureScale();
void R_UploadRipples(struct texture_s* image);