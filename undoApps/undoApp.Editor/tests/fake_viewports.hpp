// A viewport-capable ImGui context with no window behind it.
//
// The ST editor's overlays are placed with SetNextWindowPos(), whose coordinates
// are relative to the viewport they go into, and a window is merged into the main
// viewport only if its rectangle is inside that viewport's. Both facts are
// invisible while the main viewport sits at the screen's top left corner, which is
// where every headless test had it: the offset the overlays were computed with was
// zero, and no test could fail on it.
//
// Enabling ImGuiConfigFlags_ViewportsEnable needs more than the flag. The sanity
// checks in NewFrame turn the feature off unless both backend flags are set, and
// then assert that the platform handlers and the monitor list are there, so all of
// it is stubbed here: enough for ImGui to believe it has a windowing system, and
// nothing that draws.
#ifndef UNDOSTUDIO_TESTS_FAKE_VIEWPORTS_HPP
#define UNDOSTUDIO_TESTS_FAKE_VIEWPORTS_HPP

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <vector>

/// A window ImGui believes in, with the position and size it is told about
struct FakeWindow {
  ImVec2 pos = ImVec2(0.0f, 0.0f);
  ImVec2 size = ImVec2(1280.0f, 800.0f);
  bool focused = true;
  int ownsContext = 0;
};

/// Where the main window is, which is what every test here moves
inline ImVec2& fakeMainWindowPos() {
  static ImVec2 pos(1920.0f, 0.0f);
  return pos;
}

/// The main window's size, taken from io.DisplaySize so a test sets it there
inline ImVec2 fakeMainWindowSize() {
  return ImGui::GetIO().DisplaySize;
}

/// @brief Give the context a windowing system good enough for multi-viewport
///
/// @param size The main window's size
///
/// Every platform viewport ImGui asks about is a FakeWindow hanging off
/// PlatformUserData, and each is counted in the focus and the current-context
/// totals, so a test can see whether the editor asked for a second window at all.
inline void installFakeViewportBackend(const ImVec2& size) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = size;
  io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;

  static ImGuiPlatformMonitor monitor;
  monitor.WorkPos = ImVec2(0.0f, 0.0f);
  monitor.WorkSize = ImVec2(5760.0f, 2400.0f);
  monitor.MainPos = monitor.WorkPos;
  monitor.MainSize = monitor.WorkSize;
  monitor.DpiScale = 1.0f;

  ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
  platform.Platform_CreateWindow = [](ImGuiViewport* viewport) {
    viewport->PlatformUserData = new FakeWindow();
    viewport->PlatformWindowCreated = true;
  };
  platform.Platform_DestroyWindow = [](ImGuiViewport* viewport) {
    delete static_cast<FakeWindow*>(viewport->PlatformUserData);
    viewport->PlatformUserData = nullptr;
    viewport->PlatformWindowCreated = false;
  };
  platform.Platform_GetWindowPos = [](ImGuiViewport* viewport) -> ImVec2 {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    return window ? window->pos : fakeMainWindowPos();
  };
  platform.Platform_SetWindowPos = [](ImGuiViewport* viewport, const ImVec2 pos) {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    if (window) window->pos = pos;
  };
  platform.Platform_GetWindowSize = [](ImGuiViewport* viewport) -> ImVec2 {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    return window ? window->size : fakeMainWindowSize();
  };
  platform.Platform_SetWindowSize = [](ImGuiViewport* viewport, const ImVec2 size) {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    if (window) window->size = size;
  };
  platform.Platform_GetWindowFramebufferScale = [](ImGuiViewport*) { return ImVec2(1.0f, 1.0f); };
  platform.Platform_GetWindowFocus = [](ImGuiViewport* viewport) {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    return window ? window->focused : true;
  };
  platform.Platform_GetWindowMinimized = [](ImGuiViewport*) { return false; };
  platform.Platform_SetWindowAlpha = [](ImGuiViewport*, float) {};
  platform.Platform_GetWindowDpiScale = [](ImGuiViewport*) { return 1.0f; };
  platform.Platform_SetWindowFocus = [](ImGuiViewport* viewport) {
    FakeWindow* window = static_cast<FakeWindow*>(viewport->PlatformUserData);
    if (window) window->focused = true;
  };
  // UpdatePlatformWindows() calls these without checking that they are there, so
  // leaving them out is a jump to a null pointer rather than a skipped call.
  platform.Platform_ShowWindow = [](ImGuiViewport*) {};
  platform.Platform_SetWindowTitle = [](ImGuiViewport*, const char*) {};

  // The monitor list is cleared and refilled every frame from the platform side, so
  // a single entry that covers everything is all that is needed.
  platform.Platform_SetWindowAlpha = [](ImGuiViewport*, float) {};
  platform.Monitors.resize(0);
  platform.Monitors.push_back(monitor);

  // The main viewport is the one ImGui does not create, so it is given its window
  // here rather than by Platform_CreateWindow.
  if (ImGuiViewport* main = ImGui::GetMainViewport()) {
    if (main->PlatformUserData == nullptr) main->PlatformUserData = new FakeWindow();
    static_cast<FakeWindow*>(main->PlatformUserData)->size = size;
    static_cast<FakeWindow*>(main->PlatformUserData)->pos = fakeMainWindowPos();
    main->PlatformWindowCreated = true;
  }
}

/// @brief How many platform windows exist, the main one included
///
/// This is the count that decides whether a second OS window was asked for, which
/// is what the overlays must not do.
inline int fakeWindowCount() {
  int count = 0;
  for (int i = 0; i < ImGui::GetPlatformIO().Viewports.Size; ++i) {
    if (ImGui::GetPlatformIO().Viewports[i]->PlatformWindowCreated) ++count;
  }
  return count;
}

/// @brief The main viewport's rectangle on the screen
inline ImRect fakeMainViewportRect() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  return ImRect(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
}

#endif
