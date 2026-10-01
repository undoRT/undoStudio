/**
 * @file DemoApp.cpp
 * @brief Implementation of the demo undoApp
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "DemoApp.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/Application.hpp"

#include <imgui.h>
#include <implot.h>
#include <iostream>
#include <random>

namespace undoApp {
namespace Demo {

DemoApp& DemoApp::getInstance()
{
   static DemoApp instance;
   return instance;
}

bool DemoApp::initialize()
{
   if (m_initialized) {
      return true;
   }

   std::cout << "[undoApp.Demo] Initializing..." << std::endl;
   registerPanels();

   m_initialized = true;
   std::cout << "[undoApp.Demo] Initialization complete" << std::endl;
   return true;
}

void DemoApp::shutdown()
{
   if (!m_initialized) {
      return;
   }

   std::cout << "[undoApp.Demo] Shutting down..." << std::endl;

   // Remove our panels BEFORE we return: once the plugin is dlclose()'d,
   // the std::function objects ImGuiManager stores for these panels would
   // point at unmapped code.
   auto& imguiManager = undoStudio::ui::ImGuiManager::getInstance();
   imguiManager.removePanel("undoApp Demo");
   imguiManager.removePanel("About undoApp.Demo");

   m_initialized = false;
   std::cout << "[undoApp.Demo] Shutdown complete" << std::endl;
}

void DemoApp::registerPanels()
{
   auto& imguiManager = undoStudio::ui::ImGuiManager::getInstance();

   // Register main demo panel
   imguiManager.addPanel("undoApp Demo", renderPanel);
   imguiManager.addPanel("About undoApp.Demo", renderAboutPanel);

   std::cout << "[undoApp.Demo] Panels registered" << std::endl;
}

void DemoApp::renderPanel()
{
   // This panel demonstrates various ImGui and ImPlot features

   if (ImGui::BeginTabBar("DemoTabs")) {
      // Controls tab
      if (ImGui::BeginTabItem("Controls")) {
         static bool checkbox = false;
         static int slider = 50;
         static float color[3] = {1.0f, 0.5f, 0.2f};
         static char buffer[256] = "Hello undoStudio!";
         static int comboSelection = 0;
         static const char* comboItems[] = {"Option 1", "Option 2", "Option 3"};

         // Various widgets
         ImGui::Checkbox("Enable Feature", &checkbox);
         ImGui::SliderInt("Value", &slider, 0, 100);
         ImGui::ColorPicker3("Color Picker", color);
         ImGui::InputText("Input", buffer, sizeof(buffer));
         ImGui::Combo("Selection", &comboSelection, comboItems, 3);

         if (ImGui::Button("Execute")) {
            std::cout << "[undoApp.Demo] Button clicked!" << std::endl;
         }

         ImGui::SameLine();

         if (ImGui::Button("Reset")) {
            checkbox = false;
            slider = 50;
            std::cout << "[undoApp.Demo] Reset to defaults" << std::endl;
         }

         ImGui::EndTabItem();
      }

      // Data tab
      if (ImGui::BeginTabItem("Data")) {
         static float progress = 0.5f;

         ImGui::Text("Data Processing Status:");
         ImGui::ProgressBar(progress, ImVec2(-1.0f, 20.0f));

         if (ImGui::Button("Start Process")) {
            std::cout << "[undoApp.Demo] Starting data processing..." << std::endl;
            // Simulate progress
            for (int i = 0; i <= 10; i++) {
               progress = i / 10.0f;
               // In a real app, this would update in real-time
            }
         }

         // Tree view demonstration
         if (ImGui::TreeNode("Data Tree")) {
            if (ImGui::TreeNode("Input Data")) {
               ImGui::Text("  - Signal A: 42.5");
               ImGui::Text("  - Signal B: 13.2");
               ImGui::Text("  - Signal C: 67.8");
               ImGui::TreePop();
            }
            if (ImGui::TreeNode("Processed Data")) {
               ImGui::Text("  - Result: 123.45");
               ImGui::Text("  - Status: OK");
               ImGui::TreePop();
            }
            ImGui::TreePop();
         }

         ImGui::EndTabItem();
      }

      // Plot tab
      if (ImGui::BeginTabItem("Plots")) {
         // Multiple plots demonstration
         static std::random_device rd;
         static std::mt19937 gen(rd());
         static std::uniform_real_distribution<> dis(0.0, 1.0);
         static float data[100];

         // Update data
         for (int i = 0; i < 99; i++) {
            data[i] = data[i + 1];
         }
         data[99] = dis(gen);

         if (ImPlot::BeginPlot("Real-Time Data", ImVec2(-1, 150))) {
            ImPlot::SetupAxes("Sample", "Value");
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, 100);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1.2);
            ImPlot::PlotLine("Signal", data, 100);
            ImPlot::EndPlot();
         }

         // Additional plot with two signals
         static float data2[100];
         for (int i = 0; i < 99; i++) {
            data2[i] = data2[i + 1];
         }
         data2[99] = 0.5f + 0.4f * sinf(data[99] * 10.0f);

         if (ImPlot::BeginPlot("Comparison", ImVec2(-1, 150))) {
            ImPlot::SetupAxes("Sample", "Value");
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, 100);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -0.2, 1.2);
            ImPlot::PlotLine("Signal 1", data, 100);
            ImPlot::PlotLine("Signal 2", data2, 100);
            ImPlot::EndPlot();
         }

         ImGui::EndTabItem();
      }

      // Log tab
      if (ImGui::BeginTabItem("Log")) {
         static bool autoscroll = true;
         static int logLevel = 0;
         static const char* levels[] = {"Debug", "Info", "Warning", "Error"};

         ImGui::Checkbox("Auto-scroll", &autoscroll);
         ImGui::SameLine();
         ImGui::Combo("Level", &logLevel, levels, 4);

         ImGui::Separator();

         // Simulated log content
         ImGui::BeginChild("LogView", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

         ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[2024-01-15 10:23:45] DEBUG: Initializing undoApp.Demo");
         ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[2024-01-15 10:23:46] INFO: Panel registered successfully");
         ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "[2024-01-15 10:23:47] WARNING: Configuration file not found, using defaults");
         ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[2024-01-15 10:23:48] ERROR: Failed to connect to runtime (timeout)");

         if (autoscroll) {
            ImGui::SetScrollHereY(1.0f);
         }

         ImGui::EndChild();

         ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
   }
}

void DemoApp::renderAboutPanel()
{
   ImGui::Text("undoApp.Demo");
   ImGui::Text("Version: " STUDIO_VERSION_STRING);
   ImGui::Separator();
   ImGui::Text("This is a demo undoApp for undoStudio.");
   ImGui::Text("It demonstrates the extension capabilities");
   ImGui::Text("of the undoStudio IDE framework.");
   ImGui::Separator();
   ImGui::Text("Features demonstrated:");
   ImGui::BulletText("ImGui widgets integration");
   ImGui::BulletText("ImPlot plotting capabilities");
   ImGui::BulletText("Tab-based UI organization");
   ImGui::BulletText("Real-time data visualization");
   ImGui::BulletText("Logging and diagnostics");
}

// Entry point function for plugin loading (future use)
extern "C" {
void* createUndoApp()
{
   auto& app = DemoApp::getInstance();
   app.initialize();
   return &app;
}

void destroyUndoApp(void* app)
{
   auto* demoApp = static_cast<DemoApp*>(app);
   demoApp->shutdown();
}
}

} // namespace Demo
} // namespace undoApp