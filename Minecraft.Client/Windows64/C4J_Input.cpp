#include "stdafx.h"
#include "4JLibs/inc/4J_Input.h"
#include <xinput.h>
#include <math.h>

// Singleton instance
C_4JInput InputManager;

static unsigned int s_joypadMaps[4][256] = { {0} };
static unsigned char s_padMapVal[4] = { 0 };
static bool s_menuDisplayed[4] = { false };
static float s_repeatDelay = 0.3f;
static float s_repeatRate = 0.1f;
static unsigned int s_deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
static unsigned int s_movementRangeMax = 32767;
static float s_sensitivity[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static unsigned int s_axisMap[4][4] = { {0,1,2,3}, {0,1,2,3}, {0,1,2,3}, {0,1,2,3} };
static unsigned int s_trigMap[4][2] = { {0,1}, {0,1}, {0,1}, {0,1} };

static bool s_connected[4] = { false, false, false, false };
static unsigned int s_currButtons[4] = { 0 };
static unsigned int s_prevButtons[4] = { 0 };
static float s_rawAxes[4][4] = { {0.0f} };
static unsigned char s_rawTrigs[4][2] = { {0} };
static ULONGLONG s_lastActivityTick[4] = { 0 };
static ULONGLONG s_nextRepeatTick[4][256] = { {0} };

namespace
{
	float NormalizeThumbAxis(SHORT rawVal, unsigned int deadzone, unsigned int maxRange)
	{
		float dz = (float)deadzone;
		float mx = (maxRange > deadzone) ? (float)maxRange : 32767.0f;
		float v = (float)rawVal;
		float av = fabsf(v);
		if (av <= dz)
		{
			return 0.0f;
		}
		float norm = (av - dz) / (mx - dz);
		if (norm > 1.0f) norm = 1.0f;
		return (v < 0.0f) ? -norm : norm;
	}

	unsigned int BuildMappedButtons(const XINPUT_GAMEPAD &gp)
	{
		unsigned int mask = 0;
		if (gp.wButtons & XINPUT_GAMEPAD_A)              mask |= _360_JOY_BUTTON_A;
		if (gp.wButtons & XINPUT_GAMEPAD_B)              mask |= _360_JOY_BUTTON_B;
		if (gp.wButtons & XINPUT_GAMEPAD_X)              mask |= _360_JOY_BUTTON_X;
		if (gp.wButtons & XINPUT_GAMEPAD_Y)              mask |= _360_JOY_BUTTON_Y;
		if (gp.wButtons & XINPUT_GAMEPAD_START)          mask |= _360_JOY_BUTTON_START;
		if (gp.wButtons & XINPUT_GAMEPAD_BACK)           mask |= _360_JOY_BUTTON_BACK;
		if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) mask |= _360_JOY_BUTTON_RB;
		if (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  mask |= _360_JOY_BUTTON_LB;
		if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    mask |= _360_JOY_BUTTON_RTHUMB;
		if (gp.wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     mask |= _360_JOY_BUTTON_LTHUMB;
		if (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP)        mask |= _360_JOY_BUTTON_DPAD_UP;
		if (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      mask |= _360_JOY_BUTTON_DPAD_DOWN;
		if (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      mask |= _360_JOY_BUTTON_DPAD_LEFT;
		if (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     mask |= _360_JOY_BUTTON_DPAD_RIGHT;

		if (gp.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)  mask |= _360_JOY_BUTTON_LT;
		if (gp.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) mask |= _360_JOY_BUTTON_RT;

		const SHORT stickThresh = 16384;
		if (gp.sThumbLX > stickThresh)  mask |= _360_JOY_BUTTON_LSTICK_RIGHT;
		if (gp.sThumbLX < -stickThresh) mask |= _360_JOY_BUTTON_LSTICK_LEFT;
		if (gp.sThumbLY > stickThresh)  mask |= _360_JOY_BUTTON_LSTICK_UP;
		if (gp.sThumbLY < -stickThresh) mask |= _360_JOY_BUTTON_LSTICK_DOWN;
		if (gp.sThumbRX > stickThresh)  mask |= _360_JOY_BUTTON_RSTICK_RIGHT;
		if (gp.sThumbRX < -stickThresh) mask |= _360_JOY_BUTTON_RSTICK_LEFT;
		if (gp.sThumbRY > stickThresh)  mask |= _360_JOY_BUTTON_RSTICK_UP;
		if (gp.sThumbRY < -stickThresh) mask |= _360_JOY_BUTTON_RSTICK_DOWN;

		return mask;
	}

	unsigned int GetActionMask(int iPad, unsigned char ucAction)
	{
		if (iPad < 0 || iPad >= 4) return 0;
		unsigned char mapIdx = s_padMapVal[iPad];
		if (mapIdx >= 4) mapIdx = 0;
		return s_joypadMaps[mapIdx][ucAction];
	}
}

void C_4JInput::Initialise(int iInputStateC, unsigned char ucMapC, unsigned char ucActionC, unsigned char ucMenuActionC)
{
	(void)iInputStateC;
	(void)ucMapC;
	(void)ucActionC;
	(void)ucMenuActionC;
	ULONGLONG now = GetTickCount64();
	for (int i = 0; i < 4; ++i)
	{
		s_lastActivityTick[i] = now;
		s_currButtons[i] = 0;
		s_prevButtons[i] = 0;
	}
}

void C_4JInput::Tick(void)
{
	ULONGLONG now = GetTickCount64();
	for (int i = 0; i < 4; ++i)
	{
		s_prevButtons[i] = s_currButtons[i];

		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(state));
		if (XInputGetState((DWORD)i, &state) == ERROR_SUCCESS)
		{
			s_connected[i] = true;
			s_currButtons[i] = BuildMappedButtons(state.Gamepad);
			s_rawAxes[i][AXIS_MAP_LX] = NormalizeThumbAxis(state.Gamepad.sThumbLX, s_deadzone, s_movementRangeMax);
			s_rawAxes[i][AXIS_MAP_LY] = NormalizeThumbAxis(state.Gamepad.sThumbLY, s_deadzone, s_movementRangeMax);
			s_rawAxes[i][AXIS_MAP_RX] = NormalizeThumbAxis(state.Gamepad.sThumbRX, s_deadzone, s_movementRangeMax);
			s_rawAxes[i][AXIS_MAP_RY] = NormalizeThumbAxis(state.Gamepad.sThumbRY, s_deadzone, s_movementRangeMax);
			s_rawTrigs[i][TRIGGER_MAP_0] = state.Gamepad.bLeftTrigger;
			s_rawTrigs[i][TRIGGER_MAP_1] = state.Gamepad.bRightTrigger;

			if (s_currButtons[i] != 0 ||
				fabsf(s_rawAxes[i][0]) > 0.01f || fabsf(s_rawAxes[i][1]) > 0.01f ||
				fabsf(s_rawAxes[i][2]) > 0.01f || fabsf(s_rawAxes[i][3]) > 0.01f ||
				s_rawTrigs[i][0] > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
				s_rawTrigs[i][1] > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			{
				s_lastActivityTick[i] = now;
			}
		}
		else
		{
			s_connected[i] = false;
			s_currButtons[i] = 0;
			s_rawAxes[i][0] = s_rawAxes[i][1] = s_rawAxes[i][2] = s_rawAxes[i][3] = 0.0f;
			s_rawTrigs[i][0] = s_rawTrigs[i][1] = 0;
		}
	}
}

void C_4JInput::SetDeadzoneAndMovementRange(unsigned int uiDeadzone, unsigned int uiMovementRangeMax)
{
	s_deadzone = (uiDeadzone > 0 && uiDeadzone < 30000) ? uiDeadzone : XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	s_movementRangeMax = (uiMovementRangeMax > s_deadzone) ? uiMovementRangeMax : 32767;
}

void C_4JInput::SetGameJoypadMaps(unsigned char ucMap, unsigned char ucAction, unsigned int uiActionVal)
{
	if (ucMap < 4)
	{
		s_joypadMaps[ucMap][ucAction] = uiActionVal;
	}
}

unsigned int C_4JInput::GetGameJoypadMaps(unsigned char ucMap, unsigned char ucAction)
{
	if (ucMap < 4)
	{
		return s_joypadMaps[ucMap][ucAction];
	}
	return 0;
}

void C_4JInput::SetJoypadMapVal(int iPad, unsigned char ucMap)
{
	if (iPad >= 0 && iPad < 4)
	{
		s_padMapVal[iPad] = ucMap;
	}
}

unsigned char C_4JInput::GetJoypadMapVal(int iPad)
{
	if (iPad >= 0 && iPad < 4)
	{
		return s_padMapVal[iPad];
	}
	return 0;
}

void C_4JInput::SetJoypadSensitivity(int iPad, float fSensitivity)
{
	if (iPad >= 0 && iPad < 4)
	{
		s_sensitivity[iPad] = fSensitivity;
	}
}

unsigned int C_4JInput::GetValue(int iPad, unsigned char ucAction, bool bRepeat)
{
	if (iPad < 0 || iPad >= 4) return 0;
	unsigned int actionMask = GetActionMask(iPad, ucAction);
	if (actionMask == 0) return 0;

	bool isDown = (s_currButtons[iPad] & actionMask) != 0;
	if (!isDown)
	{
		s_nextRepeatTick[iPad][ucAction] = 0;
		return 0;
	}

	if (!bRepeat)
	{
		return 1;
	}

	ULONGLONG now = GetTickCount64();
	bool wasDown = (s_prevButtons[iPad] & actionMask) != 0;
	if (!wasDown)
	{
		ULONGLONG delayMs = (ULONGLONG)(s_repeatDelay * 1000.0f);
		if (delayMs < 50) delayMs = 50;
		s_nextRepeatTick[iPad][ucAction] = now + delayMs;
		return 1;
	}

	if (now >= s_nextRepeatTick[iPad][ucAction])
	{
		ULONGLONG rateMs = (ULONGLONG)(s_repeatRate * 1000.0f);
		if (rateMs < 30) rateMs = 30;
		s_nextRepeatTick[iPad][ucAction] = now + rateMs;
		return 1;
	}

	return 0;
}

bool C_4JInput::ButtonPressed(int iPad, unsigned char ucAction)
{
	if (iPad < 0 || iPad >= 4) return false;
	unsigned int newlyPressed = s_currButtons[iPad] & ~s_prevButtons[iPad];
	if (ucAction == 255)
	{
		return newlyPressed != 0;
	}
	return (newlyPressed & GetActionMask(iPad, ucAction)) != 0;
}

bool C_4JInput::ButtonReleased(int iPad, unsigned char ucAction)
{
	if (iPad < 0 || iPad >= 4) return false;
	unsigned int newlyReleased = (~s_currButtons[iPad]) & s_prevButtons[iPad];
	if (ucAction == 255)
	{
		return newlyReleased != 0;
	}
	return (newlyReleased & GetActionMask(iPad, ucAction)) != 0;
}

bool C_4JInput::ButtonDown(int iPad, unsigned char ucAction)
{
	if (iPad < 0 || iPad >= 4) return false;
	if (ucAction == 255)
	{
		return s_currButtons[iPad] != 0;
	}
	return (s_currButtons[iPad] & GetActionMask(iPad, ucAction)) != 0;
}

void C_4JInput::SetJoypadStickAxisMap(int iPad, unsigned int uiFrom, unsigned int uiTo)
{
	if (iPad >= 0 && iPad < 4 && uiFrom < 4 && uiTo < 4)
	{
		s_axisMap[iPad][uiFrom] = uiTo;
	}
}

void C_4JInput::SetJoypadStickTriggerMap(int iPad, unsigned int uiFrom, unsigned int uiTo)
{
	if (iPad >= 0 && iPad < 4 && uiFrom < 2 && uiTo < 2)
	{
		s_trigMap[iPad][uiFrom] = uiTo;
	}
}

void C_4JInput::SetKeyRepeatRate(float fRepeatDelaySecs, float fRepeatRateSecs)
{
	s_repeatDelay = fRepeatDelaySecs;
	s_repeatRate = fRepeatRateSecs;
}

void C_4JInput::SetDebugSequence(const char *chSequenceA, int (*Func)(LPVOID), LPVOID lpParam)
{
	(void)chSequenceA;
	(void)Func;
	(void)lpParam;
}

FLOAT C_4JInput::GetIdleSeconds(int iPad)
{
	if (iPad < 0 || iPad >= 4) return 0.0f;
	ULONGLONG now = GetTickCount64();
	if (now <= s_lastActivityTick[iPad]) return 0.0f;
	return (FLOAT)((now - s_lastActivityTick[iPad]) / 1000.0);
}

bool C_4JInput::IsPadConnected(int iPad)
{
	if (iPad == 0) return true;
	if (iPad > 0 && iPad < 4) return s_connected[iPad];
	return false;
}

float C_4JInput::GetJoypadStick_LX(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0.0f;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0.0f;
	return s_rawAxes[iPad][s_axisMap[iPad][AXIS_MAP_LX]] * s_sensitivity[iPad];
}

float C_4JInput::GetJoypadStick_LY(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0.0f;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0.0f;
	return s_rawAxes[iPad][s_axisMap[iPad][AXIS_MAP_LY]] * s_sensitivity[iPad];
}

float C_4JInput::GetJoypadStick_RX(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0.0f;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0.0f;
	return s_rawAxes[iPad][s_axisMap[iPad][AXIS_MAP_RX]] * s_sensitivity[iPad];
}

float C_4JInput::GetJoypadStick_RY(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0.0f;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0.0f;
	return s_rawAxes[iPad][s_axisMap[iPad][AXIS_MAP_RY]] * s_sensitivity[iPad];
}

unsigned char C_4JInput::GetJoypadLTrigger(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0;
	return s_rawTrigs[iPad][s_trigMap[iPad][TRIGGER_MAP_0]];
}

unsigned char C_4JInput::GetJoypadRTrigger(int iPad, bool bCheckMenuDisplay)
{
	if (iPad < 0 || iPad >= 4) return 0;
	if (bCheckMenuDisplay && s_menuDisplayed[iPad]) return 0;
	return s_rawTrigs[iPad][s_trigMap[iPad][TRIGGER_MAP_1]];
}

void C_4JInput::SetMenuDisplayed(int iPad, bool bVal)
{
	if (iPad >= 0 && iPad < 4)
	{
		s_menuDisplayed[iPad] = bVal;
	}
}

EKeyboardResult C_4JInput::RequestKeyboard(LPCWSTR Title, LPCWSTR Text, DWORD dwPad, UINT uiMaxChars, int (*Func)(LPVOID, const bool), LPVOID lpParam, C_4JInput::EKeyboardMode eMode)
{
	(void)Title;
	(void)Text;
	(void)dwPad;
	(void)uiMaxChars;
	(void)eMode;
	if (Func)
	{
		Func(lpParam, true);
	}
	return EKeyboard_ResultAccept;
}

void C_4JInput::GetText(uint16_t *UTF16String)
{
	if (UTF16String)
	{
		UTF16String[0] = 0;
	}
}

bool C_4JInput::VerifyStrings(WCHAR **pwStringA, int iStringC, int (*Func)(LPVOID, STRING_VERIFY_RESPONSE *), LPVOID lpParam)
{
	(void)pwStringA;
	(void)iStringC;
	(void)Func;
	(void)lpParam;
	return true;
}

void C_4JInput::CancelQueuedVerifyStrings(int (*Func)(LPVOID, STRING_VERIFY_RESPONSE *), LPVOID lpParam)
{
	(void)Func;
	(void)lpParam;
}

void C_4JInput::CancelAllVerifyInProgress(void)
{
}
