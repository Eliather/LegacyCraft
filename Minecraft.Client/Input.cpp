#include "stdafx.h"
#include "Minecraft.h"
#include "GameMode.h"
#include "..\Minecraft.World\net.minecraft.world.entity.player.h"
#include "..\Minecraft.World\net.minecraft.world.level.h"
#include "..\Minecraft.World\net.minecraft.world.level.storage.h"
#include "Input.h"
#include "..\Minecraft.Client\LocalPlayer.h"
#include "Options.h"
#include "KeyMapping.h"

Input::Input()
{
	xa = 0;
	ya = 0;
	wasJumping = false;
	jumping = false;
	sneaking = false;

	lReset = false;
    rReset = false;
}

void Input::tick(LocalPlayer *player)
{
	Minecraft *pMinecraft=Minecraft::GetInstance();
	int iPad=player->GetXboxPad();
	bool keyboardPlayer = (iPad == ProfileManager.GetPrimaryPad()) && pMinecraft->options != NULL;
	bool keyboardGameplay = keyboardPlayer && (pMinecraft->screen == NULL) && !ui.GetMenuDisplayed(iPad);

	// 4J-PB minecraft movement seems to be the wrong way round, so invert x!
	if( pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LEFT) || pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_RIGHT) )
		xa = -InputManager.GetJoypadStick_LX(iPad);
	else
		xa = 0.0f;
	
	if( pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_FORWARD) || pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_BACKWARD) )
		ya = InputManager.GetJoypadStick_LY(iPad);
	else
		ya = 0.0f;

	if (keyboardGameplay)
	{
		float keyboardXA = 0.0f;
		float keyboardYA = 0.0f;

		if (pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LEFT) && Keyboard::isKeyDown(pMinecraft->options->keyLeft->key))
		{
			keyboardXA += 1.0f;
		}
		if (pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_RIGHT) && Keyboard::isKeyDown(pMinecraft->options->keyRight->key))
		{
			keyboardXA -= 1.0f;
		}
		if (pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_FORWARD) && Keyboard::isKeyDown(pMinecraft->options->keyUp->key))
		{
			keyboardYA += 1.0f;
		}
		if (pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_BACKWARD) && Keyboard::isKeyDown(pMinecraft->options->keyDown->key))
		{
			keyboardYA -= 1.0f;
		}

		xa += keyboardXA;
		ya += keyboardYA;

		if (xa > 1.0f) xa = 1.0f;
		if (xa < -1.0f) xa = -1.0f;
		if (ya > 1.0f) ya = 1.0f;
		if (ya < -1.0f) ya = -1.0f;

		// FIX 4: Normalize diagonal movement to avoid speed boost.
		float moveLen = sqrtf(xa*xa + ya*ya);
		if (moveLen > 1.0f)
		{
			xa /= moveLen;
			ya /= moveLen;
		}
	}

#ifndef _CONTENT_PACKAGE
	if (app.GetFreezePlayers())
	{
		xa = ya = 0.0f;
		player->abilities.flying = true;
	}
#endif
	
    if (!lReset)
    {
        if (xa*xa+ya*ya==0.0f)
        {
            lReset = true;
        }
        xa = ya = 0.0f;
    }

	// 4J - in flying mode, don't actually toggle sneaking
	if(!player->abilities.flying)
	{
		if((player->ullButtonsPressed&(1LL<<MINECRAFT_ACTION_SNEAK_TOGGLE)) && pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_SNEAK_TOGGLE))
		{
			sneaking=!sneaking;
		}
	}

	if(sneaking)
	{
		xa*=0.3f;
		ya*=0.3f;
	}

    float turnSpeed = 50.0f;

	float tx = 0.0f;
	float ty = 0.0f;

	// --- GAMEPAD LOOK (mouse look is handled per-frame in GameRenderer::render) ---
	if( pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LOOK_LEFT) || pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LOOK_RIGHT) )
		tx = InputManager.GetJoypadStick_RX(iPad) * (((float)app.GetGameSettings(iPad,eGameSetting_Sensitivity_InGame)) / 100.0f);

	if( pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LOOK_UP) || pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_LOOK_DOWN) )
		ty = InputManager.GetJoypadStick_RY(iPad) * (((float)app.GetGameSettings(iPad,eGameSetting_Sensitivity_InGame)) / 100.0f);

#ifndef _CONTENT_PACKAGE
	if (app.GetFreezePlayers())	tx = ty = 0.0f;
#endif

	// 4J: WESTY : Invert look Y if required.
	if ( app.GetGameSettings(iPad,eGameSetting_ControlInvertLook) )
	{
		ty = -ty;
	}

	// Gamepad uses quadratic curve (tx*|tx|) for analog feel.
	float turnX = tx * abs(tx);
	float turnY = ty * abs(ty);

	if (!rReset)
	{
		if (turnX*turnX+turnY*turnY==0.0f)
		{
			rReset = true;
		}
		turnX = turnY = 0.0f;
	}

	if (turnX != 0.0f || turnY != 0.0f)
	{
		player->interpolateTurn(turnX * turnSpeed, turnY * turnSpeed);
	}

	unsigned int jump = InputManager.GetValue(iPad, MINECRAFT_ACTION_JUMP);
	bool keyJump = keyboardGameplay && Keyboard::isKeyDown(pMinecraft->options->keyJump->key);
	if( (jump > 0 || keyJump) && pMinecraft->localgameModes[iPad]->isInputAllowed(MINECRAFT_ACTION_JUMP) )
		jumping = true;
	else
 		jumping = false;

#ifndef _CONTENT_PACKAGE
	if (app.GetFreezePlayers())	jumping = false;
#endif
}