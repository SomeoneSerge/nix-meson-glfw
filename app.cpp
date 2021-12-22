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

#include <ATen/ATen.h>    // c10::InferenceMode
#include <torch/script.h> // One-stop header.
#include <torch/torch.h>

#include "viscor/imgui-utils.h"
#include "viscor/raii.h"
#include "viscor/utils.h"

using namespace VisCor;

struct AppArgs {
  std::string image0Path;
  std::string image1Path;
  std::string modelPath;
  bool fix01Scale = false;
  float heatmapAlpha = 0.6;

  AppArgs(int argc, char *argv[]) {
    using namespace clipp;

    auto cli =
        (value("Path to the traced model", modelPath),
         value("image0 path", image0Path), value("image1 path", image1Path),
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
  }
};

std::tuple<torch::Tensor, torch::Tensor> extract(torch::jit::script::Module &m,
                                                 const Uint8Image &image0,
                                                 const Uint8Image &image1,
                                                 const torch::Device &device) {

  std::vector<torch::jit::IValue> inputs;
  inputs.push_back(tensorFromImage(image0)
                       .to(device, torch::kF32)
                       .squeeze(-1)
                       .unsqueeze(0)
                       .unsqueeze(0)
                       .div(255.0));
  inputs.push_back(tensorFromImage(image1)
                       .to(device, torch::kF32)
                       .squeeze(-1)
                       .unsqueeze(0)
                       .unsqueeze(0)
                       .div(255.0));
  const auto outputs = m.forward(inputs).toTuple();
  return std::make_tuple(
      outputs->elements()[0].toTensor().squeeze(0).contiguous(),
      outputs->elements()[1].toTensor().squeeze(0).contiguous());
}

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

  std::cerr << "[I] Using " << device << std::endl;

  torch::jit::script::Module model = torch::jit::load(args.modelPath, device);

  Uint8Image image0(oiioLoadImage(args.image0Path));
  Uint8Image image1(oiioLoadImage(args.image1Path));

  const auto outputs = extract(model, image0, image1, device);

  ImHeatSlice heatView(DescriptorField::fromTensor(std::get<0>(outputs)),
                       DescriptorField::fromTensor(std::get<1>(outputs)),
                       Uint8Image(image0), Uint8Image(image1), device, args.fix01Scale);

  if (heatView.desc0.chw_data.isnan().any().item<bool>()) {
    std::cerr << "[E] desc0: Found "
              << heatView.desc0.chw_data.isnan().sum().item<long>() << " NaNs"
              << std::endl;
  }
  if (heatView.desc1.chw_data.isnan().any().item<bool>()) {
    std::cerr << "[E] desc1: Found "
              << heatView.desc1.chw_data.isnan().sum().item<long>() << " NaNs"
              << std::endl;
  }

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
        ImVec2(workArea.x, .5 * workArea.x * heatView.tex0.aspect());
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
