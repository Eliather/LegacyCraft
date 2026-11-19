#include "stdafx.h"
#include "Windows64_UIController.h"
#include "..\CustomTextInputModal.h"
#include "..\Minecraft.h"
#include "..\Textures.h"

#define _ENABLEIGGY

ConsoleUIController ui;

void ConsoleUIController::init(S32 w, S32 h)
{
#ifdef _ENABLEIGGY
	preInit(w, h);

	gdraw_funcs = (GDrawFunctions *)RenderManager.GDrawCreateContext(w, h);
	if(!gdraw_funcs)
	{
		app.DebugPrintf("Failed to initialise GDraw!\n");
#ifndef _CONTENT_PACKAGE
		__debugbreak();
#endif
		app.FatalLoadError();
	}

	IggySetGDraw(gdraw_funcs);
	IggyAudioUseDirectSound();

	postInit();
#endif
}

void ConsoleUIController::updateRenderTargets(S32 w, S32 h)
{
#ifdef _ENABLEIGGY
	RenderManager.GDrawSetRendertargetSize(w, h);
	SetScreenDimensions(w, h);
#endif
}

void ConsoleUIController::render()
{
#ifdef _ENABLEIGGY
	RenderManager.GDrawSetTileOrigin(0, 0);
	renderScenes();
	RenderManager.GDrawNoMoreGDrawThisFrame();
	if(CustomTextInputModal::IsOpen())
	{
		CustomTextInputModal::Render(Minecraft::GetInstance());
	}
#endif
}

void ConsoleUIController::beginIggyCustomDraw4J(IggyCustomDrawCallbackRegion *region, CustomDrawData *customDrawRegion)
{
	PIXBeginNamedEvent(0,"Starting Iggy custom draw\n");
	PIXBeginNamedEvent(0,"Gdraw setup");
	RenderManager.GDrawBeginCustomDraw4J(region, customDrawRegion->mat);
	PIXEndNamedEvent();
}

CustomDrawData *ConsoleUIController::setupCustomDraw(UIScene *scene, IggyCustomDrawCallbackRegion *region)
{
	CustomDrawData *customDrawRegion = new CustomDrawData();
	customDrawRegion->x0 = region->x0;
	customDrawRegion->x1 = region->x1;
	customDrawRegion->y0 = region->y0;
	customDrawRegion->y1 = region->y1;

	PIXBeginNamedEvent(0,"Starting Iggy custom draw\n");
	RenderManager.GDrawBeginCustomDraw4J(region, customDrawRegion->mat);
	setupCustomDrawGameStateAndMatrices(scene, customDrawRegion);
	return customDrawRegion;
}

CustomDrawData *ConsoleUIController::calculateCustomDraw(IggyCustomDrawCallbackRegion *region)
{
	CustomDrawData *customDrawRegion = new CustomDrawData();
	customDrawRegion->x0 = region->x0;
	customDrawRegion->x1 = region->x1;
	customDrawRegion->y0 = region->y0;
	customDrawRegion->y1 = region->y1;

	RenderManager.GDrawCalculateCustomDraw4J(region, customDrawRegion->mat);
	return customDrawRegion;
}

void ConsoleUIController::endCustomDraw(IggyCustomDrawCallbackRegion *region)
{
	endCustomDrawGameStateAndMatrices();
	RenderManager.GDrawEndCustomDraw(region);
	PIXEndNamedEvent();
}

void ConsoleUIController::setTileOrigin(S32 xPos, S32 yPos)
{
	RenderManager.GDrawSetTileOrigin(xPos, yPos);
}

GDrawTexture *ConsoleUIController::getSubstitutionTexture(int textureId)
{
	return (GDrawTexture *)RenderManager.GDrawWrappedTextureCreate(textureId);
}

void ConsoleUIController::destroySubstitutionTexture(void *destroyCallBackData, GDrawTexture *handle)
{
	(void)destroyCallBackData;
	RenderManager.GDrawWrappedTextureDestroy(handle);
}

void ConsoleUIController::shutdown()
{
#ifdef _ENABLEIGGY
	RenderManager.GDrawDestroyContext();
#endif
}
