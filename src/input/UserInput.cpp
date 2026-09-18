#include "pch.h"

#include "UserInput.h"
#include "EngineTypes.h"

// TODO:
// Add alt-tab capabilities
// Full screen sizing

inline static constexpr std::array TrackedGlfwKeys
{
	GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
	GLFW_KEY_SPACE, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
	GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_R, GLFW_KEY_F, GLFW_KEY_P
};

namespace UserInput
{
	MouseState mouse;
	KeyboardState keyboard;

	static glm::vec2 lastPos;
	static bool firstMouse = true;
	static bool bWindowResizedThisFrame = true;

	static bool bKeyboardCaptured = false;
	static bool bMouseCaptured = false;

	Extents2D cachedWindowExtent;

	void UpdateCachedWindowExtent(uint32_t w, uint32_t h)
	{
		cachedWindowExtent.Width()  = std::max(w, 1u);
		cachedWindowExtent.Height() = std::max(h, 1u);
	}

	void NotifyWindowResized()
	{
		bWindowResizedThisFrame = true;
	}

	void SetCursorPos(GLFWwindow* window);

	// Maintains cursor to 1:1 with window sizing. Keeps mouse consistent and stable during a window resize.
	void NormalizeMousePos(GLFWwindow* window);

	void handleMouseCapture(
		GLFWwindow* window,
		bool& justClicked,
		glm::vec2& position,
		glm::vec2& delta
	);
}

void UserInput::SetCursorPos(GLFWwindow* window)
{
	glfwSetCursorPos(window, static_cast<double>(cachedWindowExtent.Width()) / 2.0, static_cast<double>(cachedWindowExtent.Height()) / 2.0);
}

void UserInput::NormalizeMousePos(GLFWwindow* window)
{
	glfwGetCursorPos(window, &mouse.mousePos[0], &mouse.mousePos[1]);
	float aspectRatio = static_cast<float>(cachedWindowExtent.Width()) / static_cast<float>(cachedWindowExtent.Height());
	mouse.normalizedPos[0] = (2.0f * static_cast<float>(mouse.mousePos[0]) / static_cast<float>(cachedWindowExtent.Width()) - 1.0f) * aspectRatio;
	mouse.normalizedPos[1] = 2.0f * static_cast<float>(mouse.mousePos[1]) / static_cast<float>(cachedWindowExtent.Height()) - 1.0f;
}

void UserInput::MouseState::Update(GLFWwindow* window)
{
	// On a resize frame the window origin and extent both moved, and while
	// iconified the framebuffer is 0x0.
	const bool bSuppressMotion =
		bWindowResizedThisFrame ||
		bMouseCaptured ||
		glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0;

	// Button state is read unconditionally so the cursor hide/show state
	// machine below can never get stuck hidden across a resize.
	rightPressed = !bMouseCaptured &&
		glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

	if (bSuppressMotion)
	{
		delta = glm::vec2(0.0f);

		// Force the next live frame to re-seed lastPos rather than
		// differencing against a position from before the resize.
		firstMouse = true;
	}
	else
	{
		NormalizeMousePos(window);

		position = glm::vec2(normalizedPos[0], normalizedPos[1]);

		if (firstMouse)
		{
			lastPos = position;
			firstMouse = false;
		}

		delta = position - lastPos;
		lastPos = position;
	}

	// --- Right click: free cam ---
	if (rightPressed)
	{
		if (!rightHideCursor)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
			SetCursorPos(window);
			rightHideCursor = true;
			rightJustClicked = true;
		}

		if (!bSuppressMotion)
			handleMouseCapture(window, rightJustClicked, position, delta);
	}
	else if (rightHideCursor)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		rightHideCursor = false;
	}
}

int UserInput::KeyboardState::ToGlfw(Keys key)
{
	static constexpr std::array<int, 12> glfwKeys
	{
		GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D,
		GLFW_KEY_SPACE, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
		GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_R, GLFW_KEY_F, GLFW_KEY_P
	};

	// Keys enum values are sequential from 0, so index directly
	return glfwKeys[static_cast<std::size_t>(key)];
}

void UserInput::KeyboardState::update(GLFWwindow* window)
{
	const bool bCaptured = bKeyboardCaptured;

	if (!bCaptured && isPressed(Keys::ESC))
		glfwSetWindowShouldClose(window, true);

	for (auto key : trackedKeys)
	{
		const int glfwKey = ToGlfw(key);

		bool isDown = false;

		if (!bCaptured)
		{
			const int state = glfwGetKey(window, glfwKey);
			isDown = (state == GLFW_PRESS || state == GLFW_REPEAT);
		}

		KeyState& prevState = keyStates[glfwKey];
		KeyState  newState = KeyState::None;

		switch (prevState) {
		case KeyState::None:
			newState = isDown ? KeyState::Pressed : KeyState::None;
			break;
		case KeyState::Pressed:
		case KeyState::Held:
			newState = isDown ? KeyState::Held : KeyState::Released;
			break;
		case KeyState::Released:
			newState = isDown ? KeyState::Pressed : KeyState::None;
			break;
		}

		if (!isDown && (prevState == KeyState::Held || prevState == KeyState::Pressed))
			newState = KeyState::Released;

		prevState = newState;
	}
}

bool UserInput::KeyboardState::isPressed(Keys key) const
{
	auto it = keyStates.find(ToGlfw(key));
	return it != keyStates.end() && it->second == KeyState::Pressed;
}

bool UserInput::KeyboardState::isHeld(Keys key) const
{
	auto it = keyStates.find(ToGlfw(key));
	if (it == keyStates.end()) return false;
	return it->second == KeyState::Held || it->second == KeyState::Pressed;
}

bool UserInput::KeyboardState::isReleased(Keys key) const
{
	auto it = keyStates.find(ToGlfw(key));
	return it != keyStates.end() && it->second == KeyState::Released;
}

void UserInput::updateLocalInput(GLFWwindow* window)
{
	const ImGuiIO& io = ImGui::GetIO();

	bKeyboardCaptured = io.WantCaptureKeyboard;

	// An in-progress look drag keeps the mouse even when the recentred
	// cursor lands under a window.
	bMouseCaptured = io.WantCaptureMouse && !mouse.rightHideCursor;

	mouse.Update(window);
	keyboard.update(window);

	bWindowResizedThisFrame = false;
}

void UserInput::KeyboardState::resetKeyStates()
{
	for (auto& [key, state] : keyStates)
	{
		state = KeyState::None;
	}
}

// Mouse recentering for consistent deltas, even across frames/resizes
void UserInput::handleMouseCapture(GLFWwindow* window, bool& justClicked, glm::vec2& position, glm::vec2& delta)
{
	SetCursorPos(window);  // always reset to center
	NormalizeMousePos(window);
	position = glm::vec2(mouse.normalizedPos[0], mouse.normalizedPos[1]);

	if (justClicked)
	{
		lastPos = position;
		justClicked = false;  // allow delta on next frame
		delta = glm::vec2(0.0f);  // prevent one-frame spike
	}
	else
	{
		delta = position - lastPos;
		lastPos = position;
	}
}
