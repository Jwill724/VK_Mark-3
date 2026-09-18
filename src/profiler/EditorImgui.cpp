#include "pch.h"

#include "EditorImgui.h"
#include <algorithm>

#include "input/Camera.h"
#include "renderer/scene/World.h"
#include "renderer/scene/Scene.h"
#include "renderer/Renderer.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/Swapchain.h"
#include "renderer/backend/Queue.h"
#include "ShaderEditorPanel.h"
#include "../core/Environment.h"
#include "renderer/scene/LightingSystem.h"
#include "renderer/scene/WorldProbeTypes.h"

ImFont* Editor::s_monoFont = nullptr;

static void MyWindowFocusCallback(GLFWwindow* window, int focused)
{
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);
}

namespace
{
	// =========================================================================
	// 1. Style
	// =========================================================================
	namespace Style
	{
		static const float CATEGORY_LIST_WIDTH = 130.0f;
		static const float METRIC_LABEL_WEIGHT = 0.58f;
		static const float WINDOW_PADDING = 10.0f;

		static const ImVec4 ACCENT = ImVec4(0.40f, 0.80f, 1.00f, 1.00f);
		static const ImVec4 GOOD = ImVec4(0.45f, 0.85f, 0.45f, 1.00f);
		static const ImVec4 WARN = ImVec4(1.00f, 0.75f, 0.30f, 1.00f);
		static const ImVec4 BAD = ImVec4(1.00f, 0.40f, 0.40f, 1.00f);
		static const ImVec4 MUTED = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

		static const ImVec4 ROW_ASYNC = ImVec4(0.10f, 0.24f, 0.34f, 0.55f);
		static const ImVec4 BAR_GFX = ImVec4(0.30f, 0.48f, 0.70f, 0.90f);
		static const ImVec4 BAR_ASYNC = ImVec4(0.20f, 0.60f, 0.80f, 0.90f);
	}

	// =========================================================================
	// 2. UI
	// =========================================================================
	namespace UI
	{
		struct WindowScope
		{
			bool isOpen = false;

			WindowScope(
				const char* title,
				bool* pOpen,
				ImGuiWindowFlags flags)
			{
				isOpen = ImGui::Begin(title, pOpen, flags);
			}

			~WindowScope()
			{
				ImGui::End();
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		struct ChildScope
		{
			bool isOpen = false;

			ChildScope(const char* id, const ImVec2& size, bool border)
			{
				isOpen = ImGui::BeginChild(id, size, border);
			}

			~ChildScope()
			{
				ImGui::EndChild();
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		struct TableScope
		{
			bool isOpen = false;

			TableScope(
				const char* id,
				int columnCount,
				ImGuiTableFlags flags,
				const ImVec2& outerSize = ImVec2(0.0f, 0.0f))
			{
				isOpen = ImGui::BeginTable(id, columnCount, flags, outerSize);
			}

			~TableScope()
			{
				if (isOpen) {
					ImGui::EndTable();
				}
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		struct IdScope
		{
			IdScope(const char* stringId)
			{
				ImGui::PushID(stringId);
			}

			IdScope(int intId)
			{
				ImGui::PushID(intId);
			}

			~IdScope()
			{
				ImGui::PopID();
			}
		};

		struct StyleVarScope
		{
			bool pushed = false;

			StyleVarScope(ImGuiStyleVar var, float v)
			{
				ImGui::PushStyleVar(var, v);
				pushed = true;
			}

			StyleVarScope(ImGuiStyleVar var, const ImVec2& v)
			{
				ImGui::PushStyleVar(var, v);
				pushed = true;
			}

			~StyleVarScope()
			{
				if (pushed) {
					ImGui::PopStyleVar();
				}
			}
		};

		struct ColorScope
		{
			ColorScope(ImGuiCol index, const ImVec4& color)
			{
				ImGui::PushStyleColor(index, color);
			}

			~ColorScope()
			{
				ImGui::PopStyleColor();
			}
		};

		struct DisabledScope
		{
			DisabledScope(bool disabled)
			{
				ImGui::BeginDisabled(disabled);
			}

			~DisabledScope()
			{
				ImGui::EndDisabled();
			}
		};

		static void separatorText(const char* text)
		{
#if IMGUI_VERSION_NUM >= 18923
			ImGui::SeparatorText(text);
#else
			ImGui::Separator();
			ImGui::TextUnformatted(text);
			ImGui::Separator();
#endif
		}

		static void textRightAligned(const char* text, const ImVec4* color)
		{
			const float available = ImGui::GetContentRegionAvail().x;
			const float textWidth = ImGui::CalcTextSize(text).x;

			if (textWidth < available) {
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - textWidth));
			}

			if (color) {
				ImGui::TextColored(*color, "%s", text);
			}
			else {
				ImGui::TextUnformatted(text);
			}
		}

		// Label on the left, value right aligned. Every read-only number in the
		// editor goes through this so columns line up across sections.
		struct MetricTable
		{
			bool isOpen = false;

			MetricTable(const char* id, float labelWeight = Style::METRIC_LABEL_WEIGHT)
			{
				isOpen = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp);

				if (isOpen) {
					ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, labelWeight);
					ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelWeight);
				}
			}

			~MetricTable()
			{
				if (isOpen) {
					ImGui::EndTable();
				}
			}

			explicit operator bool() const
			{
				return isOpen;
			}
		};

		static void metricImpl(const char* label, const std::string& value, const ImVec4* color)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);

			ImGui::TableSetColumnIndex(1);
			textRightAligned(value.c_str(), color);
		}

		static void metric(const char* label, const std::string& value)
		{
			metricImpl(label, value, nullptr);
		}

		static void metric(const char* label, const std::string& value, const ImVec4& color)
		{
			metricImpl(label, value, &color);
		}

		static void bar(float fraction, const char* overlay, const ImVec4& color)
		{
			if (fraction < 0.0f) fraction = 0.0f;
			if (fraction > 1.0f) fraction = 1.0f;

			ColorScope barColor(ImGuiCol_PlotHistogram, color);
			ImGui::ProgressBar(fraction, ImVec2(-1.0f, ImGui::GetTextLineHeight()), overlay);
		}

		static std::string formatCount(uint64_t count)
		{
			if (count >= 1000000ull) {
				return fmt::format("{:.2f}M", double(count) / 1e6);
			}
			if (count >= 1000ull) {
				return fmt::format("{:.1f}K", double(count) / 1e3);
			}
			return fmt::format("{}", count);
		}
	}

	// =========================================================================
	// 3. UIWidgets
	// =========================================================================
	namespace UIWidgets
	{
		static bool toggleU32(const char* label, uint32_t* valueU32)
		{
			bool valueBool = (*valueU32 != 0u);
			bool changed = ImGui::Checkbox(label, &valueBool);
			if (changed) {
				*valueU32 = valueBool ? 1u : 0u;
			}
			return changed;
		}

		static bool sliderU32(
			const char* label,
			uint32_t* valueU32,
			uint32_t minValue,
			uint32_t maxValue)
		{
			int valueInt = static_cast<int>(*valueU32);
			const int minInt = static_cast<int>(minValue);
			const int maxInt = static_cast<int>(maxValue);

			bool changed = ImGui::SliderInt(label, &valueInt, minInt, maxInt);
			if (!changed) {
				return false;
			}

			if (valueInt < minInt) valueInt = minInt;
			if (valueInt > maxInt) valueInt = maxInt;
			*valueU32 = static_cast<uint32_t>(valueInt);
			return true;
		}

		static bool comboU32(
			const char* label,
			uint32_t* valueU32,
			const char* const items[],
			int itemCount)
		{
			int current = static_cast<int>(*valueU32);

			if (!ImGui::Combo(label, &current, items, itemCount)) {
				return false;
			}

			*valueU32 = static_cast<uint32_t>(current);
			return true;
		}
	}

	// =========================================================================
	// 4. Context
	// =========================================================================
	struct UIContext
	{
		Profiler* profiler = nullptr;
		FrameStats* stats = nullptr;
		RD::RenderToggles* dbg = nullptr;
		Renderer* renderer = nullptr;
	};

	using DrawFn = void(*)(UIContext& ui);

	// One collapsible block of controls or readouts. Categories and the
	// profiler window are both just lists of these.
	struct ControlGroup
	{
		const char* label = "";
		DrawFn fn = nullptr;
		bool defaultOpen = true;
	};

	static void drawGroups(UIContext& ui, const ControlGroup* groups, int groupCount)
	{
		for (int i = 0; i < groupCount; ++i)
		{
			const ControlGroup& group = groups[i];
			if (!group.fn) continue;

			UI::IdScope id(group.label);

			const ImGuiTreeNodeFlags flags =
				group.defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;

			if (ImGui::CollapsingHeader(group.label, flags)) {
				group.fn(ui);
				ImGui::Spacing();
			}
		}
	}

	using PanelVisibleFn = bool(*)(const UIContext& ui);

	struct Panel
	{
		const char* name = "";
		DrawFn fn = nullptr;
		PanelVisibleFn isVisible = nullptr;   // null = always drawn
	};

	struct PanelRegistry
	{
		std::vector<Panel> panels;

		void addPanel(const char* name, DrawFn fn, PanelVisibleFn isVisible = nullptr)
		{
			Panel panel;
			panel.name = name;
			panel.fn = fn;
			panel.isVisible = isVisible;
			panels.push_back(panel);
		}

		void draw(UIContext& ui) const
		{
			for (const Panel& panel : panels) {
				if (!panel.fn) continue;
				if (panel.isVisible && !panel.isVisible(ui)) continue;

				UI::IdScope id(panel.name);
				panel.fn(ui);
			}
		}
	};

	// -------------------------------------------------------------------------
	// Shared reads. Nothing here writes to the profiler.
	// -------------------------------------------------------------------------
	static float passGpuMs(const PassTimingStats& stats)
	{
		return stats.gpuMsAverage.IsInitialized() ? stats.gpuMsAverage.Get() : 0.0f;
	}

	static float passCpuMs(const PassTimingStats& stats)
	{
		return stats.cpuMsAverage.IsInitialized() ? stats.cpuMsAverage.Get() : 0.0f;
	}

	// =========================================================================
	// 5. Settings groups
	// =========================================================================
	static void groupCamera(UIContext& ui)
	{
		auto& camera = World::GetScene().GetCamera();

		const auto& pos = camera.GetPosition();
		const auto& camVelo = camera.GetVelocity();

		{
			UI::MetricTable table("CameraState");
			if (table) {
				UI::metric("World Position", fmt::format("{:.2f} {:.2f} {:.2f}", pos.x, pos.y, pos.z));
				UI::metric("Velocity", fmt::format("{:.2f} {:.2f} {:.2f}", camVelo.x, camVelo.y, camVelo.z));
			}
		}

		ImGui::Spacing();

		float camSens = camera.GetSensitivity();
		if (ImGui::SliderFloat("Sensitivity##cam", &camSens, 1.0f, 100.0f, "%.0f")) {
			camera.SetSensitivity(camSens);
		}

		float camFOV = camera.GetFovY();
		if (ImGui::SliderFloat("FOV##cam", &camFOV, Camera::CAMERA_MIN_FOV, Camera::CAMERA_MAX_FOV, "%.0f")) {
			camera.SetFovY(camFOV);
		}

		float maxSpeed = camera.GetMaxSpeed();
		if (ImGui::SliderFloat("Max Speed##cam", &maxSpeed, 1.0f, 100.0f, "%.0f")) {
			camera.SetMaxSpeed(maxSpeed);
		}

		float minSpeed = camera.GetMinSpeed();
		if (ImGui::SliderFloat("Min Speed##cam", &minSpeed, 1.0f, 100.0f, "%.0f")) {
			camera.SetMinSpeed(minSpeed);
		}

		float accel = camera.GetAcceleration();
		if (ImGui::SliderFloat("Acceleration##cam", &accel, 1.0f, 100.0f, "%.0f")) {
			camera.SetAcceleration(accel);
		}

		float damping = camera.GetDamping();
		if (ImGui::SliderFloat("Damping##cam", &damping, 1.0f, 100.0f, "%.0f")) {
			camera.SetDamping(damping);
		}
	}

	static void groupAsyncCompute(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		const auto& async = profiler.asyncStats;

		if (!async.bDedicatedQueue)
		{
			ImGui::TextDisabled("Unavailable");
			ImGui::TextWrapped(
				"This device exposes no compute queue family distinct from "
				"graphics. The graph always uses the single-submit path.");
			return;
		}

		ImGui::Checkbox("Enable Async Compute##async", &profiler.enableAsyncCompute);

		//ImGui::Spacing();

		//UI::MetricTable table("AsyncState");
		//if (!table) return;

		//UI::metric("Graphics batches", fmt::format("{}", async.graphicsBatchCount));
		//UI::metric("Async passes", fmt::format("{}", async.asyncPassCount));
		//UI::metric("Overlapped passes", fmt::format("{}", async.overlapPassCount));
		//UI::metric(
		//	"Active this frame",
		//	async.bActiveThisFrame ? "yes" : "no",
		//	async.bActiveThisFrame ? Style::ACCENT : Style::MUTED);
	}

	static void groupAntiAliasing(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& taaSettings = profiler.taaSettings;
		auto& casSettings = profiler.casSettings;

		static constexpr const char* kAAModeLabels[] = { "Off", "TAA", "TAA + CAS" };

		int aaMode = static_cast<int>(dbg.aaMode);
		if (ImGui::Combo("Mode", &aaMode, kAAModeLabels, IM_ARRAYSIZE(kAAModeLabels)))
			dbg.aaMode = static_cast<uint32_t>(aaMode);

		const bool temporalOn = dbg.aaMode != static_cast<uint32_t>(RD::AntiAliasingMethod::AA_OFF);
		const bool sharpenOn = dbg.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA_CAS);

		ImGui::BeginDisabled(!temporalOn);
		ImGui::SeparatorText("Temporal");

		ImGui::SliderFloat("Clamp Gamma", &taaSettings.clampGamma, 0.0f, 10.0f);
		ImGui::SliderFloat("Depth Reject Scale", &taaSettings.depthRejectScale, 0.0f, 10.0f);
		ImGui::SliderFloat("Motion Speed Scale", &taaSettings.motionSpeedScale, 0.0001f, 0.02f, "%.4f");
		ImGui::SliderFloat("Sigma Floor", &taaSettings.sigmaFloor, 0.001f, 0.03f, "%.3f");
		ImGui::SliderFloat("Shading Response", &taaSettings.shadingResponse, 1.0f, 4.0f, "%.2f");
		ImGui::SliderFloat("Shading Reject Scale", &taaSettings.shadingRejectScale, 1.0f, 25.0f);

		ImGui::EndDisabled();

		ImGui::BeginDisabled(!sharpenOn);
		ImGui::SeparatorText("Sharpening");

		ImGui::SliderFloat("Sharpness", &casSettings.sharpness, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Denoise", &casSettings.denoise, 0.0f, 1.0f, "%.2f");

		ImGui::EndDisabled();
	}

	static void groupTransparency(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		ImGui::SliderFloat("OIT Z Scale", &profiler.forwardPush.oitDepthScale, 50.0f, 2000.0f, "%.0f");
	}

	static void groupSun(UIContext& ui)
	{
		auto& scene = World::GetScene().GetSceneData();
		auto& sky = ui.profiler->atmosphereSkySettings;

		UI::separatorText("Direction");

		glm::vec3 direction = glm::vec3(scene.sunlightDirection);
		float lengthSq = glm::dot(direction, direction);

		if (!std::isfinite(lengthSq) || lengthSq < 1e-12f)
			direction = glm::vec3(0.0f, 1.0f, 0.0f);
		else
			direction /= std::sqrt(lengthSq);

		// Retain azimuth at the poles, where the direction does not define it.
		static float azimuthDeg = 0.0f;

		float horizontalSq =
			direction.x * direction.x + direction.z * direction.z;

		if (horizontalSq > 1e-10f)
		{
			azimuthDeg = glm::degrees(
				std::atan2(direction.z, direction.x));

			if (azimuthDeg < 0.0f)
				azimuthDeg += 360.0f;
		}

		float elevationDeg = glm::degrees(
			std::asin(glm::clamp(direction.y, -1.0f, 1.0f)));

		bool directionChanged = false;

		directionChanged |= ImGui::SliderFloat(
			"Azimuth##sun",
			&azimuthDeg,
			0.0f, 360.0f,
			"%.2f deg",
			ImGuiSliderFlags_AlwaysClamp);

		ImGui::SetItemTooltip(
			"Full rotation around the world Y axis. "
			"0 = +X, 90 = +Z, 180 = -X, 270 = -Z. "
			"Ctrl-click to enter an exact angle.");

		directionChanged |= ImGui::SliderFloat(
			"Elevation##sun",
			&elevationDeg,
			-90.0f, 90.0f,
			"%.2f deg",
			ImGuiSliderFlags_AlwaysClamp);

		ImGui::SetItemTooltip(
			"0 is the world-horizontal plane. "
			"Positive angles place the sun overhead; negative angles place it below.");

		if (directionChanged)
		{
			float azimuth = glm::radians(azimuthDeg);
			float elevation = glm::radians(elevationDeg);
			float horizontal = std::cos(elevation);

			direction = glm::vec3(
				horizontal * std::cos(azimuth),
				std::sin(elevation),
				horizontal * std::sin(azimuth));

			scene.sunlightDirection = glm::vec4(direction, 0.0f);
		}

		if (ImGui::TreeNode("Direction Vector##sun"))
		{
			glm::vec3 editedDirection = glm::vec3(scene.sunlightDirection);

			if (ImGui::InputFloat3(
				"XYZ##sun",
				glm::value_ptr(editedDirection),
				"%.6f"))
			{
				float editedLengthSq = glm::dot(
					editedDirection, editedDirection);

				if (std::isfinite(editedLengthSq) && editedLengthSq > 1e-12f)
				{
					editedDirection /= std::sqrt(editedLengthSq);
					scene.sunlightDirection = glm::vec4(editedDirection, 0.0f);
				}
			}

			ImGui::SetItemTooltip(
				"World-space direction toward the sun. "
				"Nonzero vectors are normalized when edited.");

			ImGui::TreePop();
		}

		UI::separatorText("Color & Intensity");

		glm::vec3 tint = glm::vec3(scene.sunlightColor);

		const ImGuiColorEditFlags colorFlags =
			ImGuiColorEditFlags_HDR |
			ImGuiColorEditFlags_Float |
			ImGuiColorEditFlags_DisplayRGB |
			ImGuiColorEditFlags_InputRGB;

		bool tintChanged = ImGui::ColorEdit3(
			"Tint##sun",
			glm::value_ptr(tint),
			colorFlags);

		ImGui::SetItemTooltip(
			"Linear RGB tint. Values above 1 are supported. "
			"Tint is normalized to unit luminance; intensity is controlled separately.");

		if (ImGui::Button("Neutral White##sun"))
		{
			tint = glm::vec3(1.0f);
			tintChanged = true;
		}

		if (tintChanged)
		{
			if (std::isfinite(tint.x) &&
				std::isfinite(tint.y) &&
				std::isfinite(tint.z))
			{
				tint = glm::max(tint, glm::vec3(0.0f));

				float luminance =
					0.2126f * tint.r +
					0.7152f * tint.g +
					0.0722f * tint.b;

				// A black tint cannot be normalized.
				// Use zero illuminance to turn sunlight off.
				if (luminance > 1e-6f)
				{
					tint /= luminance;

					scene.sunlightColor = glm::vec4(
						tint, scene.sunlightColor.w);
				}
			}
		}

		float illuminance = scene.sunlightColor.w;

		if (ImGui::DragFloat(
			"Illuminance##sun",
			&illuminance,
			100.0f,
			0.0f, 1.0e9f,
			"%.1f lux",
			ImGuiSliderFlags_AlwaysClamp))
		{
			if (std::isfinite(illuminance))
				scene.sunlightColor.w = std::max(illuminance, 0.0f);
		}

		ImGui::SetItemTooltip(
			"Solar illuminance before atmospheric attenuation. "
			"Ctrl-click for exact input. Zero disables the solar source.");

		UI::separatorText("Sun Disc");

		ImGui::SliderFloat(
			"Angular Radius##sun",
			&sky.sunAngularRadiusDeg,
			0.05f, 2.0f,
			"%.4f deg",
			ImGuiSliderFlags_AlwaysClamp);

		ImGui::SetItemTooltip(
			"0.2666 degrees is the default. "
			"Disc radiance adjusts with area to preserve illuminance.");

		if (ImGui::TreeNode("Solar Scale##sun"))
		{
			ImGui::DragFloat(
				"Multiplier##sun",
				&sky.solarIntensityScale,
				0.01f,
				0.0f, 100.0f,
				"%.3f",
				ImGuiSliderFlags_AlwaysClamp);

			ImGui::SetItemTooltip(
				"Additional multiplier applied by the atmosphere lighting. "
				"Keep at 1 for direct control through Illuminance.");

			ImGui::TreePop();
		}
	}

	static void groupLocalLights(UIContext& ui)
	{
		(void)ui;

		bool dynamicLights = LightingSystem::_dynamicLightsEnabled;
		if (ImGui::Checkbox("Dynamic Lights##light", &dynamicLights)) {
			LightingSystem::_dynamicLightsEnabled = dynamicLights ? 1u : 0u;
		}

		static uint32_t targetLightCount = 0u;
		if (UIWidgets::sliderU32("Light Count##light", &targetLightCount, 0u, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS))) {
			LightingSystem::SetTargetActiveLightCount(targetLightCount);
		}

		const uint32_t activeCount = LightingSystem::GetActiveLightCount();

		UI::MetricTable table("LightCounts");
		if (table) {
			UI::metric("Active", fmt::format("{} / {}", activeCount, static_cast<uint32_t>(RD::MAX_VISIBLE_LIGHTS)));
		}
	}

	static void groupFlashlight(UIContext& ui)
	{
		(void)ui;

		auto& flashlight = LightingSystem::_flashlightSettings;
		auto& flashlightReal = LightingSystem::_mainFlashLight;

		ImGui::SliderFloat("Lag Strength", &flashlightReal.m_lagStrength, 10.0, 100.0f);
		ImGui::SliderFloat("Sway Strength", &flashlightReal.m_swayStrength, 0.001f, 0.1f, "%.3f");
		ImGui::SliderFloat("Source Radius##light", &flashlight.sourceRadius, 0.02, 0.3f);
		ImGui::SliderFloat("Lumens##light", &flashlight.lumens, 50.0f, 3000.0f, "%.0f lm");
		//ImGui::SliderFloat("light radius##light", &flashlight.radius, 5, 100.0f);
		//ImGui::SliderFloat("light outer degree##light", &flashlight.outerDeg, 10.0f, 40.0f);
		//ImGui::SliderFloat("light inner degree##light", &flashlight.innerDeg, 10.0f, 40.0f);
		//ImGui::SliderFloat("Offset R##light", &flashlight.offsetRight, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("Offset D##light", &flashlight.offsetDown, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("Offset L##light", &flashlight.offsetFwd, -0.2f, 0.2f, "%.2f");
		//ImGui::SliderFloat("fov y scale##light", &flashlight.fovYScale, 0.1f, 5.0f);

		//ImGui::SliderFloat("near projection##light", &flashlight.nearProj, 0.1f, 3.0f);
		//ImGui::SliderFloat("shadow bias##light", &flashlight.shadowBias, 0.0001f, 1.5f, "%.4f");
		//ImGui::SliderFloat("radius texels##light", &flashlight.radiusTexels, 1.0f, 4.0f);
	}


	static void rtShadowParamsUI(RTShadowParams& p, const char* id, float tMaxMax)
	{
		auto label = [id](const char* name) {
			static char buf[96];
			snprintf(buf, sizeof(buf), "%s##%s", name, id);
			return buf;
			};

		ImGui::SliderFloat(label("Ray TMin"), &p.rayTMin, 0.0001f, 0.1f, "%.4f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat(label("Ray TMax"), &p.rayTMax, 5.0f, tMaxMax);

		ImGui::SliderFloat(label("Normal Bias"), &p.normalBias, 0.0f, 0.25f, "%.3f");
		ImGui::SetItemTooltip("Flat world-space offset along the surface normal.");

		ImGui::SliderFloat(label("Slope Bias"), &p.rayBias, 0.0f, 1e-3f, "%.5f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Scaled by distance from camera. Total = normal + slope * dist.");
	}

	static void groupShadows(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		bool shadows = dbg.enableShadows != 0u;
		bool contact = dbg.enableSSS != 0u;

		if (ImGui::Checkbox("Enable Shadows##rt", &shadows)) {
			dbg.enableShadows = shadows ? 1u : 0u;
		}

		if (!dbg.enableShadows) return;

		auto& shadowControl = World::GetScene().GetShadowControls();

		ImGui::Spacing();
		UI::separatorText("Sun Shadow Mode");

		const char* sunFilterModes[] = { "PCF", "PCSS", "Ray-Traced" };
		int sunFilter = static_cast<int>(dbg.sunShadowFilter);

		if (ImGui::Combo("Filter Mode##rt", &sunFilter, sunFilterModes, IM_ARRAYSIZE(sunFilterModes)))
		{
			dbg.sunShadowFilter = static_cast<uint32_t>(sunFilter);
		}

		const bool bRTSoft = dbg.sunShadowFilter >= static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT);

		if (!bRTSoft)
		{
			const char* qualityModes[] = { "Low", "Medium", "High" };
			int currentQuality = static_cast<int>(profiler.shadowQuality);

			if (ImGui::Combo(
				"Shadow Quality",
				&currentQuality,
				qualityModes,
				IM_ARRAYSIZE(qualityModes)))
			{
				profiler.shadowQuality =
					static_cast<RD::ShadowQuality>(currentQuality);
			}

			int shadowFar = static_cast<int>(shadowControl.shadowFar);

			ImGui::SliderInt(
				"Shadow Far##rt",
				&shadowFar,
				500,
				1500);

			if (shadowFar != static_cast<int>(shadowControl.shadowFar))
			{
				shadowControl.shadowFar = static_cast<float>(shadowFar);
				World::GetScene().ShouldUpdateCascadeSplits();
			}

			float splitLambda = shadowControl.splitLambda;
			ImGui::SliderFloat("Split Lambda##rt", &splitLambda, 0.94, 0.97, "%.2f");
			if (splitLambda != shadowControl.splitLambda)
			{
				shadowControl.splitLambda = splitLambda;
				World::GetScene().ShouldUpdateCascadeSplits();
			}

			bool depthHack = shadowControl.enableShadowDepthExtendHack;

			if (ImGui::Checkbox("Enable Depth Hack##rt", &depthHack))
			{
				shadowControl.enableShadowDepthExtendHack = depthHack;
				World::GetScene().ShouldUpdateCascadeSplits();
			}

			if (dbg.sunShadowFilter == static_cast<uint32_t>(RD::SunShadowFilter::PCSS))
			{
				ImGui::SliderFloat(
					"Sun Angular Radius##rt",
					&shadowControl.sunAngularRadiusDeg,
					0.0f,
					6.0f,
					"%.2f deg");

				ImGui::SliderFloat(
					"Min Radius (texels)##rt",
					&shadowControl.minFilterRadiusTexels,
					0.25f,
					4.0f,
					"%.2f");

				ImGui::SliderFloat(
					"Search Scale##rt",
					&shadowControl.searchRadiusScale,
					0.5f,
					2.0f,
					"%.2f");

				ImGui::SliderFloat(
					"Max Normal Offset##rt",
					&shadowControl.maxNormalOffsetTexels,
					1.0f,
					12.0f,
					"%.1f");

				ImGui::DragFloat3(
					"Max Radius (texels)##rt",
					&shadowControl.pcssMaxRadiusTexels.x,
					0.1f,
					1.0f,
					48.0f,
					"%.1f");
			}
		}
		else
		{
			rtShadowParamsUI(
				profiler.rtShadowPush.shadow,
				"rtsh",
				1500.0f);
		}

		ImGui::Spacing();
		UI::separatorText("Contact Shadows");

		if (ImGui::Checkbox("Enable Screen Space Contact Shadows##rt", &contact)) {
			dbg.enableSSS = contact ? 1u : 0u;
		}

		if (dbg.enableSSS)
		{
			auto& contactShadowSettings = profiler.contactShadowsSettings;

			ImGui::SliderFloat(
				"Surface Thickness##rt",
				&contactShadowSettings.surfaceThickness,
				0.001f,
				0.02f,
				"%.3f");

			ImGui::SliderFloat(
				"Bilinear Threshold##rt",
				&contactShadowSettings.bilinearThreshold,
				0.001f,
				0.2f,
				"%.3f");

			int shadowContrastInt =
				static_cast<int>(contactShadowSettings.shadowContrast);

			ImGui::SliderInt(
				"Shadow Contrast##rt",
				&shadowContrastInt,
				1,
				8);

			contactShadowSettings.shadowContrast =
				static_cast<float>(shadowContrastInt);
		}
	}

	static void groupSSGI(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		const char* giModes[] = { "Off", "VBAO", "VBGI" };
		UIWidgets::comboU32("GI Method", &dbg.giMode, giModes, IM_ARRAYSIZE(giModes));
		if (dbg.giMode == static_cast<uint32_t>(RD::GIMethod::OFF)) return;

		auto& s = profiler.ssgiSettings;

		ImGui::SeparatorText("Trace");
		ImGui::SliderFloat("Effect Radius##ssgi", &s.effectRadius, 0.5f, 25.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Radius Multiplier##ssgi", &s.radiusMultiplier, 0.5f, 3.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Falloff Range##ssgi", &s.effectFalloffRange, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Sample Distribution Power##ssgi", &s.sampleDistributionPower, 1.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::SeparatorText("Spatial Denoise");
		ImGui::SliderFloat("Blur Beta##ssgi", &s.denoiseBlurBeta, 0.1f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Upsample Depth Sigma##ssgi", &s.upsampleDepthSigma, 32.0f, 1024.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::SeparatorText("AO Temporal");
		ImGui::SliderFloat("History Weight##ao", &s.aoHistoryWeight, 0.0f, 0.99f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Depth Tolerance##ao", &s.aoDepthTolerance,
			0.001f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Normal Threshold##ao", &s.aoNormalThreshold, 0.0f, 0.9999f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Max History Samples##ao", &s.aoMaxHistorySamples, 1.0f, 128.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Initial Variance##ao", &s.aoInitialVariance,
			0.000001f, 0.0625f, "%.6f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

		if (ImGui::TreeNode("Reconstruction Confidence##ao"))
		{
			ImGui::SliderFloat("Minimum Observation##ao", &s.aoMinObservation,
				0.001f, 0.5f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Neighborhood Confidence##ao", &s.aoNeighborConfidence,
				s.aoMinObservation, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Clipping Confidence##ao", &s.aoClipConfidence,
				s.aoMinObservation, 0.99f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("History Footprint Support##ao", &s.aoHistoryFootprintMinSupport,
				0.01f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Clipping and Response##ao"))
		{
			ImGui::SliderFloat("Clip Sigma Multiplier##ao", &s.aoClipSigma, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Maximum Clip Sigma##ao", &s.aoClipMaxSigma,
				0.001f, 0.25f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Minimum Clip Margin##ao", &s.aoClipMargin, 0.0f, 0.1f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Reactive Threshold##ao", &s.aoReactiveThreshold,
				0.001f, 0.25f, "%.3f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Missing Observations##ao"))
		{
			ImGui::SliderFloat("Hold Frames##ao", &s.aoMissingHoldFrames, 0.0f, 32.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			s.aoMissingMaxFrames = std::max(s.aoMissingMaxFrames, s.aoMissingHoldFrames + 1.0f);
			ImGui::SliderFloat("Expiry Frames##ao", &s.aoMissingMaxFrames,
				s.aoMissingHoldFrames + 1.0f, 64.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderFloat("Missing History Age Retention##ao", &s.aoMissingAgeDecay,
				0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TreePop();
		}

		ImGui::TextDisabled("AO history: %s", s.aoHistoryValid != 0u ? "Available" : "Reset / unavailable");
		ImGui::TextDisabled("Noise index: %u", static_cast<unsigned int>(s.noiseIndex));

		if (dbg.giMode != static_cast<uint32_t>(RD::GIMethod::VBGI)) return;
		auto& t = profiler.forwardPush;
		ImGui::SeparatorText("Indirect");
		ImGui::SliderFloat("GI Intensity##ssgi", &t.giIntensity, 0.0f, 25.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SeparatorText("GI Temporal");
		ImGui::SliderFloat("Current Frame Weight##gi", &s.giTemporalAlpha, 0.02f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Depth Tolerance##gi", &s.giReprojTolerance, 0.02f, 0.3f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Firefly Clamp##ssgi", &s.giClampMax,
			0.5f, 32.0f, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
	}

	static void groupWorldProbes(UIContext& ui)
	{
		auto& s = ui.profiler->worldProbeSettings;
		const WorldProbeHeader& h = ui.renderer->GetWorldProbeHeader();
		ImGui::SeparatorText("Probe Shading");
		ImGui::Checkbox("Reconstruction Direct Fallback##wp", &s.reconstructDirectFallback);
		ImGui::Checkbox("Reconstruction Debug Colors##wp", &s.reconstructDebugView);
		ImGui::SetItemTooltip("Inspect the reconstruction target directly: red=fallback, green=coverage, blue=compatible zero coverage. Diagnostic colors replace lighting.");
		ImGui::Checkbox("Fog Probe Cache##wp", &s.cacheFog);
		ImGui::Checkbox("Transparent Probe Cache##wp", &s.cacheTransparency);
		ImGui::SetItemTooltip("Off means zero cached probe lighting for the cached transparent shader, not automatic direct sampling.");

		ImGui::SeparatorText("System");
		ImGui::Checkbox("Enable World Probes##wp", &s.enabled);
		ImGui::Checkbox("Inject SSGI Bounce##wp", &s.injectSSGI);
		ImGui::Checkbox("Pause Regular Updates##wp", &s.pauseUpdates);
		ImGui::SetItemTooltip("Pauses both regular round robins and confidence decay. Newly scrolled-in and reset probes still initialize.");
		ImGui::Checkbox("Freeze Clipmaps##wp", &s.freezeClipmaps);
		ImGui::SetItemTooltip("Holds the resident windows in place. Regular lighting and geometry updates continue.");
		ImGui::SeparatorText("Clipmap");
		ImGui::Checkbox("Follow Camera##wp", &s.followCamera);
		if (!s.followCamera) ImGui::DragFloat3("Anchor##wp", &s.anchor.x, 0.5f);
		ImGui::DragFloat3("Center Bias##wp", &s.centerBias.x, 0.1f);
		ImGui::DragFloat("Base Spacing##wp", &s.baseSpacing, 0.1f, 0.1f, 100.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Changing base spacing resets all five cascades.");
		ImGui::BeginDisabled();
		ImGui::DragFloat("Spacing Multiplier##wp", &s.spacingMultiplier, 0.1f, 2.0f, 2.0f, "%.1f");
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("The production preset fixes the multiplier at 2.");
		ImGui::SliderFloat("View Forward Bias##wp", &s.viewForwardBias, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Scroll Hysteresis##wp", &s.scrollHysteresis, 0.0f, 4.0f, "%.2f cells");
		ImGui::SliderFloat("Cascade Edge Fade##wp", &s.cascadeEdgeFade, 0.01f, 3.0f, "%.2f cells");
		for (uint32_t c = 0u; c < 5u; ++c)
		{
			const auto& d = h.cascades[c];
			const float spacing = d.originSpacing.w;
			ImGui::Text("C%u: %.2f m | 16 x 8 x 16 | 2048 probes", c, spacing);
			ImGui::Text("  Base %d, %d, %d | Footprint %.1f x %.1f x %.1f m",
				d.baseCell.x, d.baseCell.y, d.baseCell.z, spacing * 16.0f, spacing * 8.0f, spacing * 16.0f);
			ImGui::Text("  Full %u + %u | Relight %u + %u", d.schedule.x, d.schedule.y, d.schedule.z, d.schedule.w);
		}
		const double mib = 1.0 / (1024.0 * 1024.0);
		const size_t probes = GetWorldProbeBufferBytes(RD::WORLD_PROBE_COUNT);
		const size_t atlas = GetWorldProbeVisibilityBytes();
		const size_t schedule = GetWorldProbeScheduleBytes();
		const size_t summary = GetWorldProbeSummaryBytes();
		ImGui::Text("Probes %.3f MiB | RG8 atlas %.3f MiB", probes * mib, atlas * mib);
		ImGui::Text("Schedule %.3f MiB | Summaries %.3f MiB", schedule * mib, summary * mib);
		ImGui::Text("Total %.3f MiB + %u bytes per frame", (probes + atlas + schedule + summary) * mib, unsigned(sizeof(WorldProbeFrameInfo)));

		ImGui::SeparatorText("Scheduling");
		int budget = int(s.probesPerFrame);
		if (ImGui::SliderInt("Probes Per Frame##wp", &budget, 0, int(RD::WORLD_PROBE_COUNT)))
			s.probesPerFrame = uint32_t(std::clamp(budget, 0, int(RD::WORLD_PROBE_COUNT)));
		ImGui::SliderFloat("Full Update Ratio##wp", &s.fullUpdateRatio, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Cascade Priority Falloff##wp", &s.cascadePriorityFalloff, 0.01f, 1.0f, "%.2f");
		ImGui::Checkbox("Quarter Resolution Scroll In##wp", &s.quarterResolutionScrollIn);
		ImGui::SetItemTooltip("16 unique geometry rays per new probe. Turning this off initializes new probes with 64 rays.");
		uint32_t fullBudget = 0u, relightBudget = 0u;
		for (const auto& c : h.cascades) { fullBudget += c.schedule.y; relightBudget += c.schedule.w; }
		ImGui::Text("Regular windows: %u full, %u relight", fullBudget, relightBudget);
		ImGui::Text("Geometry rays <= %u + scroll work", fullBudget * 64u);
		ImGui::Text("Scroll maximum: %u geometry rays", RD::WORLD_PROBE_COUNT * (s.quarterResolutionScrollIn ? 16u : 64u));
		ImGui::TextDisabled("Fresh slots and overlapping windows reduce regular queue counts.");
		ImGui::TextDisabled("SSGI injection can add up to 64 segment rays per queued probe.");

		ImGui::SeparatorText("Visibility");
		ImGui::DragFloat("Trace Distance##wp", &s.maxTraceDistance, 10.0f, 1.0f, 100000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::DragFloat("Ray Bias##wp", &s.traceBias, 0.001f, 0.0001f, s.baseSpacing * 0.25f, "%.4f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("RG8 Variance Floor##wp", &s.visibilityVariance, 0.000001f, 0.1f, "%.6f", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Normalized squared-distance variance. Increasing it can increase light leaking.");
		ImGui::SliderFloat("Visibility Exponent##wp", &s.visibilityExponent, 1.0f, 128.0f, "%.1f");
		ImGui::SliderFloat("Sky History Weight##wp", &s.skyHistoryWeight, 0.0f, 0.99f, "%.3f");
		ImGui::SeparatorText("Relocation");
		ImGui::Checkbox("Relocate Probes##wp", &s.relocation);
		ImGui::DragFloat("Minimum Frontface Distance##wp", &s.minFrontfaceDistance, 0.01f, 0.001f, 100.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Backface Threshold##wp", &s.backfaceThreshold, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Inside when ratio >= threshold. At 0.25: 16/64 full rays or 4/16 quarter rays.");
		ImGui::SliderFloat("Return Step##wp", &s.relocationReturnStep, 0.0f, 0.49f, "%.3f x spacing");
		ImGui::SliderFloat("Cell Margin##wp", &s.relocationCellMargin, 0.001f, 0.49f, "%.3f x spacing");
		ImGui::DragFloat("Move Epsilon##wp", &s.relocationMoveEpsilon, 0.001f, 0.00001f, s.baseSpacing * 0.1f, "%.4f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SeparatorText("Bounce Cache");
		ImGui::DragFloat("Injection Radius##wp", &s.injectionRadius, 0.1f, 0.1f, 10000.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Bounce Update Rate##wp", &s.bounceUpdateRate, 0.001f, 1.0f, "%.3f");
		ImGui::DragFloat("Confidence Half-Life##wp", &s.confidenceHalfLife, 0.1f, 0.05f, 120.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Minimum Sample Confidence##wp", &s.minSampleConfidence, 0.0f, 1.0f, "%.3f");
		ImGui::SeparatorText("Debug");
		ImGui::Checkbox("Draw Probes##wp", &s.debugDraw);
		const char* cascades[] = { "All", "Cascade 0", "Cascade 1", "Cascade 2", "Cascade 3", "Cascade 4" };
		int cascade = s.debugCascade + 1;
		if (ImGui::Combo("Cascade##wp", &cascade, cascades, IM_ARRAYSIZE(cascades))) s.debugCascade = cascade - 1;
		const char* modes[] = { "Irradiance", "Sky Visibility", "Bounce Confidence", "Distance", "State", "Cascade", "Schedule Class" };
		int mode = int(s.debugMode);
		if (ImGui::Combo("View##wp", &mode, modes, IM_ARRAYSIZE(modes))) s.debugMode = uint32_t(mode);
		ImGui::Checkbox("Show Inactive##wp", &s.debugShowInactive);
		ImGui::SliderFloat("Sphere Radius##wp", &s.debugRadius, 0.01f, 0.49f, "%.2f x spacing");
		ImGui::DragFloat("Max Distance##wp", &s.debugMaxDistance, 1.0f, 1.0f, 10000.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Intensity##wp", &s.debugIntensity, 0.01f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
		if (s.debugMode == 4u)
		{
			ImGui::TextColored(ImVec4(0.1f, 0.3f, 1, 1), "Blue: never traced / needs full trace");
			ImGui::TextColored(ImVec4(1, 0, 1, 1), "Magenta: inside, escape failed");
			ImGui::TextColored(ImVec4(1, 0.05f, 0.05f, 1), "Red: inactive");
			ImGui::TextColored(ImVec4(1, 0.55f, 0, 1), "Orange: relocated");
			ImGui::TextColored(ImVec4(0, 1, 1, 1), "Cyan overlay: quarter result");
			ImGui::TextColored(ImVec4(0.1f, 1, 0.2f, 1), "Green overlay: updated this frame");
		}
		if (ImGui::Button("Reset Probe Cache##wp")) ui.renderer->RequestWorldProbeReset();
		ImGui::SameLine();
		if (ImGui::Button("Restore Probe Defaults##wp"))
		{
			s = WorldProbeSettings{};
			ui.renderer->RequestWorldProbeReset();
		}
	}


	static void groupReflections(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& rs = profiler.reflectPush;

		UIWidgets::toggleU32("Enable RT Reflections##rtr", &dbg.enableRTReflections);

		ImGui::SeparatorText("Ray");

		ImGui::SliderFloat("Reflect Roughness Cutoff##rtr", &rs.reflectRoughnessCutoff, 0.05f, 1.0f);

		int maxBouncesInt = static_cast<int>(rs.maxBounces);
		if (ImGui::SliderInt("Max Bounces##rtr", &maxBouncesInt, 2, 3)) {
			rs.maxBounces = static_cast<uint32_t>(maxBouncesInt);
		}

		int maxReflectLightsInt = static_cast<int>(rs.maxReflectLights);
		if (ImGui::SliderInt("Max Reflected Lights##rtr", &maxReflectLightsInt, 50, 300)) {

			rs.maxReflectLights = static_cast<uint32_t>(maxReflectLightsInt);
		}

		ImGui::SliderFloat("Shadow Skip Threshold##rtr", &rs.shadowSkipThreshold, 0.001f, 0.01f, "%.3f");

		//ImGui::SeparatorText("Shadow Rays");
		//rtShadowParamsUI(rs.shadow, "rtr", 500.0f);

		ImGui::SeparatorText("Resolve");
		ImGui::SliderFloat("Roughness Fade##rtr", &rs.roughnessFadeStart, 0.0f, 1.0f);
		ImGui::SliderFloat("Ambient Scale##rtr", &rs.ambientScale, 0.0f, 4.0f);
	}

	static void groupVolumetrics(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& fog = profiler.froxelSettings;
		auto& composite = profiler.compositePush;

		UIWidgets::toggleU32("Enable Volumetrics##vol", &dbg.enableVolumetrics);

		if (!dbg.enableVolumetrics) return;

		UI::separatorText("Medium");

		ImGui::SliderFloat("Density##vol", &fog.density, 0.0f, 0.001f, "%.6f");
		ImGui::SliderFloat("Scattering##vol", &fog.scatteringStrength, 0.0f, 10.0f);
		ImGui::SliderFloat("Extinction##vol", &fog.extinction, 0.0f, 1.0f);
		ImGui::SetItemTooltip("Absorption rate. Scales density into the extinction coefficient.");
		ImGui::SliderFloat("Asymmetry Factor##vol", &fog.asymmetryFactor, 0.0f, 0.95f, "%.2f");
		ImGui::SetItemTooltip("HG phase g. Higher values push scattering forward, tightening godrays.");
		ImGui::SliderFloat("Height Falloff##vol", &fog.heightFalloff, 0.0f, 0.1f, "%.3f");

		UI::separatorText("Local Lights");

		ImGui::SliderFloat("Light Intensity##vol", &fog.localLightIntensity, 0.0f, 4.0f, "%.2f");
		ImGui::SetItemTooltip("Contribution of clustered local lights to the fog volume. 0 skips the cluster walk entirely.");

		UI::separatorText("Volume");

		float froxelNear = fog.froxelClips.x;
		float froxelFar = fog.froxelClips.y;

		bool clipsChanged = false;
		clipsChanged |= ImGui::SliderFloat("Fog Near##vol", &froxelNear, 0.1f, 5.0f, "%.2f");
		ImGui::SetItemTooltip("Front of the froxel grid. Raising it gains slice density everywhere.");
		clipsChanged |= ImGui::SliderFloat("Fog Far##vol", &froxelFar, 20.0f, 400.0f, "%.0f");
		ImGui::SetItemTooltip("Back of the froxel grid. Fog stops accumulating past this distance.");

		if (clipsChanged)
		{
			froxelFar = std::max(froxelFar, froxelNear + 1.0f);

			fog.froxelClips = glm::vec2(froxelNear, froxelFar);
			composite.froxelClips = fog.froxelClips;
		}

		{
			const float ratio = froxelFar / std::max(froxelNear, 1e-4f);
			const float sliceRatio = std::pow(ratio, 1.0f / float(RD::FROXEL_GRID_Z));

			UI::MetricTable table("FroxelVolume");
			if (table) {
				UI::metric("Grid", fmt::format("{} x {} x {}",
					RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z));
				UI::metric("Depth Ratio", fmt::format("{:.1f}x", ratio));
				UI::metric("Slice Step", fmt::format("{:.3f}x", sliceRatio),
					(sliceRatio > 1.15f) ? Style::WARN : Style::MUTED);
			}
		}

		UI::separatorText("Temporal");

		ImGui::SliderFloat("History Weight##vol", &fog.historyWeight, 0.80f, 0.98f, "%.2f");
		ImGui::SetItemTooltip("Fraction of the reprojected volume kept per frame. Higher is smoother but lags faster motion.");
		ImGui::SliderFloat("Slice Jitter##vol", &fog.jitterStrength, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Per-frame offset of sample depth within each slice. Needs history weight to resolve.");
	}

	static void groupAtmosphereSky(UIContext& ui)
	{
		auto& sky = ui.profiler->atmosphereSkySettings;

		UI::separatorText("Scattering");

		ImGui::SliderFloat(
			"Mie Scattering Albedo##sky",
			&sky.mieAlbedo,
			0.0f, 1.0f,
			"%.2f");

		ImGui::SliderFloat(
			"Mie Asymmetry##sky",
			&sky.mieG,
			0.0f, 0.95f,
			"%.2f");

		UI::separatorText("Ground");

		ImGui::ColorEdit3(
			"Ground Albedo##sky",
			&sky.ground.x,
			ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);

		ImGui::SetItemTooltip(
			"Lambertian planet albedo for the sky-view bottom boundary. "
			"0.1 water or dark rock, 0.3 neutral, 0.4+ sand or snow.");

		UI::separatorText("Quality");

		int samples = static_cast<int>(sky.viewSamples);

		if (ImGui::SliderInt(
			"Sky View Samples##sky",
			&samples,
			16, 256))
		{
			sky.viewSamples = static_cast<uint32_t>(samples);
		}

		UI::separatorText("World Placement");

		ImGui::DragFloat3(
			"Sea Level Origin##sky",
			&sky.seaLevelOrigin.x,
			1.0f);

		ImGui::DragFloat(
			"World To Kilometers##sky",
			&sky.worldToKm,
			0.00001f,
			0.000001f, 1.0f,
			"%.6f",
			ImGuiSliderFlags_AlwaysClamp);

		ImGui::SetItemTooltip(
			"For world meters use 0.001. "
			"Keep the camera above the bottom sphere and inside the atmosphere.");
	}

	static void groupAtmosphere(UIContext& ui)
	{
		auto& settings = ui.profiler->atmosphereSettings;
		auto& p = settings.parameters;

		UI::separatorText("Atmospheric Medium");
		ImGui::TextUnformatted("Transport distances: km. Extinction coefficients: 1/km.");

		ImGui::SliderFloat3("Rayleigh Extinction##atm", &p.rayleigh.x, 0.0f, 0.1f, "%.6f");
		ImGui::SliderFloat("Rayleigh Scale Height##atm", &p.rayleigh.w, 0.1f, 20.0f, "%.2f km");
		ImGui::SliderFloat3("Mie Extinction##atm", &p.mie.x, 0.0f, 0.1f, "%.6f");
		ImGui::SetItemTooltip("Total aerosol extinction: scattering plus absorption. Phase does not affect transmittance.");
		ImGui::SliderFloat("Mie Scale Height##atm", &p.mie.w, 0.1f, 10.0f, "%.2f km");

		UI::separatorText("Absorption Layer");
		ImGui::SliderFloat3("Absorption Extinction##atm", &p.absorption.x, 0.0f, 0.01f, "%.6f");
		ImGui::SliderFloat("Absorption Density##atm", &p.absorption.w, 0.0f, 4.0f, "%.2f");
		ImGui::SliderFloat("Layer Center##atm", &p.geometry.z, 0.0f, p.geometry.y, "%.1f km");
		ImGui::SliderFloat("Layer Half Width##atm", &p.geometry.w, 0.1f, 50.0f, "%.1f km");
		ImGui::SetItemTooltip("Triangular density profile. Zero beyond center +/- half width.");

		UI::separatorText("Planet");
		ImGui::SliderFloat("Planet Radius##atm", &p.geometry.x, 100.0f, 10000.0f, "%.1f km");
		ImGui::SliderFloat("Atmosphere Thickness##atm", &p.geometry.y, 1.0f, 200.0f, "%.1f km");

		UI::separatorText("Integration");
		int samples = static_cast<int>(p.integration.x);
		if (ImGui::SliderInt("Integration Samples##atm", &samples, 32, 1024))
			p.integration.x = static_cast<uint32_t>(samples);
		ImGui::SetItemTooltip("LUT rebuild only. Camera, sun direction and exposure changes do not rebuild it.");
		if (ImGui::Button("Reset Medium##atm"))
			p = AtmosphereParameters{};
		SanitizeAtmosphere(p);
	}

	static void groupToneMapping(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		auto& luma = profiler.lumaExposureSettings;

		bool manual = luma.manualExposure != 0u;
		if (ImGui::Checkbox("Manual Exposure##tm", &manual))
			luma.manualExposure = manual ? 1u : 0u;

		ImGui::BeginDisabled(!manual);
		ImGui::SliderFloat("EV100##tm", &luma.manualEV100, -4.0f, 17.0f, "%.1f");
		ImGui::EndDisabled();

		ImGui::BeginDisabled(manual);
		ImGui::SliderFloat("Adapt Up##tm", &luma.adaptSpeedUp, 0.1f, 5.0f, "%.2f");
		ImGui::SliderFloat("Adapt Down##tm", &luma.adaptSpeedDown, 0.1f, 5.0f, "%.2f");
		ImGui::EndDisabled();

		ImGui::SliderFloat("Compensation##tm", &luma.exposureCompensation, -3.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Min EV100##tm", &luma.minEV100, -8.0f, 10.0f, "%.1f");
		ImGui::SliderFloat("Max EV100##tm", &luma.maxEV100, 8.0f, 20.0f, "%.1f");
	}

	static void groupBloom(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		UIWidgets::toggleU32("Enable Bloom##post", &dbg.enableBloom);

		if (!dbg.enableBloom) return;

		auto& bloom = profiler.bloomPush;
		ImGui::SliderFloat("Bloom Intensity", &profiler.debugToggles.bloomIntensity, 0.001f, 0.1f, "%.3f");
		ImGui::SliderFloat("Bloom Threshold", &bloom.bloomThreshold, 0.01f, 10.0f, "%.2f");
		ImGui::SliderFloat("Bloom Knee", &bloom.bloomKnee, 0.01f, 5.0f, "%.2f");
	}

	static void groupChromaticAberration(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& caSettings = profiler.caPush;

		UIWidgets::toggleU32("Enable Chromatic Aberration##post", &dbg.enableChromaticAberration);

		ImGui::BeginDisabled(dbg.enableChromaticAberration == 0);

		ImGui::SliderFloat("Shift##post_ca", &caSettings.maxShiftPixels, 0.0f, 8.0f, "%.2f px");
		ImGui::SliderFloat("Distortion##post_ca", &caSettings.distortionAmount, 0.0f, 0.30f, "%.3f");
		ImGui::SliderFloat("Falloff##post_ca", &caSettings.falloffExponent, 1.0f, 6.0f, "%.2f");

		int taps = int(caSettings.maxTaps);
		if (ImGui::SliderInt("Max Taps##post_ca", &taps, 3, 16))
			caSettings.maxTaps = uint32_t(taps);

		ImGui::EndDisabled();
	}

	static void groupLensFlare(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		auto& lf = profiler.lensFlareSettings;

		constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;
		constexpr ImGuiSliderFlags kLog = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic;

		UIWidgets::toggleU32("Enable Lens Flare##post", &dbg.enableLensFlare);

		UI::separatorText("Source");
		ImGui::SliderFloat("Intensity##lf", &lf.brightIntensity, 0.0f, 4.0f, "%.2f", kClamp);

		UI::separatorText("Halo");

		ImGui::SliderFloat("Inner Radius##lf", &lf.ringInnerRadius, 0.0f, 0.45f, "%.4f", kClamp);
		ImGui::SliderFloat("Outer Radius##lf", &lf.ringOuterRadius, 0.005f, 0.60f, "%.4f", kClamp);
		lf.ringOuterRadius = std::max(lf.ringOuterRadius, lf.ringInnerRadius + 0.005f);

		ImGui::SliderFloat("Halo Opacity##lf", &lf.haloOpacity, 0.0f, 1.0f, "%.3f", kClamp);
		ImGui::SliderFloat("Anisotropy##lf", &lf.haloAnisotropy, 0.0f, 1.0f, "%.2f", kClamp);
		ImGui::SliderFloat("Chroma##lf", &lf.chromaStrength, 0.0f, 1.0f, "%.2f", kClamp);
		ImGui::SliderFloat("Squeeze##lf", &lf.haloSqueeze, 1.0f, 3.0f, "%.2f", kClamp);
		ImGui::SliderFloat("Angle Gain##lf", &lf.haloAngleGain, 0.0f, 6.0f, "%.2f", kClamp);

		UI::separatorText("Ghosts");

		ImGui::SliderFloat("Ghost Strength##lf", &lf.ghostStrength, 0.0f, 0.35f, "%.3f", kLog);
		ImGui::SliderFloat("Ghost Spacing##lf", &lf.ghostSpacing, 0.4f, 1.8f, "%.2f", kClamp);


		UI::separatorText("Starburst");

		int blades = static_cast<int>(lf.starburstBlades + 0.5f);
		if (ImGui::SliderInt("Blades##lf", &blades, 3, 16))
			lf.starburstBlades = static_cast<float>(blades);

		ImGui::SliderFloat("Burst Intensity##lf", &lf.starburstIntensity, 0.0f, 2.0f, "%.2f", kClamp);
		ImGui::SliderFloat("Burst Length##lf", &lf.starburstLength, 0.01f, 0.50f, "%.3f", kLog);
		ImGui::SliderFloat("Burst Width##lf", &lf.starburstWidth, 0.005f, 0.25f, "%.3f", kLog);
		ImGui::SliderFloat("Burst Rotation##lf", &lf.starburstRotation, -4.0f, 4.0f, "%.2f", kClamp);

		UI::separatorText("Anamorphic Streak");

		ImGui::SliderFloat("Streak Strength##lf", &lf.streakStrength, 0.0f, 1.0f, "%.2f", kClamp);
		ImGui::SliderFloat("Streak Width##lf", &lf.streakWidth, 0.002f, 0.08f, "%.4f", kLog);
		ImGui::SliderFloat("Streak Length##lf", &lf.streakLength, 0.02f, 0.60f, "%.3f", kLog);

		UI::separatorText("Occlusion");

		ImGui::SliderFloat("Radius (px)##lf", &lf.occlusionRadiusPixels, 1.0f, 64.0f, "%.1f", kClamp);
		ImGui::SliderFloat("Depth Bias##lf", &lf.occlusionDepthBias, 0.0f, 5.0f, "%.3f", kClamp);
		ImGui::SliderFloat("Fade (world)##lf", &lf.occlusionFade, 0.5f, 2000.0f, "%.1f", kLog);
		ImGui::SliderFloat("TAA Jitter Scale##lf", &lf.sunJitterScale, 0.0f, 1.0f, "%.2f", kClamp);
	}

	static void groupCullingDebug(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		UIWidgets::toggleU32("Disable Occlusion Culling##rt", &dbg.disableOcclusionCull);
	}

	static void groupDebugDraw(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		UIWidgets::toggleU32("Show Opaque OBB##pipes", &dbg.showOpaqueOBBs);
		UIWidgets::toggleU32("Show Transparent OBB##pipes", &dbg.showTransparentOBBs);

		ImGui::Checkbox("Show Wireframe", &profiler.enableWireframeView);
	}

	static void groupDebugViews(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;

		int current = static_cast<int>(dbg.debugView);

		constexpr int columns = 3;
		int column = 0;

		for (const RD::DebugViewEntry& entry : RD::DEBUG_VIEW_TABLE)
		{
			const uint32_t viewIndex = static_cast<uint32_t>(entry.view);

			// Cascade visualization only applies to raster sun shadows.
			if (entry.view == RD::DebugView::Cascades &&
				(!dbg.enableShadows ||
					dbg.sunShadowFilter == static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT)))
			{
				continue;
			}

			if (column != 0) {
				ImGui::SameLine();
			}

			const std::string label = fmt::format("{}##dbgview", entry.label);

			if (ImGui::RadioButton(label.c_str(), &current, static_cast<int>(viewIndex))) {
				dbg.debugView = static_cast<uint32_t>(current);
			}

			column = (column + 1) % columns;
			if (column == 0) {
				ImGui::NewLine();
			}
		}

		if (column != 0) {
			ImGui::NewLine();
		}
	}

	static void groupPanels(UIContext& ui)
	{
		RD::RenderToggles& dbg = *ui.dbg;
		Profiler& profiler = *ui.profiler;

		ImGui::Checkbox("Shader Editor##panels", &profiler.enableShaderEditor);

		ImGui::TextDisabled("Shader editor hides with this window (TAB).");
	}

	// -------------------------------------------------------------------------
	// Category tables. Adding a control block is one row here; moving one
	// between categories is a cut and paste of that row.
	// -------------------------------------------------------------------------
	static const ControlGroup RENDER_GROUPS[] = {
		{ "Panels",                  &groupPanels,         false },
		{ "Execution / Async Compute", &groupAsyncCompute, false },
		{ "Anti-Aliasing & Sharpness", &groupAntiAliasing,  true  },
		{ "Transparency",            &groupTransparency,   false },
	};

	static const ControlGroup SKY_WEATHER_GROUPS[] = {
		{ "Sun",                    &groupSun,             true  },
		{ "Sky Scattering",         &groupAtmosphereSky,   false },
		{ "Shadows",                &groupShadows,         false },
		{ "Volumetric Fog",         &groupVolumetrics,     false },
		{ "Atmospheric Medium",     &groupAtmosphere,      false },
	};

	static const ControlGroup LIGHTING_GROUPS[] = {
		{ "Global Illumination & Occlusion", &groupSSGI,        false },
		{ "World Probes",                    &groupWorldProbes, true  },
		{ "Reflections",                     &groupReflections, false },
		{ "Local Lights",                    &groupLocalLights, false },
		{ "Flashlight",                      &groupFlashlight,  false }
	};

	static const ControlGroup POSTFX_GROUPS[] = {
		{ "Camera",                 &groupCamera,              false },
		{ "Exposure",               &groupToneMapping,         true  },
		{ "Bloom",                  &groupBloom,               false },
		{ "Lens Flare",             &groupLensFlare,           false },
		{ "Chromatic Aberration",   &groupChromaticAberration, false },
	};

	static const ControlGroup DEBUG_GROUPS[] = {
		{ "Culling",                &groupCullingDebug, true  },
		{ "Debug Draw",             &groupDebugDraw,    false },
		{ "Shading Overlay",        &groupDebugViews,   false },
	};

	struct SettingsCategoryEntry
	{
		const char* label = "";
		const ControlGroup* groups = nullptr;
		int groupCount = 0;
	};

	static void groupShaderEditor(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		auto& hot = ui.renderer->GetShaderHotReload();

		ImGui::Checkbox("Open Shader Editor##shader", &profiler.enableShaderEditor);

		UI::MetricTable table("ShaderState");
		if (table)
		{
			UI::metric("Tracked shaders",
				fmt::format("{}", ui.renderer->GetShaderCache().Records().size()));
			UI::metric("Watching", hot.IsWatchEnabled() ? "yes" : "no");
			UI::metric("Pending", fmt::format("{}", hot.PendingCount()));
		}
	}

	static void panelShaderWindow(UIContext& ui)
	{
		ImGui::SetNextWindowSize(ImVec2(1000.0f, 720.0f), ImGuiCond_FirstUseEver);

		UI::WindowScope window(
			"Shader Editor",
			&ui.profiler->enableShaderEditor,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		ShaderEditorPanel::Get().Draw(*ui.renderer);
	}

	static const ControlGroup PIPELINE_GROUPS[] = {
		{ "Shaders", &groupShaderEditor, true },
	};

	static const SettingsCategoryEntry SETTINGS_CATEGORIES[] = {
		{
			"Render",
			RENDER_GROUPS,
			IM_ARRAYSIZE(RENDER_GROUPS)
		},
		{
			"Sky & Weather",
			SKY_WEATHER_GROUPS,
			IM_ARRAYSIZE(SKY_WEATHER_GROUPS)
		},
		{
			"Scene Lighting",
			LIGHTING_GROUPS,
			IM_ARRAYSIZE(LIGHTING_GROUPS)
		},
		{
			"Camera & Post",
			POSTFX_GROUPS,
			IM_ARRAYSIZE(POSTFX_GROUPS)
		},
		{
			"Pipelines",
			PIPELINE_GROUPS,
			IM_ARRAYSIZE(PIPELINE_GROUPS)
		},
		{
			"Debug",
			DEBUG_GROUPS,
			IM_ARRAYSIZE(DEBUG_GROUPS)
		},
	};

	static_assert(
		IM_ARRAYSIZE(SETTINGS_CATEGORIES) == static_cast<int>(Editor::SettingsCategory::Count),
		"Category table count mismatch.");

	static void drawCategorySelector(Editor::SettingsCategory& selectedCategory)
	{
		UI::separatorText("Categories");

		for (int categoryIndex = 0; categoryIndex < IM_ARRAYSIZE(SETTINGS_CATEGORIES); ++categoryIndex) {
			const bool isSelected = (static_cast<int>(selectedCategory) == categoryIndex);

			if (ImGui::Selectable(SETTINGS_CATEGORIES[categoryIndex].label, isSelected)) {
				selectedCategory = (Editor::SettingsCategory)categoryIndex;
			}
		}
	}

	// =========================================================================
	// 6. Profiler sections — read-only views over Profiler / FrameStats
	// =========================================================================
	static void sectionFrame(UIContext& ui)
	{
		FrameStats& stats = *ui.stats;

		const float fps = stats.fps.Get();
		const ImVec4& fpsColor =
			(fps >= 100.0f) ? Style::GOOD :
			(fps >= 45.0f) ? Style::WARN : Style::BAD;

		{
			UI::MetricTable table("FrameTimings");
			if (table) {
				UI::metric("FPS", fmt::format("{:.1f}", fps), fpsColor);
				UI::metric("Frame (capped)", fmt::format("{:.3f} ms", stats.frameTime.Get()));
				UI::metric("Frame (true)", fmt::format("{:.3f} ms", stats.frameTimeRaw.Get()));
				UI::metric("CPU draw", fmt::format("{:.3f} ms", stats.drawTime.Get()));
				UI::metric("CPU scene", fmt::format("{:.3f} ms", stats.sceneUpdateTime.Get()));
			}
		}

		ImGui::Spacing();

		ImGui::Checkbox("Cap Framerate", &stats.capFramerate);

		{
			UI::DisabledScope disabled(!stats.capFramerate);

			struct FpsPreset { const char* label; float value; };

			static const FpsPreset presets[] = {
				{ "60",  RD::TARGET_FPS_60  },
				{ "120", RD::TARGET_FPS_120 },
				{ "144", RD::TARGET_FPS_144 },
				{ "240", RD::TARGET_FPS_240 },
			};

			const float buttonWidth = 60.0f;

			for (int i = 0; i < IM_ARRAYSIZE(presets); ++i)
			{
				if (i != 0) ImGui::SameLine();

				const bool isActive = (stats.targetFrameRate == presets[i].value);

				UI::ColorScope highlight(
					ImGuiCol_Button,
					isActive ? Style::BAR_ASYNC : ImGui::GetStyle().Colors[ImGuiCol_Button]);

				if (ImGui::Button(presets[i].label, ImVec2(buttonWidth, 0))) {
					stats.targetFrameRate = presets[i].value;
				}
			}
		}
	}

	static void sectionGpu(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;
		FrameStats& stats = *ui.stats;

		float    graphicsGpuMs = 0.0f;
		float    asyncGpuMs = 0.0f;
		//float    graphicsCpuMs = 0.0f;
		//float    asyncCpuMs    = 0.0f;
		//uint32_t graphicsCount = 0u;
		uint32_t asyncCount = 0u;

		const auto& allPassStats = profiler.GetAllPassStats();
		for (const PassTimingStats& s : allPassStats)
		{
			if (!s.activeLastFrame) continue;

			const float gpu = passGpuMs(s);
			//const float cpu = s.cpuMsAverage.IsInitialized() ? s.cpuMsAverage.Get() : 0.0f;

			if (s.asyncQueueLastFrame)
			{
				asyncGpuMs += gpu;
				//asyncCpuMs += cpu;
				++asyncCount;
			}
			else
			{
				graphicsGpuMs += gpu;
				//graphicsCpuMs += cpu;
				//++graphicsCount;
			}
		}

		{
			UI::MetricTable table("GpuTimings");
			if (table) {
				UI::metric("Total", fmt::format("{:.3f} ms", stats.gpuFrameTime.Get()));
				UI::metric("Graphics queue", fmt::format("{:.3f} ms", graphicsGpuMs));

				if (asyncCount > 0u) {
					UI::metric("Async queue", fmt::format("{:.3f} ms", asyncGpuMs), Style::ACCENT);
				}
			}
		}

		// Queue split. Sums of per-pass averages, so it tracks the shape of the
		// frame rather than the wall clock total above.
		const float queueTotal = graphicsGpuMs + asyncGpuMs;
		if (queueTotal > 0.0f && asyncCount > 0u)
		{
			UI::bar(
				graphicsGpuMs / queueTotal,
				fmt::format("gfx {:.0f}%", 100.0f * graphicsGpuMs / queueTotal).c_str(),
				Style::BAR_GFX);
		}

		ImGui::Spacing();
		UI::separatorText("Device");

		const double usedMb = double(stats.vramStats.used) / (1024.0 * 1024.0);

		const double budgetMb = double(stats.vramStats.budget) / (1024.0 * 1024.0);

		const float vramFrac = (budgetMb > 0.0)
			? float(usedMb / budgetMb)
			: 0.0f;

		UI::bar(
			vramFrac,
			fmt::format(
				"{:.0f} / {:.0f} MB",
				usedMb,
				budgetMb).c_str(),
			(vramFrac > 0.9f)
			? Style::BAD
			: (vramFrac > 0.75f)
			? Style::WARN
			: Style::BAR_GFX);

		ImGui::TextWrapped("%s", stats.gpuName.c_str());
	}

	static void sectionAssets(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		UI::MetricTable table("AssetCounts");
		if (!table) return;

		UI::metric("Meshes", UI::formatCount(profiler.assetCounts.totalMeshCount));
		UI::metric("Materials", UI::formatCount(profiler.assetCounts.totalMaterialCount));
		UI::metric("Vertices", UI::formatCount(profiler.assetCounts.totalVertexCount));
		UI::metric("Indices", UI::formatCount(profiler.assetCounts.totalIndexCount));
	}

	static void sectionVisibility(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		// GPU driven rendering statistics
		const GPUStats& gpu = profiler.gpuStats;

		const uint32_t totalVisible = gpu.visibleOpaque + gpu.visibleTransparent;
		const uint32_t totalInstances = static_cast<uint32_t>(World::GetInstanceState().gpuInputs.size());

		{
			UI::MetricTable table("VisibilityCounts");
			if (table) {
				UI::metric("Visible Opaque", UI::formatCount(gpu.visibleOpaque));
				UI::metric("Visible Transparent", UI::formatCount(gpu.visibleTransparent));
				UI::metric("Shadow Casters", UI::formatCount(gpu.visibleShadowCasters));
				UI::metric("Triangles", UI::formatCount(gpu.triangleCount));
			}
		}

		if (totalInstances > 0)
		{
			const uint32_t culled = (totalInstances >= totalVisible)
				? totalInstances - totalVisible : 0u;
			const float cullRatio = float(culled) / float(totalInstances);

			ImGui::Spacing();
			UI::bar(
				cullRatio,
				fmt::format("culled {:.1f}%  ({} / {})", 100.0f * cullRatio, culled, totalInstances).c_str(),
				Style::BAR_GFX);
		}

		//ImGui::Spacing();
		//UI::separatorText("Draw Commands");

		//UI::MetricTable table("DrawCounts");
		//if (!table) return;

		//UI::metric("Opaque", UI::formatCount(gpu.opaqueDrawCount));
		//UI::metric("Transparent", UI::formatCount(gpu.transparentDrawCount));
		//UI::metric("Shadow", UI::formatCount(gpu.shadowDrawCount));
		////UI::metric("Total", UI::formatCount(
		////	gpu.opaqueDrawCount + gpu.transparentDrawCount + gpu.shadowDrawCount));
	}

	static void sectionMeshlets(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		const GPUStats& gpu = profiler.gpuStats;
		const uint32_t mlDrawn = gpu.meshletsDrawnEarly + gpu.meshletsDrawnLate;

		//if (gpu.meshletsSubmitted == 0u)
		//{
		//	ImGui::TextDisabled("No meshlet dispatches this frame.");
		//	return;
		//}

		//const bool bOvercount = (mlDrawn > gpu.meshletsSubmitted);

		//static uint32_t peakOvercount   = 0u;
		//static uint32_t overcountFrames = 0u;
		//static uint32_t quietFrames     = 0u;

		//constexpr uint32_t OVERCOUNT_DECAY_FRAMES = 60u;

		//if (bOvercount)
		//{
		//	peakOvercount = std::max(peakOvercount, mlDrawn - gpu.meshletsSubmitted);
		//	++overcountFrames;
		//	quietFrames = 0u;
		//}
		//else if (peakOvercount > 0u)
		//{
		//	if (++quietFrames >= OVERCOUNT_DECAY_FRAMES)
		//	{
		//		peakOvercount   = 0u;
		//		overcountFrames = 0u;
		//		quietFrames     = 0u;
		//	}
		//}

		//{
		//	UI::MetricTable table("MeshletCounts");
		//	if (table) {
		//		UI::metric("Submitted", UI::formatCount(gpu.meshletsSubmitted));

		//		if (bOvercount) {
		//			UI::metric("Drawn", UI::formatCount(mlDrawn), Style::BAD);
		//		}
		//		else {
		//			UI::metric("Drawn", UI::formatCount(mlDrawn));
		//		}

		//		UI::metric("Phase 1", UI::formatCount(gpu.meshletsDrawnEarly));
		//		UI::metric("Phase 2", UI::formatCount(gpu.meshletsDrawnLate));
		//	}
		//}
		//ImGui::Spacing();

		//if (bOvercount)
		//{
		//	ImGui::TextColored(Style::BAD, "Drawn over submitted by %u",
		//		mlDrawn - gpu.meshletsSubmitted);
		//}
		//else
		//{
		//	const float culledFrac =
		//		float(gpu.meshletsSubmitted - mlDrawn) / float(gpu.meshletsSubmitted);

		//	UI::bar(
		//		culledFrac,
		//		fmt::format("culled {:.1f}%", 100.0f * culledFrac).c_str(),
		//		Style::BAR_ASYNC);
		//}

		//{
		//	UI::MetricTable table("MeshletOvercount");
		//	if (table)
		//	{
		//		if (peakOvercount > 0u)
		//		{
		//			UI::metric("Peak over", UI::formatCount(peakOvercount),
		//				(overcountFrames > OVERCOUNT_DECAY_FRAMES) ? Style::BAD : Style::WARN);
		//			UI::metric("Frames", fmt::format("{}", overcountFrames), Style::MUTED);
		//		}
		//		else
		//		{
		//			UI::metric("Peak over", "--", Style::MUTED);
		//			UI::metric("Frames", "--", Style::MUTED);
		//		}
		//	}
		//}

		//ImGui::Spacing();
		UI::separatorText("Cull Info");
		{
			const uint64_t reasonSum =
				uint64_t(gpu.meshletsCulledFrustum) +
				uint64_t(gpu.meshletsCulledCone) +
				uint64_t(gpu.meshletsCulledHiZ);

			UI::MetricTable table("MeshletCullReasons");
			if (table) {
				UI::metric("Frustum", UI::formatCount(gpu.meshletsCulledFrustum));
				UI::metric("Cone", UI::formatCount(gpu.meshletsCulledCone));
				UI::metric("Hi-Z", UI::formatCount(gpu.meshletsCulledHiZ));
				UI::metric("Sum", UI::formatCount(reasonSum), Style::MUTED);
			}
		}

		ImGui::Spacing();
		UI::separatorText("Triangles");

		const uint32_t drawnTris = gpu.meshletTriangles;

		UI::MetricTable table("MeshletTriangles");
		if (!table) return;

		UI::metric("Submitted", fmt::format("{:.2f}M", double(gpu.triangleCount) / 1e6));
		UI::metric("Drawn", fmt::format("{:.2f}M", double(drawnTris) / 1e6),
			(drawnTris > gpu.triangleCount) ? Style::BAD : Style::MUTED);

		if (gpu.triangleCount > 0u && drawnTris <= gpu.triangleCount)
		{
			UI::metric("Reduction", fmt::format(
				"{:.1f}%", 100.0f * (1.0f - float(drawnTris) / float(gpu.triangleCount))));
		}
	}

	static void sectionPassTimings(UIContext& ui)
	{
		Profiler& profiler = *ui.profiler;

		static bool bHighlightAsync = true;
		static bool bSortByCost = false;
		static bool bShowCpu = true;

		ImGui::Checkbox("Async##passes", &bHighlightAsync);
		ImGui::SameLine();
		ImGui::Checkbox("Sort##passes", &bSortByCost);
		ImGui::SameLine();
		ImGui::Checkbox("CPU##passes", &bShowCpu);

		const auto& allPassStats = profiler.GetAllPassStats();

		// Index list so sorting never touches the profiler's own storage.
		std::array<uint32_t, RD::PASS_COUNT> order{};
		uint32_t activeCount = 0u;
		float maxGpuMs = 0.0f;

		for (uint32_t passIndex = 0; passIndex < static_cast<uint32_t>(allPassStats.size()); ++passIndex)
		{
			const PassTimingStats& passStats = allPassStats[passIndex];
			if (!passStats.activeLastFrame) continue;

			order[activeCount++] = passIndex;
			maxGpuMs = std::max(maxGpuMs, passGpuMs(passStats));
		}

		if (activeCount == 0u)
		{
			ImGui::TextDisabled("No passes recorded last frame.");
			return;
		}

		if (bSortByCost)
		{
			std::sort(
				order.begin(),
				order.begin() + activeCount,
				[&allPassStats](uint32_t a, uint32_t b) {
					return passGpuMs(allPassStats[a]) > passGpuMs(allPassStats[b]);
				});
		}

		const int columnCount = bShowCpu ? 3 : 2;

		UI::TableScope table(
			"PassTimings",
			columnCount,
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_RowBg);

		if (!table) return;

		ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, bShowCpu ? 0.45f : 0.55f);
		if (bShowCpu) {
			ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthStretch, 0.20f);
		}
		ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthStretch, bShowCpu ? 0.35f : 0.45f);
		ImGui::TableHeadersRow();

		for (uint32_t i = 0; i < activeCount; ++i)
		{
			const RD::Renderer_Pass passID = static_cast<RD::Renderer_Pass>(order[i]);
			const PassTimingStats& passStats = allPassStats[order[i]];

			ImGui::TableNextRow();

			if (bHighlightAsync && passStats.asyncQueueLastFrame)
			{
				ImGui::TableSetBgColor(
					ImGuiTableBgTarget_RowBg0,
					ImGui::GetColorU32(Style::ROW_ASYNC));
			}

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(profiler.GetPassName(passID));

			int column = 1;

			if (bShowCpu)
			{
				ImGui::TableSetColumnIndex(column++);
				if (passStats.cpuMsAverage.IsInitialized())
					UI::textRightAligned(fmt::format("{:.3f}", passCpuMs(passStats)).c_str(), nullptr);
				else
					UI::textRightAligned("--", &Style::MUTED);
			}

			ImGui::TableSetColumnIndex(column);
			if (passStats.gpuMsAverage.IsInitialized())
			{
				const float gpuMs = passGpuMs(passStats);
				const float fraction = (maxGpuMs > 0.0f) ? (gpuMs / maxGpuMs) : 0.0f;

				UI::bar(
					fraction,
					fmt::format("{:.3f}", gpuMs).c_str(),
					passStats.asyncQueueLastFrame ? Style::BAR_ASYNC : Style::BAR_GFX);
			}
			else
			{
				UI::textRightAligned("--", &Style::MUTED);
			}
		}
	}

	static const ControlGroup PROFILER_SECTIONS[] = {
		{ "Frame",        &sectionFrame,        true  },
		{ "GPU",          &sectionGpu,          true  },
		{ "Visibility",   &sectionVisibility,   false },
		{ "Culling",      &sectionMeshlets,     false },
		{ "Pass Timings", &sectionPassTimings,  true  },
		{ "Asset Data",   &sectionAssets,       false },
	};

	// =========================================================================
	// 7. Panels
	// =========================================================================
	static void panelSettingsWindow(UIContext& ui)
	{
		ImGui::SetNextWindowPos(
			ImVec2(Style::WINDOW_PADDING, Style::WINDOW_PADDING), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(
			ImVec2(Editor::SETTINGS_SIZE_X, Editor::SETTINGS_SIZE_Y), ImGuiCond_FirstUseEver);

		UI::WindowScope window(
			"Settings",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		static Editor::SettingsCategory selectedCategory = Editor::SettingsCategory::Render;

		{
			UI::ChildScope left("SettingsLeft", ImVec2(Style::CATEGORY_LIST_WIDTH, 0.0f), true);
			if (left) {
				drawCategorySelector(selectedCategory);
			}
		}

		ImGui::SameLine();

		{
			UI::ChildScope right("SettingsRight", ImVec2(0.0f, 0.0f), true);
			if (right)
			{
				const int categoryIndex = static_cast<int>(selectedCategory);

				if (categoryIndex >= 0 && categoryIndex < IM_ARRAYSIZE(SETTINGS_CATEGORIES))
				{
					const SettingsCategoryEntry& category = SETTINGS_CATEGORIES[categoryIndex];
					drawGroups(ui, category.groups, category.groupCount);
				}
			}
		}
	}

	static void panelProfilerWindow(UIContext& ui)
	{
		ImGuiIO& io = ImGui::GetIO();

		ImVec2 profilerPos = ImVec2(
			io.DisplaySize.x - Editor::PROFILER_SIZE_X - Style::WINDOW_PADDING,
			Style::WINDOW_PADDING
		);

		ImGui::SetNextWindowPos(profilerPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(Editor::PROFILER_SIZE_X, Editor::PROFILER_SIZE_Y), ImGuiCond_Always);

		UI::WindowScope window(
			"Profiler",
			nullptr,
			ImGuiWindowFlags_NoCollapse);

		if (!window) return;

		drawGroups(ui, PROFILER_SECTIONS, IM_ARRAYSIZE(PROFILER_SECTIONS));
	}

	static const PanelRegistry& getPanelRegistry()
	{
		static const PanelRegistry registry = []
			{
				PanelRegistry built;

				built.addPanel("SettingsWindow", &panelSettingsWindow,
					[](const UIContext& ui) { return ui.dbg->enableSettings != 0u; });

				built.addPanel("ProfilerWindow", &panelProfilerWindow,
					[](const UIContext& ui) { return ui.dbg->enableProfilerView != 0u; });

				built.addPanel("ShaderWindow", &panelShaderWindow,
					[](const UIContext& ui)
					{
						return ui.profiler->enableShaderEditor && ui.dbg->enableSettings != 0u;
					});

				return built;
			}();

		return registry;
	}
}

// =============================================================================
// 8. Editor
// =============================================================================
void Editor::InitImgui(
	Renderer& renderer,
	GLFWwindow* window)
{
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;


	const auto& deviceCtx = renderer.GetDevice().GetContext();

	const auto& graphicsQ = renderer.GetDevice().GetGraphicsQueue();

	VK_CHECK(vkCreateDescriptorPool(deviceCtx.device, &pool_info, nullptr, &m_imguiPool));

	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Prevent ImGui from overriding the cursor
	io.IniFilename = nullptr; // Won't create imgui file

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = deviceCtx.instance;
	init_info.PhysicalDevice = deviceCtx.physicalDevice;
	init_info.Device = deviceCtx.device;
	init_info.Queue = graphicsQ.GetQueue();
	init_info.DescriptorPool = m_imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	VkFormat swapchainFormat = renderer.GetSwapchain().GetFormat();
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	io.Fonts->AddFontDefault();
	s_monoFont = io.Fonts->AddFontFromFileTTF("res/fonts/JetBrainsMono-Regular.ttf", 15.0f);

	if (s_monoFont == nullptr)
		fmt::println(stderr, "[Editor] mono font not found, using default");

	ImGui_ImplVulkan_Init(&init_info);

	glfwSetWindowFocusCallback(window, MyWindowFocusCallback);
}

void Editor::Shutdown(Renderer& renderer)
{
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(renderer.GetDevice().GetContext().device, m_imguiPool, nullptr);
}

void Editor::RenderImgui(Renderer& renderer)
{
	ImGuiIO& io = ImGui::GetIO();

	ShaderEditorPanel::Get().PumpReports(renderer);

	static bool wasShaderEditorOpen = false;
	const bool isShaderEditorOpen = renderer.GetProfiler().enableShaderEditor;

	if (wasShaderEditorOpen && !isShaderEditorOpen)
		ShaderEditorPanel::Get().ClearLog();

	wasShaderEditorOpen = isShaderEditorOpen;

	if (io.WantTextInput)
		io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
	else
		io.ConfigFlags |= ImGuiConfigFlags_NoKeyboard;

	ImGui_ImplGlfw_NewFrame();
	ImGui_ImplVulkan_NewFrame();
	ImGui::NewFrame();

	UIContext ui;
	ui.profiler = &renderer.GetProfiler();
	ui.stats = &renderer.GetFrameStats();
	ui.dbg = &renderer.GetRenderToggles();
	ui.renderer = &renderer;

	getPanelRegistry().draw(ui);

	ImGui::Render();
}
