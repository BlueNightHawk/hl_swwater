// 2024
// BlueNightHawk - Software Water 
// Credits :
// https://github.com/a1batross : Original software water code
// https://github.com/FWGS/xash3d-fwgs
// Bacontsu : Code references
// Buz : Code references

#include "PlatformHeaders.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"

#include <algorithm>

#include "hud.h"
#include "cl_util.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"

#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"

#include "r_ripples.h"
#include "r_water.h"

#include "tri.h"
#include "triangleapi.h"

#include "particleman.h"
#include "particleman_internal.h"

CWaterRenderer g_WaterRenderer;

extern CGameStudioModelRenderer g_StudioRenderer;

#define SURF_PLANEBACK 2
#define SURF_DRAWSKY 4
#define SURF_DRAWSPRITE 8
#define SURF_DRAWTURB 0x10
#define SURF_DRAWTILED 0x20
#define SURF_DRAWBACKGROUND 0x40
#define SURF_UNDERWATER 0x80
#define SURF_DONTWARP 0x100
#define BACKFACE_EPSILON 0.01

#define SUBDIVIDE_SIZE 64

// 0-2 are axial planes
#define PLANE_X 0
#define PLANE_Y 1
#define PLANE_Z 2

void CWaterRenderer::Init()
{
	// find out engine version
	auto ver = gEngfuncs.pfnGetCvarPointer("sv_version");
	char version[512];
	int numcommas = 0;
	strcpy(version, ver->string);
	const char* pVer = version;

	while (1)
	{
		if (numcommas == 2)
		{
			if (atoi(pVer) > 8684)
			{
				m_bisHL25 = true;
				gEngfuncs.Con_DPrintf("HL25 Detected! Engine Build: %s\n", version);
			}
			break;
		}
		else if (*pVer == ',')
			numcommas++;

		pVer++;
	}

	R_InitRipples();

	if (NULL == glActiveTextureARB)
		glActiveTextureARB = (PFNGLACTIVETEXTUREARBPROC)SDL_GL_GetProcAddress("glActiveTextureARB");
}

void CWaterRenderer::VidInit()
{
	R_ResetRipples();

	m_WaterBuffer.clear();
}

bool CWaterRenderer::AddEntity(cl_entity_s* ent)
{
	if ((int)r_ripples->value <= 0)
		return false;

	if ((ent->baseline.eflags & EF_NODRAW) != 0)
	{
		return true;
	}

	if (ent->curstate.skin == CONTENTS_WATER)
	{
		if (std::find(m_WaterBuffer.begin(), m_WaterBuffer.end(), ent) == m_WaterBuffer.end())
			m_WaterBuffer.push_back(ent);

		ent->baseline.eflags |= EF_NODRAW;
		return true;
	}
	return false;
}

static mleaf_t* Mod_PointInLeaf(Vector p, model_t* model) // quake's func
{
	mnode_t* node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t*)node;
		mplane_t* plane = node->plane;
		float d = DotProduct(p, plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL; // never reached
}

static void R_RotateForEntity(cl_entity_t* e)
{
	glTranslatef(e->origin[0], e->origin[1], e->origin[2]);
	glRotatef(e->angles[1], 0, 0, 1);
	glRotatef(-e->angles[0], 0, 1, 0);
	glRotatef(e->angles[2], 1, 0, 0);
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
static texture_t *R_TextureAnimation (texture_t *base, cl_entity_t *ent)
{
	int		reletive;
	int		count;

	if (ent->curstate.frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}
	
	if (!base->anim_total)
		return base;

	reletive = (int)(gEngfuncs.GetClientTime()*10) % base->anim_total;

	count = 0;	
	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;
		if (!base)
			gEngfuncs.Con_DPrintf ("R_TextureAnimation: broken cycle\n");
		if (++count > 100)
			gEngfuncs.Con_DPrintf("R_TextureAnimation: infinite cycle\n");
	}

	return base;
}


/*
===============
CL_FxBlend
===============
*/
int CL_FxBlend(cl_entity_t* e)
{
	int blend = 0;
	float offset, dist;
	Vector tmp;

	offset = ((int)e->index) * 363.0f; // Use ent index to de-sync these fx

	switch (e->curstate.renderfx)
	{
	case kRenderFxPulseSlowWide:
		blend = e->curstate.renderamt + 0x40 * sin(gEngfuncs.GetClientTime() * 2 + offset);
		break;
	case kRenderFxPulseFastWide:
		blend = e->curstate.renderamt + 0x40 * sin(gEngfuncs.GetClientTime() * 8 + offset);
		break;
	case kRenderFxPulseSlow:
		blend = e->curstate.renderamt + 0x10 * sin(gEngfuncs.GetClientTime() * 2 + offset);
		break;
	case kRenderFxPulseFast:
		blend = e->curstate.renderamt + 0x10 * sin(gEngfuncs.GetClientTime() * 8 + offset);
		break;
	case kRenderFxFadeSlow:
		//if (RP_NORMALPASS())
		{
			if (e->curstate.renderamt > 0)
				e->curstate.renderamt -= 1;
			else
				e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxFadeFast:
		//if (RP_NORMALPASS())
		{
			if (e->curstate.renderamt > 3)
				e->curstate.renderamt -= 4;
			else
				e->curstate.renderamt = 0;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidSlow:
		//if (RP_NORMALPASS())
		{
			if (e->curstate.renderamt < 255)
				e->curstate.renderamt += 1;
			else
				e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxSolidFast:
		//if (RP_NORMALPASS())
		{
			if (e->curstate.renderamt < 252)
				e->curstate.renderamt += 4;
			else
				e->curstate.renderamt = 255;
		}
		blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeSlow:
		blend = 20 * sin(gEngfuncs.GetClientTime() * 4 + offset);
		if (blend < 0)
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFast:
		blend = 20 * sin(gEngfuncs.GetClientTime() * 16 + offset);
		if (blend < 0)
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxStrobeFaster:
		blend = 20 * sin(gEngfuncs.GetClientTime() * 36 + offset);
		if (blend < 0)
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerSlow:
		blend = 20 * (sin(gEngfuncs.GetClientTime() * 2) + sin(gEngfuncs.GetClientTime() * 17 + offset));
		if (blend < 0)
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxFlickerFast:
		blend = 20 * (sin(gEngfuncs.GetClientTime() * 16) + sin(gEngfuncs.GetClientTime() * 23 + offset));
		if (blend < 0)
			blend = 0;
		else
			blend = e->curstate.renderamt;
		break;
	case kRenderFxHologram:
	case kRenderFxDistort:
		VectorCopy(e->origin, tmp);
		VectorSubtract(tmp, g_StudioRenderer.m_vRenderOrigin, tmp);
		dist = DotProduct(tmp, g_StudioRenderer.m_vNormal);

		// turn off distance fade
		if (e->curstate.renderfx == kRenderFxDistort)
			dist = 1;

		if (dist <= 0)
		{
			blend = 0;
		}
		else
		{
			e->curstate.renderamt = 180;
			if (dist <= 100)
				blend = e->curstate.renderamt;
			else
				blend = (int)((1.0f - (dist - 100) * (1.0f / 400.0f)) * e->curstate.renderamt);
			blend += gEngfuncs.pfnRandomLong(-32, 31);
		}
		break;
	default:
		blend = e->curstate.renderamt;
		break;
	}

	blend = std::clamp(blend, 0 ,255);

	return blend;
}

static void R_SetRenderMode(cl_entity_t* e)
{
	float r_blend = 1.0f;
	// handle studiomodels with custom rendermodes on texture
	if (e->curstate.rendermode != kRenderNormal)
		r_blend = CL_FxBlend(e) / 255.0f;
	else
		r_blend = 1.0f; // draw as solid but sorted by distance

	switch (e->curstate.rendermode)
	{
	case kRenderNormal:
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	case kRenderTransColor:
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4ub(e->curstate.rendercolor.r, e->curstate.rendercolor.g, e->curstate.rendercolor.b, e->curstate.renderamt);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		break;
	case kRenderTransAdd:
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f(r_blend, r_blend, r_blend, 1.0f);
		glBlendFunc(GL_ONE, GL_ONE);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		break;
	case kRenderTransAlpha:
		glEnable(GL_ALPHA_TEST);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glDisable(GL_BLEND);
		glAlphaFunc(GL_GREATER, 0.25f);
		break;
	default:
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1.0f, 1.0f, 1.0f, r_blend);
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
		break;
	}
}

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
static void EmitWaterPolys(msurface_t* warp, qboolean reverse, cl_entity_t *ent = nullptr)
{
	float *v, nv;
	float s, t, os, ot;
	glpoly_t* p;
	int i;

	Vector origin;

	if (!warp->polys)
		return;

	for (p = warp->polys; p; p = p->next)
	{
		if (p->numverts > 0)
			p->numverts *= -1;

		if (reverse)
			v = p->verts[0] + (p->numverts - 1) * VERTEXSIZE;
		else
			v = p->verts[0];

		glDisable(GL_CULL_FACE);
		glBegin(GL_POLYGON);

		for (i = 0; i < (-p->numverts); i++)
		{
			nv = v[2];

			if (!ent || nv > (ent->curstate.maxs.z - 5))
			{
				os = v[3];
				ot = v[4];

				s = os / g_ripple.texturescale;
				t = ot / g_ripple.texturescale;

				s *= (1.0f / SUBDIVIDE_SIZE);
				t *= (1.0f / SUBDIVIDE_SIZE);

				glTexCoord2f(s, t);
				glVertex3f(v[0], v[1], nv);
			}
			if (reverse)
				v -= VERTEXSIZE;
			else
				v += VERTEXSIZE;
		}

		glEnd();
		glEnable(GL_CULL_FACE);
	}
}

void CWaterRenderer::RecursiveDrawWaterWorld(mnode_t* node, model_s* pmodel)
{
	if (node->contents == CONTENTS_SOLID)
		return;

	if (node->visframe != m_visframe)
		return;

	if (node->contents < 0)
		return; // faces already marked by engine

	// recurse down the children, Order doesn't matter
	RecursiveDrawWaterWorld(node->children[0], pmodel);
	RecursiveDrawWaterWorld(node->children[1], pmodel);

	if (!gEngfuncs.pTriAPI->BoxInPVS(node->minmaxs, node->minmaxs + 3))
	{
		return;
	}

	bool bUploadedTexture = false;

	// draw stuff
	int c = node->numsurfaces;
	if (c)
	{
		// HL25 used a different struct for msurface_t, causing crashes, thanks again valve
		msurface_t* surf = (msurface_t*)pmodel->surfaces + node->firstsurface;
		msurface_HL25_t* surf25 = (msurface_HL25_t*)pmodel->surfaces + node->firstsurface;

		for (; c; c--)
		{
			if (m_bisHL25)
			{
				surf = (msurface_t*)surf25;
				surf25++;
			}
			else
			{
				surf++;
			}

			if (surf->visframe != m_framecount)
				continue;

			if ((surf->flags & SURF_DRAWTURB) == 0)
				continue;

			if (!bUploadedTexture)
			{
				R_UploadRipples(surf->texinfo->texture);
				bUploadedTexture = true;
			}

			EmitWaterPolys(surf, false);
		}
	}
}

void CWaterRenderer::DrawWaterForEntity(cl_entity_t* entity)
{
	// HL25 used a different struct for msurface_t, causing crashes, thanks again valve
	msurface_t* psurf = (msurface_t*)gEngfuncs.GetEntityByIndex(0)->model->surfaces + entity->model->firstmodelsurface;
	msurface_HL25_t* psurf25 = (msurface_HL25_t*)gEngfuncs.GetEntityByIndex(0)->model->surfaces + entity->model->firstmodelsurface;

	bool bUploadedTexture = false;

	Vector mins = entity->origin + entity->curstate.mins;
	Vector maxs = entity->origin + entity->curstate.maxs;
	//Vector origin = entity->origin + (entity->curstate.mins + entity->curstate.maxs) * 0.5f;

	if (!gEngfuncs.pTriAPI->BoxInPVS(mins, maxs))
	{
		return;
	}

	glPushMatrix();
	R_RotateForEntity(entity);
	R_SetRenderMode(entity);

	for (int i = 0; i < entity->model->nummodelsurfaces; i++)
	{
		if (m_bisHL25)
		{
			psurf = (msurface_t*)psurf25;
			psurf25++;
		}
		else
		{
			psurf++;
		}

		if (!bUploadedTexture)
		{
			auto tex = R_TextureAnimation(psurf->texinfo->texture, entity);
			R_UploadRipples(tex);
			bUploadedTexture = true;
		}

		EmitWaterPolys(psurf, false, entity);	
	}

	glDisable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glPopMatrix();
}

void CWaterRenderer::Draw()
{
	if ((int)r_ripples->value <= 0)
		return;

	glPushAttrib(GL_TEXTURE_BIT);

	// buz: workaround half-life's bug, when multitexturing left enabled after
	// rendering brush entities
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);

	R_AnimateRipples();

	m_pworld = gEngfuncs.GetEntityByIndex(0)->model;

	mleaf_t* leaf = Mod_PointInLeaf(g_StudioRenderer.m_vRenderOrigin, m_pworld);
	m_visframe = leaf->visframe;

	// get current frame number
	m_framecount = g_StudioRenderer.m_nFrameCount;

	// draw world
	RecursiveDrawWaterWorld(m_pworld->nodes, m_pworld);

	glPopAttrib();
}

void CWaterRenderer::DrawTransparent()
{
	if ((int)r_ripples->value <= 0)
		return;

	glPushAttrib(GL_TEXTURE_BIT);
	for (size_t i = 0; i < m_WaterBuffer.size(); i++)
	{
		DrawWaterForEntity(m_WaterBuffer[i]);
	}
	glPopAttrib();
}