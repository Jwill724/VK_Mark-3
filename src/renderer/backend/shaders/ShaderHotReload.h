#pragma once

#include "ShaderCache.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_set>

class PipelineManager;

class ShaderHotReload final
{
public:
	enum class Status : uint8_t { Clean, Queued, Compiling, Failed };

	struct Report
	{
		std::string shader;
		std::string message;
		bool        ok = false;
	};

	void Init(ShaderCache& cache, VkDevice device, VkPipelineLayout layout);
	void Shutdown();

	// overrideSource empty = read from disk
	void Queue(const std::string& spvPath, std::string overrideSource = {});
	void QueueAll();

	// Render thread. Returns how many pipelines were swapped.
	uint32_t Publish(PipelineManager& pipelines);

	std::vector<Report> DrainReports();

	Status   GetStatus(const std::string& spvPath) const;
	uint32_t PendingCount() const noexcept { return m_pending.load(std::memory_order_relaxed); }

	void SetWatchEnabled(bool enabled) { m_watchEnabled.store(enabled, std::memory_order_relaxed); }
	bool IsWatchEnabled() const { return m_watchEnabled.load(std::memory_order_relaxed); }

private:
	struct Request { std::string spvPath; std::string overrideSource; };
	struct Built { RD::Renderer_Pipeline id; VkPipeline pipeline; };

	struct Result
	{
		std::string              spvPath;
		std::string              sourcePath;
		std::vector<Built>       built;
		std::vector<uint32_t>    spirv;
		std::vector<std::string> includes;
		uint64_t                 hash = 0ull;
		bool                     fromDisk = true;
		bool                     ok = false;
		std::string              message;
	};

	void ThreadMain();
	void ProcessBatch(std::vector<Request>&& batch);
	Result ProcessOne(const Request& request);   // no locking inside
	void ScanForChanges();

	ShaderCache* m_cache = nullptr;
	VkDevice         m_device = VK_NULL_HANDLE;
	VkPipelineLayout m_layout = VK_NULL_HANDLE;

	std::thread             m_thread;
	std::atomic<bool>       m_running{ false };
	std::atomic<bool>       m_watchEnabled{ true };
	std::atomic<uint32_t>   m_pending{ 0u };

	mutable std::mutex      m_mutex;
	std::condition_variable m_wake;

	std::deque<Request>             m_queue;
	std::unordered_set<std::string> m_inFlight;
	std::vector<Built>              m_built;
	std::vector<Report>             m_reports;

	std::unordered_map<std::string, Status> m_status;

	std::unordered_map<std::string, std::filesystem::file_time_type> m_stamps;
};