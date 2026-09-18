#include "pch.h"

#include "Engine.h"
#include "Window.h"
#include "JobSystem.h"
#include "asset/AssetManager.h"
#include "../profiler/EditorImgui.h"
#include "../input/UserInput.h"
#include "renderer/Renderer.h"
#include "asset/importers/BCNCompression.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/pipelines/PipelineManager.h"
#include "renderer/backend/descriptors/DescriptorManager.h"

namespace Engine
{
	Window _mainWindow;
	//const Window& GetWindow() { return _mainWindow; }

	Renderer _renderer;
	JobSystem _jobSystem;
	Editor _editor;
	AssetManager _assetManager;

	static constexpr uint32_t DEFAULT_WIN_EXTENT_W = 1280;
	static constexpr uint32_t DEFAULT_WIN_EXTENT_H = 960;

	bool _bIsInitialized{ false };
	bool IsInitialized() { return _bIsInitialized; }

	uint64_t _resizeGeneration = 0ull;

	void Cleanup();

	void Run();

	std::vector<SceneUploadBatch> _pendingBatches;
	std::mutex _batchMutex;
}

void Engine::Run()
{
	_mainWindow.Init(DEFAULT_WIN_EXTENT_W, DEFAULT_WIN_EXTENT_H, "Mark_3");
	InitBC7Encoder();
	_jobSystem.Init();

	_renderer.Init(_mainWindow, _jobSystem);
	_editor.InitImgui(_renderer, _mainWindow.GetWindowHandle());

	_renderer.StartTimer();
	_assetManager.LoadScenes(
		[&](SceneUploadBatch&& batch)
		{
			std::scoped_lock lock(_batchMutex);
			_pendingBatches.push_back(std::move(batch));
		},
		_jobSystem);

	_jobSystem.Wait();
	_bIsInitialized = true;

	_renderer.UploadScenes(std::move(_pendingBatches));
	_renderer.EndAssetTimer();
	_pendingBatches.clear();

	while (_mainWindow.IsOpen())
	{
		_mainWindow.PollEvents();

		if (_mainWindow.ConsumeResizeFlag())
			_renderer.RequestResize(ResizeReason::WindowEvent);

		if (!_renderer.ResolveResize(_mainWindow.GetExtent()))
		{
			glfwWaitEventsTimeout(0.1);
			continue;
		}

		if (_renderer.GetResizeGeneration() != _resizeGeneration)
		{
			_resizeGeneration = _renderer.GetResizeGeneration();
			UserInput::NotifyWindowResized();
		}

		if (_mainWindow.ThrottleIfWindowUnfocused(0.033)) continue;

		_renderer.BeginFrameTimer();
		_renderer.TickVramUsage();

		if (_renderer.ShouldRenderImgui())
		{
			_editor.RenderImgui(_renderer);
		}
		else
		{
			ImGuiIO& io = ImGui::GetIO();
			io.WantCaptureMouse = false;
			io.WantCaptureKeyboard = false;
			io.WantTextInput = false;
		}

		if (_renderer.PrepareFrame())
		{
			_renderer.EndFrameTimer();
			continue;
		}

		_renderer.StartTimer();
		_renderer.UpdateRendererContext(_mainWindow.GetWindowHandle());
		_renderer.EndSceneUpdateTimer();

		_renderer.StartTimer();
		_renderer.RecordRenderCommand(_jobSystem);
		_renderer.EndDrawTimer();

		if (_renderer.SubmitFrame())
		{
			_renderer.EndFrameTimer();
			continue;
		}

		_renderer.EndFrameTimer();
	}

	Cleanup();
}

void Engine::Cleanup()
{
	if (_bIsInitialized)
	{
		_bIsInitialized = false;
		_renderer.StallDevice();
		_assetManager.Shutdown(_jobSystem);
		_renderer.UnloadAllScenes();
		_editor.Shutdown(_renderer);
		_renderer.Cleanup();
		_jobSystem.Shutdown();
		_mainWindow.Cleanup();
	}
}
