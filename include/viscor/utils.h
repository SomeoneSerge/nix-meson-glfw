#ifndef _VISCOR_UTILS_DIR_H
#define _VISCOR_UTILS_DIR_H

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#include "viscor/raii.h"

namespace VisCor {

namespace fs = std::filesystem;

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
