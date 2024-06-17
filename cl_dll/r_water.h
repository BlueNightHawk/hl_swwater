#pragma once

#include <vector>
#include "PlatformHeaders.h"
#include "SDL2/SDL_opengl.h"
#include "com_model.h"

class CWaterRenderer
{
public:
	// cdll_int.cpp -> HUD_Init()
	void Init();
	// cdll_int.cpp -> HUD_VidInit()
	void VidInit();
	
	// tri.cpp -> HUD_DrawNormalTriangles()
	void Draw();

	// entity.cpp -> HUD_AddEntity()
	bool AddEntity(cl_entity_s* ent);

private:
	std::vector<cl_entity_t*> m_WaterBuffer;

	bool m_bisHL25;
	struct model_s* m_pworld;
	int m_visframe;
	int m_framecount;

	PFNGLACTIVETEXTUREARBPROC glActiveTextureARB = NULL;

private:
	void RecursiveDrawWaterWorld(mnode_t* node, model_s* pmodel);
	void DrawWaterForEntity(cl_entity_t* entity);
};

extern CWaterRenderer g_WaterRenderer;