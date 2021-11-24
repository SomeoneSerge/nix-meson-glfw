#include <OpenImageIO/imageio.h>
#include <clipp.h>
#include <iostream>

int main(int argc, char **argv) {
  using namespace clipp;
  using namespace OIIO;

  std::string path, outPath;

  enum class Mode { Shape, LsChannels, Help, Export };
  Mode mode(Mode::Shape);

  {
    auto inPath = value("Path to the descriptors.exr", path);
    auto cli = (((command("shape").set(mode, Mode::Shape), inPath) |
                 (command("ls-channels").set(mode, Mode::LsChannels), inPath) |
                 (command("export").set(mode, Mode::Export),
                  option("-o", "--output") & value("path", outPath), inPath)) |
                command("--help").set(mode, Mode::Help));

    if (!parse(argc, argv, cli) || mode == Mode::Help) {
      std::cerr << "Couldn't parse the command line arguments" << std::endl;
      std::cerr << make_man_page(cli, argv[0]);
      std::exit(1);
    } else if (mode == Mode::Help) {
      std::cerr << make_man_page(cli, argv[0]);
      std::exit(0);
    }
  }

  std::unique_ptr<ImageInput> in = ImageInput::open(path);
  if (!in) {
    std::cerr << "Couldn't load " << path << std::endl;
    return 1;
  }

  const ImageSpec &spec = in->spec();

  // TODO: check existence
  const int iR = spec.channelindex("R");
  const int iG = spec.channelindex("G");
  const int iB = spec.channelindex("B");

  if (std::make_tuple(iR, iG, iB) != std::make_tuple(0, 1, 2)) {
    std::cerr << "The first channels must be R, G, B. Instead we got R, G, B "
                 "at indices "
              << iR << ", " << iG << ", " << iB << std::endl;
    std::exit(1);
  }

  switch (mode) {
  case Mode::Shape: {
    int xres = spec.width;
    int yres = spec.height;
    int channels = spec.nchannels;

    std::cout << "width: " << xres << std::endl;
    std::cout << "height: " << yres << std::endl;
    std::cout << "channels: " << channels << std::endl;
    break;
  }
  case Mode::LsChannels: {
    for (auto c : spec.channelnames) {
      std::cout << c << std::endl;
    }
    break;
  }
  case Mode::Export: {
    // const auto inDtype = spec.channelformat(iR);
    const TypeDesc outDtype = TypeDesc::UINT8;
    std::vector<unsigned char> data(spec.width * spec.height * 3);
    std::cerr << "Reading the image" << std::endl;
    in->read_image(iR, iR + 3, outDtype, data.data());

    std::cerr << "Creating the output image at " << outPath << std::endl;
    std::unique_ptr<ImageOutput> out = ImageOutput::create(outPath);

    if (!out) {
      std::cerr << "Couldn't create " << outPath << std::endl;
      std::exit(1);
    }

    ImageSpec outSpec(spec.width, spec.height, 3, outDtype);

    std::cerr << "Opening the output image" << std::endl;
    out->open(outPath, outSpec);

    std::cerr << "Writing the output image" << std::endl;
    out->write_image(outDtype, data.data());

    break;
  }
  case Mode::Help: {
    std::cerr << "Mode::help should have already been handled" << std::endl;
    std::exit(1);
  }
  }

  return 0;
}
