#pragma once

#include "Core.h"
#include <cmath>

struct GLFWwindow;
class Profiler;
struct Extents2D;

class Camera
{
public:
	static constexpr float CAMERA_MIN_FOV = 70.0f;
	static constexpr float CAMERA_MAX_FOV = 103.0f;

	void SetSpawnPoint(glm::vec3 spawn) { m_defaultSpawn = spawn; }

	const glm::vec3& GetPosition() const { return m_position; }
	const glm::vec3& GetVelocity() const { return m_velocity; }
	const glm::vec3& GetView() const { return m_currentView; }
	const glm::quat& GetRotation() const { return m_rotation; }
	const glm::vec3& GetPreviousPosition() const { return m_prevPosition; }
	const glm::quat& GetPreviousRotation() const { return m_prevRotation; }

	float GetPitch() const { return m_pitch; }
	float GetYaw() const { return m_yaw; }

	float GetAcceleration() const { return m_acceleration; }
	float GetDamping() const { return m_damping; }

	float GetFovY() const { return m_fovY; }
	float GetNearClip() const { return m_nearClip; }
	float GetFarClip() const { return m_farClip; }

	float GetSensitivity() const { return m_sensitivity; }
	float GetMaxSpeed() const { return m_maxSpeed; }
	float GetMinSpeed() const { return m_minSpeed; }

	float GetAperture() const { return m_aperture; }
	float GetShutterSpeed() const { return m_shutterSpeed; }
	float GetISO() const { return m_iso; }

	glm::vec2 GetDelta() const { return m_delta; }

	// https://github.com/PanosK92/SpartanEngine/blob/1621c3e27fe671a029bbd0f9ccf22fd1c6c1fb3c/source/runtime/World/Components/Camera.h#L107
	float GetExposure() const {
		// computed ev (using squared aperture for photometric accuracy)
		// note: this calculates the exposure scale factor (1/l_avg)
		float ev100 = std::log2((m_aperture * m_aperture) / m_shutterSpeed * 100.0f / m_iso);
		// standard standard output sensitivity (sos) calculation
		// 1.2 is a common calibration constant (matches ue5/frostbite)
		// this maps the average scene luminance to middle grey (0.18)
		const float calibration_constant = 1.2f;
		float base_exposure = 1.0f / (calibration_constant * std::pow(2.0f, ev100));
		return base_exposure;
	}

	float GetEV100() const {
		return std::log2((m_aperture * m_aperture) / m_shutterSpeed * 100.0f / m_iso);
	}

	void SetPosition(const glm::vec3& pos) { m_position = pos; }
	void SetVelocity(const glm::vec3& vel) { m_velocity = vel; }

	void SetPitch(float pitch) { m_pitch = pitch; }
	void SetYaw(float yaw) { m_yaw = yaw; }

	void SetAcceleration(float accel) { m_acceleration = accel; }
	void SetDamping(float damping) { m_damping = damping; }

	void SetFovY(float fov) { m_fovY = fov; }
	void SetNearClip(float nearClip) { m_nearClip = nearClip; }
	void SetFarClip(float farClip) { m_farClip = farClip; }

	void SetSensitivity(float s) { m_sensitivity = s; }
	void SetMaxSpeed(float s) { m_maxSpeed = s; }
	void SetMinSpeed(float s) { m_minSpeed = s; }

	void SetAperture(float aperture) { m_aperture = aperture; }
	void SetShutterSpeed(float shutter) { m_shutterSpeed = shutter; }
	void SetISO(float iso) { m_iso = iso; }

	void SetDelta(glm::vec2 delta) { m_delta = delta; }

	glm::mat4 GetViewMatrix() const;
	glm::mat4 GetRotationMatrix() const;

	void ProcessInput(GLFWwindow* window, Profiler& profiler, const Extents2D& drawExtent, bool& isTemporalInvalid);
	void Reset();

private:
	glm::vec3 m_position{0.0f};
	glm::vec3 m_velocity{0.0f};

	float m_pitch{0.0f}; // vertical
	float m_yaw{0.0f};   // horizontal

	float m_acceleration{0.0f};
	float m_damping{0.0f};

	glm::vec3 m_prevPosition{0.0f};
	glm::quat m_rotation;
	glm::quat m_prevRotation;

	glm::vec3 m_currentView{0.0f};

	glm::vec2 m_delta{0.0f};

	float m_sensitivity{0.0f};
	float m_maxSpeed{0.0f};
	float m_minSpeed{0.0f};

	float m_fovY{0.0f};
	float m_nearClip{0.1f};
	float m_farClip{10000.0f}; // Good for d32 precision

	// Day time only defaults
	float m_aperture{16.0f};
	float m_shutterSpeed{1.0f / 60.0f};
	float m_iso{100.0f};

	glm::vec3 m_defaultSpawn;
};
