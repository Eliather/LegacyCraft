#pragma once

#include <string>
#include <vector>

class Minecraft;
class Font;

/**
 * Modular In-Game Debug Overlay (F3)
 *
 * Designed with decoupled Section Providers so new metrics or lines can be
 * added, reordered, or modified in the future without touching HUD/Gui or
 * input handling code.
 */
class DebugOverlay
{
public:
	struct OverlayContext
	{
		Minecraft *minecraft;
		Font *font;
		int screenWidth;
		int screenHeight;
		int safeLeft;
		int safeRight;
		int safeTop;
		int currentFps;
		int chunkUpdates;
	};

	typedef void (*SectionProviderFn)(const OverlayContext &ctx, std::vector<std::wstring> &outLines);

	static void Init();
	static void Reset();
	static void Toggle();
	static void SetVisible(bool visible);
	static bool IsVisible();

	// Called once per rendered frame to track real-time FPS & frame time
	static void NotifyFrameRendered();

	// Polls F3 toggle when in active gameplay (no menus/modals open)
	static void HandleInput(Minecraft *minecraft);

	// Renders the left & right overlay panels if visible and in-game
	static void Render(Minecraft *minecraft, Font *font, int screenWidth, int screenHeight, int safezoneXHalf, int safezoneYHalf);

	// Extensibility API: register custom left/right section providers
	static void RegisterLeftSection(SectionProviderFn provider);
	static void RegisterRightSection(SectionProviderFn provider);
};
