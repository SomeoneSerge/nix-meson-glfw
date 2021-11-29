#define GLEW_STATIC

#include <msgpack.hpp> // Must go before OIIO

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <OpenImageIO/imageio.h>

#include <clipp.h>
#include <filesystem>

#include <ATen/ATen.h> // c10::InferenceMode
#include <torch/torch.h>

#include "viscor/imgui-utils.h"
#include "viscor/raii.h"
#include "viscor/utils.h"

using namespace VisCor;

struct AppArgs {
  std::string image0Path;
  std::string image1Path;
  std::string feat0Path;
  std::string feat1Path;
  bool fix01Scale = false;
  float heatmapAlpha = 0.6;

  AppArgs(int argc, char *argv[]) {
    using namespace clipp;

    bool image0set = false, image1set = false;

    auto cli =
        (value("Path to the first featuremap", feat0Path),
         value("Path to the second featuremap", feat1Path),
         option("--image0").set(image0set) & value("path", image0Path),
         option("--image1").set(image1set) & value("path", image1Path),
         option("-01", "--fix-01-scale")
             .set(fix01Scale)
             .doc("Fix the heatmap scale to [0..1]. Otherwise, adjust to "
                  "current min/max values"),
         option("-a", "--heatmap-alpha") &
             value("alpha", heatmapAlpha) %
                 "Transparency of the heatmap overlay");

    if (!clipp::parse(argc, argv, cli)) {
      std::cerr << make_man_page(cli, argv[0]);
      std::exit(1);
    }
    if (!image0set) {
      // image0Path = fs::path(tracePath) / "image0.tif";
    }
    if (!image1set) {
      // image1Path = fs::path(tracePath) / "image1.tif";
    }
  }
};

int main(int argc, char *argv[]) {

  AppArgs args(argc, argv);

  SafeGlfwCtx ctx;
  SafeGlfwWindow safeWindow;
  safeWindow.makeContextCurrent();

  GLFWwindow *window = safeWindow.window();

  SafeGlew glew;
  SafeImGui imguiContext(safeWindow.window());

  at::NoGradGuard
      inferenceModeGuard; // TODO: InferenceMode guard in a newer pytorch
  const auto device = torch::cuda::is_available() ? torch::kCUDA : torch::kCPU;

  std::cerr << "Using " << device << std::endl;

  ImHeatSlice heatView(
      loadExrField(args.feat0Path, device),
      loadExrField(args.feat1Path, device),
      SafeGlTexture(oiioLoadImage(args.image0Path), GL_NEAREST),
      SafeGlTexture(oiioLoadImage(args.image1Path), GL_NEAREST), device,
      args.fix01Scale);

  constexpr auto defaultWindowOptions = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoResize;
  while (!glfwWindowShouldClose(window)) {

    GlfwFrame glfwFrame(window);
    ImGuiGlfwFrame imguiFrame;

    struct {
      int x, y;
    } glfwSize;
    glfwGetWindowSize(window, &glfwSize.x, &glfwSize.y);

    const auto toolboxHeight = ImGui::GetTextLineHeightWithSpacing() * 6;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(glfwSize.x, toolboxHeight),
                                        ImVec2(glfwSize.x, toolboxHeight));

    if (ImGui::Begin("Toolbox", nullptr, defaultWindowOptions)) {
      ImGui::SliderFloat("Heatmap Alpha", &heatView.alpha, 0.0, 1.0);
      heatView.alpha = normalizeAlpha(heatView.alpha);

      ImGui::Checkbox("exp", &heatView.newQuery.exp);
    }
    ImGui::End();

    const auto workArea = ImVec2(glfwSize.x, glfwSize.y - toolboxHeight);
    const auto neededArea =
        ImVec2(workArea.x, .5 * workArea.x * heatView.image0.aspect());
    ImGui::SetNextWindowPos(ImVec2(0, toolboxHeight));
    ImGui::SetNextWindowSizeConstraints(
        neededArea,
        ImVec2(neededArea.x, std::max<double>(glfwSize.y, neededArea.y)));

    ImGui::Begin("Window0", nullptr, defaultWindowOptions);
    heatView.draw();
    ImGui::End();
  }
  return 0;
}
