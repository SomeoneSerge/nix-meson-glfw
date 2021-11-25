#ifndef _VISCOR_UTILS_H
#define _VISCOR_UTILS_H

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <torch/torch.h>

namespace VisCor {

namespace fs = std::filesystem;

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

DescriptorField loadExrField(const fs::path &path, const torch::Device &device);

struct Uint8Image {
  int xres;
  int yres;
  int channels;
  std::unique_ptr<unsigned char[]> data;

  Uint8Image(int xres, int yres, int channels,
             std::unique_ptr<unsigned char[]> &&data)
      : xres(xres), yres(yres), channels(channels), data(std::move(data)) {}

  Uint8Image(Uint8Image &&other)
      : xres(other.xres), yres(other.yres), data(std::move(other.data)) {}
  Uint8Image(const Uint8Image &other) = delete;
};

Uint8Image oiioLoadImage(const std::string &filename);

struct LayoutJson {
  LayoutJson(const fs::path &path) {
    using json = nlohmann::json;
    std::ifstream fLayout(path);
    json j;
    fLayout >> j;

    shape = (std::vector<int>)j.at("shape");
    dtype = j.at("dtype");
  }
  std::vector<int> shape;
  std::string dtype;
};
}; // namespace VisCor

#endif
