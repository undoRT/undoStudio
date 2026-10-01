/**
 * @file WindowService.hpp
 * @brief Window management service interface
 * @ingroup services
 * 
 * This file defines the abstract interface for window management.
 * It provides platform-agnostic window creation, event handling,
 * and rendering context management.
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace undoStudio {
namespace services {

/**
 * @brief Configuration structure for window creation
 */
struct WindowConfig
{
   std::string title = "undoStudio"; ///< Window title
   int width = 1280;                 ///< Window width in pixels
   int height = 720;                 ///< Window height in pixels
   bool fullscreen = false;          ///< Fullscreen mode flag
   bool vsync = true;                ///< Vertical sync flag
   int glMajor = 3;                  ///< OpenGL major version
   int glMinor = 3;                  ///< OpenGL minor version
};

/**
 * @brief Abstract interface for window management service
 * 
 * This service provides platform-independent window management.
 * It handles window creation, event processing, and rendering
 * context management.
 */
class WindowService
{
public:
   virtual ~WindowService() = default;

   /**
     * @brief Initialize the window with the given configuration
     * @param config Window configuration parameters
     * @return true if initialization succeeded, false otherwise
     */
   virtual bool initialize(const WindowConfig& config) = 0;

   /**
     * @brief Shutdown and destroy the window
     */
   virtual void shutdown() = 0;

   /**
     * @brief Check if the window should be closed
     * @return true if the close event has been triggered
     */
   virtual bool shouldClose() const = 0;

   /**
     * @brief Poll and process all pending events
     * 
     * This method processes all pending window events including
     * input, resize, and close events.
     */
   virtual void pollEvents() = 0;

   /**
     * @brief Be told which paths were dropped onto the window
     * @param handler Called with the dropped paths, or with an empty list for a
     *                drop that carried none, which a drag from inside a browser
     *                does not.
     *
     * The handler is called from pollEvents, so it runs on the same thread as the
     * rest of the window's events and may set a request that a later frame picks
     * up. It may be null, and then drops are still delivered by the platform and
     * simply go nowhere.
     *
     * A drop arrives here rather than through ImGui because the paths come from
     * the window system, and the window is this service's. The undoApp that opens
     * them is not: see ImGuiManager::requestOpenFile.
     */
   using FileDropHandler = std::function<void(const std::vector<std::string>&)>;
   virtual void setFileDropHandler(FileDropHandler handler) = 0;

   /**
     * @brief Swap the front and back buffers
     * 
     * This method swaps the OpenGL/DirectX buffers, displaying
     * the rendered frame.
     */
   virtual void swapBuffers() = 0;

   /**
     * @brief Get the current window size
     * @return glm::ivec2 containing width and height
     */
   virtual glm::ivec2 getWindowSize() const = 0;

   /**
     * @brief Set the window size
     * @param width New window width in pixels
     * @param height New window height in pixels
     */
   virtual void setWindowSize(int width, int height) = 0;

   /**
     * @brief Set the window title
     * @param title New window title string
     */
   virtual void setTitle(const std::string& title) = 0;

   /**
     * @brief Get the native window handle
     * @return Pointer to the native window handle (platform-specific)
     */
   virtual void* getNativeHandle() const = 0;

   // Callback type definitions
   using ResizeCallback = std::function<void(int, int)>;
   using KeyCallback = std::function<void(int, int, int, int)>;
   using MouseCallback = std::function<void(double, double)>;
   using MouseButtonCallback = std::function<void(int, int, int)>;
   using ScrollCallback = std::function<void(double, double)>;
   using CloseCallback = std::function<void()>;

   /**
     * @brief Set the window resize callback
     * @param callback Function to call on window resize
     */
   virtual void setResizeCallback(ResizeCallback callback) = 0;

   /**
     * @brief Set the keyboard input callback
     * @param callback Function to call on keyboard events
     */
   virtual void setKeyCallback(KeyCallback callback) = 0;

   /**
     * @brief Set the mouse movement callback
     * @param callback Function to call on mouse movement
     */
   virtual void setMouseCallback(MouseCallback callback) = 0;

   /**
     * @brief Set the mouse button callback
     * @param callback Function to call on mouse button events
     */
   virtual void setMouseButtonCallback(MouseButtonCallback callback) = 0;

   /**
     * @brief Set the scroll wheel callback
     * @param callback Function to call on scroll events
     */
   virtual void setScrollCallback(ScrollCallback callback) = 0;

   /**
     * @brief Set the window close callback
     * @param callback Function to call on window close
     */
   virtual void setCloseCallback(CloseCallback callback) = 0;

   /**
     * @brief Get the singleton instance of WindowService
     * @return Reference to the single WindowService instance
     */
   static WindowService& getInstance();
};

} // namespace services
} // namespace undoStudio