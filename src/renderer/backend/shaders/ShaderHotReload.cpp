#include "pch.h"

#include "ShaderHotReload.h"
#include "ShaderCompiler.h"
#include "../pipelines/PipelineManager.h"

#include <chrono>

namespace fs = std::filesystem;

void ShaderHotReload::Init(ShaderCache& cache, VkDevice device, VkPipelineLayout layout)
{
	m_cache = &cache;
	m_device = device;
	m_layout = layout;

	for (const auto& record : cache.Records())
	{
		m_status[record.spvPath] = Status::Clean;

		std::error_code ec;
		const auto stamp = fs::last_write_time(record.sourcePath, ec);
		if (!ec) m_stamps[record.sourcePath] = stamp;

		for (const auto& inc : record.includes)
		{
			const auto incStamp = fs::last_write_time(inc, ec);
			if (!ec) m_stamps[inc] = incStamp;
		}
	}

	m_running.store(true, std::memory_order_release);
	m_thread = std::thread(&ShaderHotReload::ThreadMain, this);
}

void ShaderHotReload::Shutdown()
{
	m_running.store(false, std::memory_order_release);
	m_wake.notify_all();

	if (m_thread.joinable()) m_thread.join();

	std::scoped_lock lock(m_mutex);
	for (const auto& built : m_built)
		vkDestroyPipeline(m_device, built.pipeline, nullptr);
	m_built.clear();
}

void ShaderHotReload::Queue(const std::string& spvPath, std::string overrideSource)
{
	{
		std::scoped_lock lock(m_mutex);

		auto it = std::find_if(m_queue.begin(), m_queue.end(),
			[&](const Request& r) { return r.spvPath == spvPath; });

		if (it != m_queue.end())
			it->overrideSource = std::move(overrideSource);
		else
			m_queue.push_back({ spvPath, std::move(overrideSource) });

		m_status[spvPath] = Status::Queued;
		m_pending.store(static_cast<uint32_t>(m_queue.size() + m_inFlight.size()),
			std::memory_order_relaxed);
	}

	m_wake.notify_one();
}

void ShaderHotReload::QueueAll()
{
	for (const auto& record : m_cache->Records())
		Queue(record.spvPath);
}

void ShaderHotReload::ThreadMain()
{
	using namespace std::chrono_literals;

	while (m_running.load(std::memory_order_acquire))
	{
		std::vector<Request> batch;

		{
			std::unique_lock lock(m_mutex);

			m_wake.wait_for(lock, 250ms, [&]
				{
					return !m_running.load(std::memory_order_acquire) || !m_queue.empty();
				});

			if (!m_running.load(std::memory_order_acquire)) break;

			while (!m_queue.empty())
			{
				batch.push_back(std::move(m_queue.front()));
				m_queue.pop_front();
				m_inFlight.insert(batch.back().spvPath);
				m_status[batch.back().spvPath] = Status::Compiling;
			}

			m_pending.store(static_cast<uint32_t>(m_inFlight.size()),
				std::memory_order_relaxed);
		}

		if (!batch.empty())
		{
			ProcessBatch(std::move(batch));
			continue;
		}

		if (m_watchEnabled.load(std::memory_order_relaxed)) ScanForChanges();
	}
}

void ShaderHotReload::ScanForChanges()
{
	std::vector<std::string> touched;

	for (auto& [path, stamp] : m_stamps)
	{
		std::error_code ec;
		const auto live = fs::last_write_time(path, ec);
		if (ec || live == stamp) continue;

		stamp = live;
		touched.push_back(path);
	}

	if (touched.empty()) return;

	std::unordered_map<std::string, Status> statusSnapshot;
	{
		std::scoped_lock lock(m_mutex);
		statusSnapshot = m_status;
	}

	for (const auto& record : m_cache->Records())
	{
		const bool hit =
			std::find(touched.begin(), touched.end(), record.sourcePath) != touched.end() ||
			std::any_of(record.includes.begin(), record.includes.end(),
				[&](const std::string& inc)
				{
					return std::find(touched.begin(), touched.end(), inc) != touched.end();
				});

		if (!hit) continue;

		const auto it = statusSnapshot.find(record.spvPath);
		if (it != statusSnapshot.end() && it->second != Status::Clean) continue;

		Queue(record.spvPath);
	}
}

void ShaderHotReload::ProcessBatch(std::vector<Request>&& batch)
{
	const uint32_t workers = std::min<uint32_t>(
		std::max(1u, std::thread::hardware_concurrency() / 2u),
		static_cast<uint32_t>(batch.size()));

	std::vector<Result> results(batch.size());
	std::atomic<size_t> cursor{ 0u };

	std::vector<std::thread> pool;
	pool.reserve(workers);

	for (uint32_t w = 0; w < workers; ++w)
	{
		pool.emplace_back([&]
			{
				for (;;)
				{
					const size_t i = cursor.fetch_add(1u, std::memory_order_relaxed);
					if (i >= batch.size()) return;
					results[i] = ProcessOne(batch[i]);
				}
			});
	}

	for (auto& t : pool) t.join();

	for (auto& result : results)
	{
		if (result.ok)
		{
			m_cache->CommitBlob(
				result.spvPath,
				std::move(result.spirv),
				result.hash,
				std::vector<std::string>(result.includes),
				result.fromDisk,
				false);
		}
	}

	m_cache->SaveManifest();

	{
		std::scoped_lock lock(m_mutex);

		std::error_code ec;

		for (auto& result : results)
		{
			if (result.ok)
			{
				m_built.insert(m_built.end(), result.built.begin(), result.built.end());
				m_status[result.spvPath] = Status::Clean;

				if (const auto s = fs::last_write_time(result.sourcePath, ec); !ec)
					m_stamps[result.sourcePath] = s;

				for (const auto& inc : result.includes)
					if (const auto s = fs::last_write_time(inc, ec); !ec)
						m_stamps[inc] = s;
			}
			else
			{
				m_status[result.spvPath] = Status::Failed;
			}

			m_reports.push_back({ result.sourcePath, result.message, result.ok });
			m_inFlight.erase(result.spvPath);
		}

		m_pending.store(static_cast<uint32_t>(m_queue.size() + m_inFlight.size()),
			std::memory_order_relaxed);
	}
}

ShaderHotReload::Result ShaderHotReload::ProcessOne(const Request& request)
{
	Result result;
	result.spvPath = request.spvPath;
	result.fromDisk = request.overrideSource.empty();

	const ShaderRecord* found = m_cache->Find(request.spvPath);
	if (found == nullptr)
	{
		result.message = "no cache record";
		return result;
	}

	const ShaderRecord record = *found;
	result.sourcePath = record.sourcePath;

	const std::string text = result.fromDisk
		? ShaderCache::ReadSource(record.sourcePath)
		: request.overrideSource;

	if (text.empty())
	{
		result.message = "source is empty or unreadable";
		return result;
	}

	CompileResult compiled =
		ShaderCompiler::Compile(record.sourcePath, text, record.stage, false);

	if (!compiled.ok)
	{
		result.message = compiled.log;
		return result;
	}

	std::string validationLog;
	if (!ShaderCompiler::ValidateSpirv(compiled.spirv, record.stage, validationLog))
	{
		result.message = std::move(validationLog);
		return result;
	}

	for (RD::Renderer_Pipeline id : record.dependents)
	{
		const PipelineDef& def = PipelineTable::Get(id);

		std::array<std::vector<uint32_t>, MAX_PIPELINE_STAGES> owned{};
		std::array<const std::vector<uint32_t>*, MAX_PIPELINE_STAGES> blobs{};

		for (uint32_t s = 0; s < def.shaderCount; ++s)
		{
			if (record.spvPath == def.shaders[s].path)
			{
				blobs[s] = &compiled.spirv;
				continue;
			}

			owned[s] = m_cache->SnapshotSpirv(def.shaders[s].path);
			blobs[s] = &owned[s];
		}

		std::string buildLog;
		const VkPipeline built = PipelineManager::BuildPipelineObject(
			def, blobs, m_layout, VK_NULL_HANDLE, m_device, buildLog);

		if (built == VK_NULL_HANDLE)
		{
			for (const auto& b : result.built)
				vkDestroyPipeline(m_device, b.pipeline, nullptr);
			result.built.clear();

			result.message = fmt::format("{}: {}", def.DebugName(), buildLog);
			return result;
		}

		result.built.push_back({ id, built });
	}

	result.hash = result.fromDisk
		? m_cache->HashTree(record.sourcePath, compiled.includes)
		: 0ull;

	result.includes = compiled.includes;
	result.spirv = std::move(compiled.spirv);
	result.ok = true;
	result.message = fmt::format("rebuilt {} pipeline(s)", result.built.size());

	return result;
}

uint32_t ShaderHotReload::Publish(PipelineManager& pipelines)
{
	std::vector<Built> ready;
	{
		std::scoped_lock lock(m_mutex);
		if (m_built.empty()) return 0u;
		ready.swap(m_built);
	}

	for (const auto& built : ready)
		pipelines.Adopt(built.id, built.pipeline);

	return static_cast<uint32_t>(ready.size());
}

std::vector<ShaderHotReload::Report> ShaderHotReload::DrainReports()
{
	std::scoped_lock lock(m_mutex);
	std::vector<Report> out;
	out.swap(m_reports);
	return out;
}

ShaderHotReload::Status ShaderHotReload::GetStatus(const std::string& spvPath) const
{
	std::scoped_lock lock(m_mutex);
	auto it = m_status.find(spvPath);
	return it == m_status.end() ? Status::Clean : it->second;
}