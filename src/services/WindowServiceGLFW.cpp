/**
 * @file WindowServiceGLFW.cpp
 * @brief GLFW implementation of the WindowService interface
 * @ingroup services
 * 
 * This file implements the WindowService interface using GLFW
 * as the underlying window management library. It supports
 * OpenGL rendering and provides cross-platform window management.
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/services/WindowService.hpp"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace undoStudio {
namespace services {

/**
 * @brief GLFW implementation of WindowService
 * 
 * This class uses the GLFW library for window management and
 * event handling. It provides OpenGL context creation and
 * cross-platform window operations.
 */
class WindowServiceGLFW : public WindowService
{
public:
   WindowServiceGLFW() = default;
   ~WindowServiceGLFW() override = default;

   /**
     * @brief Initialize GLFW and create a window
     * @param config Window configuration
     * @return true on success, false on failure
     */
   bool initialize(const WindowConfig& config) override
   {
      // Initialize GLFW
      if (!glfwInit()) {
         std::cerr << "[GLFW] Failed to initialize GLFW" << std::endl;
         return false;
      }

      // Set OpenGL version hints
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, config.glMajor);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, config.glMinor);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

#ifdef __APPLE__
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

      // Create window
      GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
      m_window = glfwCreateWindow(config.width, config.height, config.title.c_str(), monitor, nullptr);

      if (!m_window) {
         std::cerr << "[GLFW] Failed to create window" << std::endl;
         glfwTerminate();
         return false;
      }

      // Make OpenGL context current
      glfwMakeContextCurrent(m_window);
      glfwSwapInterval(config.vsync ? 1 : 0);

      // Store pointer to this instance in the window user pointer
      glfwSetWindowUserPointer(m_window, this);

      // Setup all callbacks
      setupCallbacks();

		// Load icon to the window
		int iconWidth = 0, iconHeight = 0, channels = 0;
		unsigned char* iconData = stbi_load("resources/icons/undoRT_logo.png", &iconWidth, &iconHeight, &channels, 4);
		if (iconData) {
		   if (iconWidth != iconHeight) {
		      std::cout << "[GLFW] Icon not square (" << iconWidth << "x" << iconHeight << "), may not display correctly" << std::endl;
		   }
		   GLFWimage icon;
		   icon.width = iconWidth;
		   icon.height = iconHeight;
		   icon.pixels = iconData;

		   glfwSetWindowIcon(m_window, 1, &icon);
		   glfwFocusWindow(m_window);
		   stbi_image_free(iconData);
		   std::cout << "[GLFW] Window icon set" << std::endl;
		} else {
		   std::cerr << "[GLFW] Failed to load window icon" << std::endl;
		}

		std::cout << "[GLFW] Window created: " << config.width << "x" << config.height << " (" << config.title << ")" << std::endl;

      return true;
   }

   /**
     * @brief Shutdown GLFW and destroy the window
     */
   void shutdown() override
   {
      if (m_window) {
         glfwDestroyWindow(m_window);
         m_window = nullptr;
      }
      glfwTerminate();
      std::cout << "[GLFW] Shutdown complete" << std::endl;
   }

   /**
     * @brief Check if window close event has been triggered
     * @return true if window should close
     */
   bool shouldClose() const override { return glfwWindowShouldClose(m_window); }

   /**
     * @brief Poll and process all pending events
     */
   void pollEvents() override { glfwPollEvents(); }

   /**
     * @brief Swap the OpenGL front and back buffers
     */
   void swapBuffers() override { glfwSwapBuffers(m_window); }

   /**
     * @brief Get current window size
     * @return glm::ivec2 containing width and height
     */
   glm::ivec2 getWindowSize() const override
   {
      int width, height;
      glfwGetWindowSize(m_window, &width, &height);
      return glm::ivec2(width, height);
   }

   /**
     * @brief Set window size
     * @param width New window width
     * @param height New window height
     */
   void setWindowSize(int width, int height) override { glfwSetWindowSize(m_window, width, height); }

   /**
     * @brief Set window title
     * @param title New window title
     */
   void setTitle(const std::string& title) override { glfwSetWindowTitle(m_window, title.c_str()); }

   /**
     * @brief Get native GLFW window handle
     * @return Pointer to GLFWwindow
     */
   void* getNativeHandle() const override { return m_window; }

   // Callback setters
   void setResizeCallback(ResizeCallback callback) override { m_resizeCallback = callback; }

   void setKeyCallback(KeyCallback callback) override { m_keyCallback = callback; }

   void setMouseCallback(MouseCallback callback) override { m_mouseCallback = callback; }

   void setMouseButtonCallback(MouseButtonCallback callback) override { m_mouseButtonCallback = callback; }

   void setScrollCallback(ScrollCallback callback) override { m_scrollCallback = callback; }

   void setCloseCallback(CloseCallback callback) override { m_closeCallback = callback; }

private:
   /**
     * @brief Setup all GLFW callbacks
     * 
     * This method registers all necessary GLFW callbacks and
     * forwards them to the appropriate C++ callbacks.
     */
   void setupCallbacks()
   {
      // Framebuffer resize callback
      glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_resizeCallback) {
            self->m_resizeCallback(width, height);
         }
      });

      // Keyboard callback
      glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_keyCallback) {
            self->m_keyCallback(key, scancode, action, mods);
         }
      });

      // Mouse movement callback
      glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double xpos, double ypos) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_mouseCallback) {
            self->m_mouseCallback(xpos, ypos);
         }
      });

      // Mouse button callback
      glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_mouseButtonCallback) {
            self->m_mouseButtonCallback(button, action, mods);
         }
      });

      // Scroll wheel callback
      glfwSetScrollCallback(m_window, [](GLFWwindow* window, double xoffset, double yoffset) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_scrollCallback) {
            self->m_scrollCallback(xoffset, yoffset);
         }
      });

      // Window close callback
      glfwSetWindowCloseCallback(m_window, [](GLFWwindow* window) {
         auto* self = static_cast<WindowServiceGLFW*>(glfwGetWindowUserPointer(window));
         if (self && self->m_closeCallback) {
            self->m_closeCallback();
         }
      });
   }

   GLFWwindow* m_window = nullptr;
   ResizeCallback m_resizeCallback;
   KeyCallback m_keyCallback;
   MouseCallback m_mouseCallback;
   MouseButtonCallback m_mouseButtonCallback;
   ScrollCallback m_scrollCallback;
   CloseCallback m_closeCallback;
};

/**
 * @brief Factory function for creating WindowService instance
 * @return Unique pointer to a new WindowServiceGLFW instance
 */
std::unique_ptr<WindowService> createWindowService()
{
   return std::make_unique<WindowServiceGLFW>();
}

/**
 * @brief Get the singleton instance of WindowService
 * @return Reference to the single WindowService instance
 */
WindowService& WindowService::getInstance()
{
   static std::unique_ptr<WindowService> instance = createWindowService();
   return *instance;
}

} // namespace services
} // namespace undoStudio