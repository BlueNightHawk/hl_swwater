// https://github.com/a1batross : Original software water code
// https://github.com/FWGS/xash3d-fwgs

#include "PlatformHeaders.h"
#include "hud.h"
#include "cl_util.h"

#include "SDL2/SDL_opengl.h"

#include "studio.h"
#include "com_model.h"
#include "r_ripples.h"

cvar_t *r_ripples = nullptr, *r_ripple_updatetime = nullptr, *r_ripple_spawntime = nullptr, *r_ripple_waves = nullptr;
cvar_t* gl_texturemode;

ripple_t g_ripple;

/*
============================================================

	HALF-LIFE SOFTWARE WATER

============================================================
*/
void R_ResetRipples(void)
{
	g_ripple.enabled = (int)r_ripples->value > 0;

	g_ripple.curbuf = g_ripple.buf[0];
	g_ripple.oldbuf = g_ripple.buf[1];
	g_ripple.time = g_ripple.oldtime = gEngfuncs.GetClientTime() - 0.1;
	memset(g_ripple.buf, 0, sizeof(g_ripple.buf));

	R_UpdateRippleTexParams();

	for (auto &f : g_ripple.texbuffers)
	{
		delete[] f.second;
	}

	g_ripple.texbuffers.clear();
}

void R_InitRipples(void)
{
	r_ripples = gEngfuncs.pfnRegisterVariable("r_ripples", "1", FCVAR_ARCHIVE);
	r_ripple_updatetime = gEngfuncs.pfnRegisterVariable("r_ripple_updatetime", "0.05", FCVAR_ARCHIVE);
	r_ripple_spawntime = gEngfuncs.pfnRegisterVariable("r_ripple_spawntime", "0.1", FCVAR_ARCHIVE);
	r_ripple_waves = gEngfuncs.pfnRegisterVariable("r_ripple_waves", "1", FCVAR_ARCHIVE);

	gl_texturemode = gEngfuncs.pfnGetCvarPointer("gl_texturemode");

	g_ripple.enabled = false;

	glGenTextures(1, (GLuint*)&g_ripple.rippletexturenum);
	R_UpdateRippleTexParams();
}

static void R_SwapBufs(void)
{
	short* tempbufp = g_ripple.curbuf;
	g_ripple.curbuf = g_ripple.oldbuf;
	g_ripple.oldbuf = tempbufp;
}

static void R_SpawnNewRipple(int x, int y, short val)
{
#define PIXEL(x, y) (((x)&RIPPLES_CACHEWIDTH_MASK) + (((y)&RIPPLES_CACHEWIDTH_MASK) << 7))
	g_ripple.oldbuf[PIXEL(x, y)] += val;

	val >>= 2;
	g_ripple.oldbuf[PIXEL(x + 1, y)] += val;
	g_ripple.oldbuf[PIXEL(x - 1, y)] += val;
	g_ripple.oldbuf[PIXEL(x, y + 1)] += val;
	g_ripple.oldbuf[PIXEL(x, y - 1)] += val;
#undef PIXEL
}

static void R_RunRipplesAnimation(const short* oldbuf, short* pbuf)
{
	size_t i = 0;
	const int w = RIPPLES_CACHEWIDTH;
	const int m = RIPPLES_TEXSIZE_MASK;

	for (i = w; i < m + w; i++, pbuf++)
	{
		*pbuf = (((int)oldbuf[(i - (w * 2)) & m] + (int)oldbuf[(i - (w + 1)) & m] + (int)oldbuf[(i - (w - 1)) & m] + (int)oldbuf[(i)&m]) >> 1) - (int)*pbuf;

		*pbuf -= (*pbuf >> 6);
	}
}

static int MostSignificantBit(unsigned int v)
{
#if __GNUC__
	return 31 - __builtin_clz(v);
#else
	int i;
	for (i = 0, v >>= 1; v; v >>= 1, i++)
		;
	return i;
#endif
}

void R_AnimateRipples(void)
{
	double frametime = gEngfuncs.GetClientTime() - g_ripple.time;

	g_ripple.update = g_ripple.enabled && frametime >= r_ripple_updatetime->value;

	if (!g_ripple.update)
		return;

	g_ripple.time = gEngfuncs.GetClientTime();

	R_SwapBufs();

	if (g_ripple.time - g_ripple.oldtime > r_ripple_spawntime->value)
	{
		int x, y, val;

		g_ripple.oldtime = g_ripple.time;

		x = rand() & 0x7fff;
		y = rand() & 0x7fff;
		val = rand() & 0x3ff;

		R_SpawnNewRipple(x, y, val);
	}

	R_RunRipplesAnimation(g_ripple.oldbuf, g_ripple.curbuf);
}

void R_UpdateRippleTexParams(void)
{
	glBindTexture(GL_TEXTURE_2D, g_ripple.rippletexturenum);

	if (!strnicmp(gl_texturemode->string, "GL_NEAREST", 10))
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
}

float R_GetRippleTextureScale()
{
	return g_ripple.texturescale;
}

uint32_t* R_GetPixelBuffer(GLuint tex)
{
	uint32_t* pixels = nullptr;

	auto it = g_ripple.texbuffers.find(tex);

	if (it == g_ripple.texbuffers.end())
	{
		pixels = new uint32_t[RIPPLES_TEXSIZE];

		glBindTexture(GL_TEXTURE_2D, tex);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		g_ripple.texbuffers.insert(std::make_pair(tex, pixels));
	}
	else
	{
		pixels = it->second;
	}

	return pixels;
}

void R_UploadRipples(struct texture_s* image)
{
	uint32_t *pixels;
	int wbits, wmask, wshft;

	pixels = R_GetPixelBuffer(image->gl_texturenum);

	// discard unuseful textures
	if (!g_ripple.enabled || image->width > RIPPLES_CACHEWIDTH || image->width != image->height)
	{
		return;
	}

	glBindTexture(GL_TEXTURE_2D, g_ripple.rippletexturenum);

	// no updates this frame
	if (!g_ripple.update && image->gl_texturenum == g_ripple.gl_texturenum)
		return;

	g_ripple.gl_texturenum = image->gl_texturenum;
	if (r_ripples->value < 2.0f)
	{
		g_ripple.texturescale = V_max(1.0f, image->width / 64.0f);
	}
	else
	{
		g_ripple.texturescale = 1.0f;
	}

	wbits = MostSignificantBit(image->width);
	wshft = 7 - wbits;
	wmask = image->width - 1;

	for (unsigned int y = 0; y < image->height; y++)
	{
		int ry = y << (7 + wshft);
		unsigned int x;

		for (x = 0; x < image->width; x++)
		{
			int rx = x << wshft;
			int val = g_ripple.curbuf[ry + rx] >> 4;

			int py = (y - val) & wmask;
			int px = (x + val) & wmask;
			int p = (py << wbits) + px;

			g_ripple.texture[(y << wbits) + x] = pixels[p];
		}
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->width, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, g_ripple.texture);
}