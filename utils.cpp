#include <OpenImageIO/imageio.h>
#include <regex>
#include <torch/torch.h>

#include "viscor/utils.h"

using namespace VisCor;

Uint8Image VisCor::oiioLoadImage(const std::string &filename) {
  using namespace OIIO;

  auto in = ImageInput::open(filename);
  if (!in)
    throw std::runtime_error("Couldn't load the image");

  const ImageSpec &spec = in->spec();
  int xres = spec.width;
  int yres = spec.height;
  int channels = spec.nchannels;
  std::unique_ptr<unsigned char[]> data =
      std::make_unique<unsigned char[]>(xres * yres * channels);

  if (!data)
    throw std::runtime_error("Couldn't allocate memory for image data");

  in->read_image(TypeDesc::UINT8, data.get());
  in->close(); /* eh... why not raii, I now have to wrap it in try-catch and I
                  don't want to */

  return Uint8Image(xres, yres, channels, std::move(data));
}

// FIXME: rm shitcode
DescriptorField VisCor::loadExrField(const fs::path &path,
                                     const torch::Device &device) {
  using namespace OIIO;
  std::unique_ptr<ImageInput> in = ImageInput::open(path);
  const ImageSpec &spec = in->spec();

  std::vector<std::string> descChannels;
  for (const auto &c : spec.channelnames) {
    // if (!c.starts_with("superglue."))
    //   continue;

    //   FIXME:
    if (c.find(".") == std::string::npos)
      continue;
    descChannels.push_back(c);
  }

  const auto nChannels = descChannels.size();
  const auto shape = std::make_tuple(spec.height, spec.width, nChannels);

  if (nChannels != 256) {
    std::cerr << "Expected 256 channels, got " << std::get<2>(shape) << "x"
              << std::get<0>(shape) << "x" << std::get<1>(shape) << std::endl;
  }
  if (nChannels < 1) {
    throw std::runtime_error("Input has 0 channels");
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
