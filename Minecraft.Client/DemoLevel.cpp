#include "stdafx.h"
#include "DemoLevel.h"
#include "..\Minecraft.World\net.minecraft.world.level.storage.h"

DemoLevel::DemoLevel(shared_ptr<LevelStorage> levelStorage, const wstring& levelName) : Level(levelStorage, levelName, (LevelSettings *)(uintptr_t)DEMO_LEVEL_SEED)
{
}

DemoLevel::DemoLevel(Level *level, Dimension *dimension): Level(level, dimension)
{
}

void DemoLevel::setInitialSpawn()
{
	levelData->setSpawn(DEMO_SPAWN_X, DEMO_SPAWN_Y, DEMO_SPAWN_Z);
}