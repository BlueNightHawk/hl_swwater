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

CWaterRenderer g_WaterRenderer;

extern CGameStudioModelRenderer g_StudioRenderer;

#define SURF_DRAWTURB 0x10
#define SUBDIVIDE_SIZE 64

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

void CWaterRenderer::AddEntity(cl_entity_s* ent)
{
	if (ent->curstate.skin == CONTENTS_WATER)
	{
		// check for existing ent
		bool found = false;
		for (size_t i = 0; i < m_WaterBuffer.size(); i++)
		{
			if (m_WaterBuffer[i]->index == ent->index)
				found = true;
		}

		if (!found)
		{
			m_WaterBuffer.push_back(ent);
		}

		ent->baseline.effects = EF_NODRAW;
		ent->baseline.renderamt = 0;
		ent->baseline.rendermode = kRenderTransAdd;

		ent->prevstate.effects = EF_NODRAW;
		ent->prevstate.renderamt = 0;
		ent->prevstate.rendermode = kRenderTransAdd;

		ent->curstate.effects = EF_NODRAW;
		ent->curstate.renderamt = 0;
		ent->curstate.rendermode = kRenderTransAdd;
	}
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

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
static void EmitWaterPolys(msurface_t* warp, qboolean reverse)
{
	float *v, nv;
	float s, t, os, ot;
	glpoly_t* p;
	int i;

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

			os = v[3];
			ot = v[4];

			s = os / g_ripple.texturescale;
			t = ot / g_ripple.texturescale;

			s *= (1.0f / SUBDIVIDE_SIZE);
			t *= (1.0f / SUBDIVIDE_SIZE);

			glTexCoord2f(s, t);
			glVertex3f(v[0], v[1], nv);

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

	if (!gEngfuncs.pTriAPI->BoxInPVS(entity->curstate.mins, entity->curstate.maxs))
	{
		return;
	}

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
			R_UploadRipples(psurf->texinfo->texture);
			bUploadedTexture = true;
		}

		EmitWaterPolys(psurf, false);	
	}
}

void CWaterRenderer::Draw()
{
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
	glPushAttrib(GL_TEXTURE_BIT);

	// buz: workaround half-life's bug, when multitexturing left enabled after
	// rendering brush entities
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_COLOR, GL_DST_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	for (size_t i = 0; i < m_WaterBuffer.size(); i++)
	{
		DrawWaterForEntity(m_WaterBuffer[i]);
	}
	glDisable(GL_BLEND);

	glPopAttrib();
}