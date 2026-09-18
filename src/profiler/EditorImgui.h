#pragma once

#include "../renderer/backend/VulkanForward.h"

class Renderer;
struct GLFWwindow;
struct ImFont;

class Editor
{
public:
	static constexpr float SETTINGS_SIZE_X = 500.0f;
	static constexpr float SETTINGS_SIZE_Y = 450.0f;

	static constexpr float PROFILER_SIZE_X = 290.0f;
	static constexpr float PROFILER_SIZE_Y = 950.0f;

	void InitImgui(
		Renderer& renderer,
		GLFWwindow* window);
	void Shutdown(Renderer& renderer);

	void RenderImgui(Renderer& renderer);

	enum class SettingsCategory
	{
		Render,
		SkyWeather,
		Lighting,
		PostFX,
		Pipelines,
		Debug,

		Count
	};

	static ImFont* GetMonoFont() { return s_monoFont; }

private:
	VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;

	static ImFont* s_monoFont;
};