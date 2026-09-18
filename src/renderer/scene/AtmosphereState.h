#pragma once

#include "AtmosphereTypes.h"
#include <cstring>

// Render-thread ownership. Prepare before graph conditions/parallel recording.
// MarkSubmitted only after a successful GRAPHICS queue submission.
class AtmosphereState
{
public:
	void Prepare(const AtmosphereSettings& settings, uint64_t frameNumber)
	{
		AtmosphereParameters next = settings.parameters;
		SanitizeAtmosphere(next);
		if (!m_hasParameters || std::memcmp(&next, &m_parameters, sizeof(next)) != 0)
		{
			m_parameters = next;
			m_hasParameters = true;
			++m_parameterVersion;
		}
		m_frame = frameNumber;
		m_recorded = false;
		m_debugMode = std::min(settings.debugMode, 2u);
		m_debugScale = std::max(settings.opticalDepthScale, 0.0f);
	}

	void Invalidate() noexcept { m_submittedVersion = 0u; }
	bool NeedsBuild() const noexcept { return m_parameterVersion != m_submittedVersion; }
	bool DebugActive() const noexcept { return m_debugMode != 0u; }
	const AtmosphereParameters& Parameters() const noexcept { return m_parameters; }
	uint64_t SubmittedVersion() const noexcept { return m_submittedVersion; }

	AtmosphereDebugPush DebugPush() const
	{
		AtmosphereDebugPush push{};
		push.parameters = m_parameters;
		push.display.x = m_debugScale;
		push.mode.x = m_debugMode;
		return push;
	}

	void MarkRecorded() noexcept { m_recorded = true; }
	void MarkSubmitted(uint64_t frameNumber) noexcept
	{
		if (m_recorded && frameNumber == m_frame)
			m_submittedVersion = m_parameterVersion;
		m_recorded = false;
	}
	void MarkAborted() noexcept { m_recorded = false; }

private:
	AtmosphereParameters m_parameters{};
	uint64_t m_parameterVersion = 0u;
	uint64_t m_submittedVersion = 0u;
	uint64_t m_frame = 0u;
	uint32_t m_debugMode = 0u;
	float m_debugScale = 0.0f;
	bool m_hasParameters = false;
	bool m_recorded = false;
};
