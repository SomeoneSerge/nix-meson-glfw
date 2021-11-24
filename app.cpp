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

#include "viscor/raii.h"
#include "viscor/utils.h"

using namespace VisCor;

const char *vtxSource = R"glsl(
    #version 150 core

    in vec2 position;

    void main()
    {
        gl_Position = vec4(position, 0.0, 1.0);
    }
)glsl";

const char *fragSource = R"glsl(
   #version 150 core
   
   out vec4 outColor;

   void main()
   {
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
   }
)glsl";

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

ImPlotColormap colormapTransparentResample(ImPlotColormap src, int newRes,
                                           double alpha) {
  const std::string name(std::string(ImPlot::GetColormapName(src)) + "-" +
                         std::to_string(newRes) + "-" + std::to_string(alpha));
  const int size = ImPlot::GetColormapSize(src);
  std::vector<ImVec4> colors(newRes);
  for (int i = 0; i < newRes; ++i) {
    const auto t = i / (newRes - 1.0);
    const auto j = t * (size - 1.0);
    const int j0 = std::floor(j);
    const int j1 = std::ceil(j);
    const auto c0 = ImPlot::GetColormapColor((int)j0, src);
    const auto c1 = ImPlot::GetColormapColor((int)j1, src);
    if (j1 == j0) {
      colors[i] = c0;
    } else {
      const auto u = j - j0;
      const auto u1 = j1 - j;
      colors[i] = ImVec4(u1 * c0.x + u * c1.x, u1 * c0.y + u * c1.y,
                         u1 * c0.z + u * c1.z, u1 * c0.w + u * c1.w);
    }
    colors[i] = c0;
    colors[i].w *= alpha;
  }
  return ImPlot::AddColormap(name.c_str(), colors.data(), newRes, false);
}

double normalizeAlpha(double alpha) {
  return std::max(std::min(std::round(alpha * 100.0) / 100.0, 1.0), 0.0);
}

std::string alphaToString(double alpha) {
  return std::to_string(normalizeAlpha(alpha));
}

ImPlotColormap colormapTransparentCopy(ImPlotColormap src, double alpha) {
  const int size = ImPlot::GetColormapSize(src);
  alpha = normalizeAlpha(alpha);
  const std::string name(alphaToString(alpha) + "_" +
                         std::string(ImPlot::GetColormapName(src)) + "_" +
                         std::to_string(size));

  {
    const auto id = ImPlot::GetColormapIndex(name.c_str());
    if (id != -1)
      return id;
  }

  std::vector<ImVec4> colors(size);
  for (int i = 0; i < size; ++i) {
    colors[i] = ImPlot::GetColormapColor(i, src);
    colors[i].w *= alpha;
  }

  const auto cmap =
      ImPlot::AddColormap(name.c_str(), colors.data(), size, false);
  return cmap;
}

struct DescriptorField {
  std::tuple<int, int, int> shape;
  torch::Tensor data;

  DescriptorField(const DescriptorField &) = delete;
  DescriptorField() = default;
  DescriptorField(DescriptorField &&) = default;

  torch::Tensor operator()(const int i, const int j) const {
    using namespace torch::indexing;
    return data.index({Slice(), i, j});
  }

  int h() const { return std::get<0>(shape); }
  int w() const { return std::get<1>(shape); }
  int c() const { return std::get<2>(shape); }
};

// FIXME: rm shitcode
DescriptorField loadExrField(const fs::path &path,
                             const torch::Device &device) {
  using namespace OIIO;
  std::unique_ptr<ImageInput> in = ImageInput::open(path);
  const ImageSpec &spec = in->spec();

  std::vector<std::string> descChannels;
  for (const auto &c : spec.channelnames) {
    if (!c.starts_with("superglue."))
      continue;
    descChannels.push_back(c);
  }

  const auto nChannels = descChannels.size();
  const auto shape = std::make_tuple(spec.height, spec.width, nChannels);

  if (nChannels != 256) {
    std::cerr << "Expected 256 channels, got " << std::get<2>(shape)
              << std::endl;
  }

  DescriptorField f;
  f.shape = shape;

  f.data = torch::empty({(long)nChannels, spec.height, spec.width},
                        torch::TensorOptions().dtype(torch::kF32));

  long offset = 0;
  for (const auto &c : descChannels) {
    const auto channelIdx = spec.channelindex(c);
    in->read_image(channelIdx, channelIdx + 1, TypeDesc::FLOAT,
                   f.data.data_ptr<float>() + offset);
    offset += spec.width * spec.height;
  }

  f.data = f.data.clone().to(device);
  return f;
}

struct Message {
  double u0 = .5, v0 = .5; // cursor position in plot 0
  double heatMin = 0, heatMax = 1;
  int iSlice = 0, jSlice = 0;
  float alpha = 1.0;
  bool exp = false;
  std::chrono::time_point<std::chrono::system_clock> lastHeatmapSwitch;
};

struct State : public Message {
  torch::Tensor heat;
  torch::Tensor heatOnDevice;
};

int main(int argc, char *argv[]) {

  AppArgs args(argc, argv);

  SafeGlfwCtx ctx;
  SafeGlfwWindow safeWindow;
  safeWindow.makeContextCurrent();

  GLFWwindow *window = safeWindow.window();

  SafeGlew glew;
  SafeImGui imguiContext(safeWindow.window());

  float vtxs[] = {0.0f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
  SafeVBO safeVbo(sizeof(vtxs), vtxs);

  VtxFragProgram triangleProgram(vtxSource, fragSource);

  /* TODO: handle inputs/outputs of the shaders */
  glBindFragDataLocation(triangleProgram.program(), 0, "outColor");

  glLinkProgram(triangleProgram.program());
  glUseProgram(triangleProgram.program());

  SafeVAO triangleVao;

  GLint posAttrib = glGetAttribLocation(triangleProgram.program(), "position");
  glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(posAttrib);

  // FIXME:
  SafeGlTexture image0(oiioLoadImage(args.image0Path), GL_NEAREST);
  SafeGlTexture image1(oiioLoadImage(args.image1Path), GL_NEAREST);

  at::NoGradGuard
      inferenceModeGuard; // TODO: InferenceMode guard in a newer pytorch
  const auto device = torch::cuda::is_available() ? torch::kCUDA : torch::kCPU;

  std::cerr << "Using " << device << std::endl;

  const auto desc0 = loadExrField(args.feat0Path, device);
  std::cerr << "Finished loading " << args.feat0Path << std::endl;

  const auto desc1 = loadExrField(args.feat1Path, device);
  std::cerr << "Finished loading " << args.feat1Path << std::endl;

  State state{
      .heat = torch::empty(
          {desc1.h(), desc1.w()},
          torch::TensorOptions().device(torch::kCPU).dtype(torch::kFloat32)),
      .heatOnDevice = torch::zeros(
          {desc1.h(), desc1.w()},
          torch::TensorOptions().device(torch::kCPU).dtype(torch::kF32))};
  state.alpha = args.heatmapAlpha;

  std::cerr << "Finished initialization of State" << std::endl;

  constexpr auto defaultWindowOptions = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoResize;
  constexpr auto defaultPlotOptions =
      ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased | ImPlotFlags_Crosshairs;

  while (!glfwWindowShouldClose(window)) {
    Message msg{
        .u0 = state.u0,
        .v0 = state.v0,
        .iSlice = state.iSlice,
        .jSlice = state.jSlice,
        .alpha = state.alpha,
        .exp = state.exp,
        .lastHeatmapSwitch = state.lastHeatmapSwitch,
    };

    GlfwFrame glfwFrame(window);
    ImGuiGlfwFrame imguiFrame;

    glDrawArrays(GL_TRIANGLES, 0, 3);

    struct {
      int x, y;
    } glfwSize;
    glfwGetWindowSize(window, &glfwSize.x, &glfwSize.y);

    const auto toolboxHeight = ImGui::GetTextLineHeightWithSpacing() * 6;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(glfwSize.x, toolboxHeight),
                                        ImVec2(glfwSize.x, toolboxHeight));

    if (ImGui::Begin("Toolbox", nullptr, defaultWindowOptions)) {
      ImGui::SliderFloat("Heatmap Alpha", &msg.alpha, 0.0, 1.0);
      msg.alpha = normalizeAlpha(msg.alpha);

      ImGui::Checkbox("exp", &msg.exp);
    }
    ImGui::End();

    const auto cmap =
        colormapTransparentCopy(ImPlotColormap_Viridis, msg.alpha);

    const auto workArea = ImVec2(glfwSize.x, glfwSize.y - toolboxHeight);
    const auto neededArea =
        ImVec2(workArea.x, .5 * workArea.x * image0.aspect());
    ImGui::SetNextWindowPos(ImVec2(0, toolboxHeight));
    ImGui::SetNextWindowSizeConstraints(
        neededArea,
        ImVec2(neededArea.x, std::max<double>(glfwSize.y, neededArea.y)));
    ImGui::Begin("Window0", nullptr, defaultWindowOptions);

    const auto frameSize = ImGui::GetWindowSize();
    const auto cmapWidth = 100;
    const auto plotSize =
        ImVec2(.5 * (frameSize.x - cmapWidth),
               .5 * (frameSize.x - cmapWidth) * image0.aspect());

    if (ImPlot::BeginPlot("Image0", nullptr, nullptr, plotSize,
                          defaultPlotOptions)) {
      ImPlot::PlotImage("im0", image0.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      const auto xyNew = ImPlot::GetPlotMousePos();
      ImPlotPoint xyDrag(state.u0, 1.0 - state.v0);

      const auto queryColor = ImVec4(255 / 255.0, 99 / 255.0, 71 / 255.0, 1.0);
      if (ImPlot::DragPoint("Query", &xyDrag.x, &xyDrag.y, true, queryColor,
                            6)) {
        xyDrag.x = xyNew.x;
        xyDrag.y = xyNew.y;
      }
      if (ImPlot::DragLineX("QueryX", &xyDrag.x, true, queryColor)) {
        xyDrag.x = xyNew.x;
      }
      if (ImPlot::DragLineY("QueryY", &xyDrag.y, true, queryColor)) {
        xyDrag.y = xyNew.y;
      }

      const auto uv = ImVec2(xyDrag.x, 1.0 - xyDrag.y);
      msg.u0 = uv.x;
      msg.v0 = uv.y;

      const auto i =
          std::max(0, std::min((int)(uv.y * desc0.h()), desc0.h() - 1));
      const auto j =
          std::max(0, std::min((int)(uv.x * desc0.w()), desc0.w() - 1));
      msg.iSlice = i;
      msg.jSlice = j;

      ImPlot::EndPlot();
    }

    ImGui::SameLine();

    if (ImPlot::BeginPlot("Image1", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      bool cachedSlice = msg.exp == state.exp && msg.iSlice == state.iSlice &&
                         msg.jSlice == state.jSlice && state.heat.size(0) > 0 &&
                         state.heat.size(1) > 0;

      if (!cachedSlice) {
        const auto stdvar = std::sqrt(desc1.c());
        const auto query =
            desc0(msg.iSlice, msg.jSlice).reshape({desc0.c(), 1, 1}) / stdvar;
        state.heatOnDevice = desc1.data.div(stdvar).mul(query).sum(0);
        if (msg.exp) {
          state.heatOnDevice.exp_();
        }
        // ImPlot color interpolation crashes whenever it sees NaNs or
        // infinities
        const auto max = 1e30; // std::numeric_limits<float>::quiet_NaN();
        state.heatOnDevice.nan_to_num_(max, max, -max);
        state.heatOnDevice.clip_(-max, max);

        state.heat.copy_(state.heatOnDevice.to(torch::kCPU));
      }

      if (args.fix01Scale) {
        msg.heatMin = 0;
        msg.heatMax = 1;
      } else {
        msg.heatMin = state.heatOnDevice.min().item<double>();
        msg.heatMax = state.heatOnDevice.max().item<double>();
      }

      ImPlot::PlotImage("im1", image1.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      ImPlot::PushColormap(cmap);

      ImPlot::PlotHeatmap("Correspondence volume slice",
                          (float *)state.heat.data_ptr(), state.heat.size(0),
                          state.heat.size(1), msg.heatMin, msg.heatMax,
                          nullptr);

      ImPlot::PopColormap();

      ImPlot::EndPlot();
    }

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImGui::SameLine();
    ImPlot::ColormapScale("ColormapScale", msg.heatMin, msg.heatMax,
                          ImVec2(cmapWidth, plotSize.y));
    ImPlot::PopColormap();

    ImGui::End();

    static_cast<Message &>(state) = msg;
  }

  return 0;
}
