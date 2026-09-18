#include "pch.h"

#include "Camera.h"
#include "UserInput.h"
#include "../profiler/Profiler.h"
#include "EngineTypes.h"

namespace UI = UserInput;

void Camera::ProcessInput(GLFWwindow* window, Profiler& profiler, const Extents2D& drawExtent, bool& isTemporalInvalid)
{
	UI::UpdateCachedWindowExtent(drawExtent.Width(), drawExtent.Height());

	if (!isTemporalInvalid)
	{
		UI::updateLocalInput(window);
	}

	m_delta = UI::mouse.delta;

	auto& debug = profiler.debugToggles;

	if (UI::keyboard.isPressed(UI::Keys::TAB))
	{
		debug.enableSettings = 1u - debug.enableSettings;
	}
	if (UI::keyboard.isPressed(UI::Keys::P))
	{
		debug.enableProfilerView = 1u - debug.enableProfilerView;
	}

	if (UI::mouse.rightPressed)
	{
		m_yaw -= UI::mouse.delta.x * m_sensitivity;
		m_pitch += UI::mouse.delta.y * m_sensitivity;
		constexpr float maxPitch = 89.0f;
		m_pitch = std::clamp(m_pitch, -maxPitch, maxPitch);
	}

	// A drag-resize or minimize parks the loop inside glfwPollEvents /
	// glfwWaitEvents for hundreds of ms.
	// Prevents feeding bad delta, especially to gpu systems down stream.
	const float rawDt = static_cast<float>(profiler.getStats().deltaSecondsRaw);
	const float dt    = std::clamp(rawDt, 1.0f / 1000.0f, 1.0f / 30.0f);

	float baseSpeed = UI::keyboard.isHeld(UI::Keys::LEFT_SHIFT) ? m_maxSpeed : m_minSpeed;

	const float radPitch = glm::radians(m_pitch);
	const float radYaw = glm::radians(m_yaw);

	glm::vec3 front = glm::normalize(glm::vec3(
		cos(radPitch) * cos(radYaw),
		sin(radPitch),
		cos(radPitch) * sin(radYaw)
	));

	m_currentView = front;

	glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
	glm::vec3 up = glm::normalize(glm::cross(right, front));

	// for world orientation relative to the camera movements
	glm::vec3 upWorld(0.0f, 1.0f, 0.0f);
	glm::vec3 flatFoward = glm::normalize(glm::vec3(m_currentView.x, 0.0f, m_currentView.z));
	glm::vec3 rightFoward = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

	glm::vec3 horiz(0.0f);
	glm::vec3 vert(0.0f);

	if (UI::keyboard.isHeld(UI::Keys::W)) { horiz += flatFoward; }
	if (UI::keyboard.isHeld(UI::Keys::S)) { horiz -= flatFoward; }
	if (UI::keyboard.isHeld(UI::Keys::A)) { horiz -= rightFoward; }
	if (UI::keyboard.isHeld(UI::Keys::D)) { horiz += rightFoward; }
	if (UI::keyboard.isHeld(UI::Keys::SPACE)) { vert += up * upWorld; }
	if (UI::keyboard.isHeld(UI::Keys::LEFT_CTRL)) { vert -= up * upWorld;}

	if (glm::length(horiz) > 0.0f) { horiz = glm::normalize(horiz); }
	if (glm::length(vert) > 0.0f) { vert = glm::normalize(vert); }

	// Build desired direction
	glm::vec3 desiredDir = horiz + vert;
	float inputMag = glm::length(desiredDir);

	if (inputMag > 0.0f)
	{
		desiredDir = glm::normalize(desiredDir);

		inputMag = inputMag * inputMag;

		float targetSpeed = baseSpeed * inputMag;

		// Project current velocity onto desired direction
		float currentSpeed = glm::dot(m_velocity, desiredDir);
		float addSpeed = targetSpeed - currentSpeed;

		if (addSpeed > 0.0f)
		{
			float accelSpeed = m_acceleration * dt * baseSpeed;
			accelSpeed = std::min(accelSpeed, addSpeed);
			m_velocity += desiredDir * accelSpeed;
		}
	}

	// Friction
	float speed = glm::length(m_velocity);
	if (speed > 0.0f)
	{
		float drop = speed * m_damping * dt;
		float newSpeed = std::max(speed - drop, 0.0f);
		m_velocity *= (newSpeed / speed);
	}

	m_prevPosition = m_position;
	m_position += m_velocity * dt;

	if (UI::keyboard.isPressed(UI::Keys::R))
	{
		Reset();
		isTemporalInvalid = true;
	}

	m_prevRotation = m_rotation;
	m_rotation = GetRotationMatrix();
}

glm::mat4 Camera::GetViewMatrix() const
{
	return glm::lookAt(m_position, m_position + m_currentView, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetRotationMatrix() const
{
	glm::quat pitchRotation = glm::angleAxis(glm::radians(m_pitch), glm::vec3{ 1.0f, 0.0f, 0.0f });
	glm::quat yawRotation = glm::angleAxis(glm::radians(m_yaw), glm::vec3{ 0.0f, 1.0f, 0.0f });

	glm::quat orientation = yawRotation * pitchRotation;
	return glm::toMat4(orientation);
}

void Camera::Reset()
{
	m_position = m_defaultSpawn;
	m_pitch = 0.0f;
	m_yaw = -90.0f;
	m_velocity = glm::vec3(0.0f);
}
