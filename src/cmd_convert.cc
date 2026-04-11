#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "cli.h"
#include "defer.h"
#include "image.h"
#include "libraries/dr_wav.h"
#include "libraries/stb_image.h"
#include "libraries/stb_image_write.h"
#include "libraries/stb_vorbis.h"
#include "logging.h"
#include "platform.h"
#include "qoa.h"
#include "stringlib.h"
#include "units.h"

namespace G {
namespace {

enum class Format {
  kUnknown,
  kPng,
  kJpeg,
  kBmp,
  kGif,
  kTga,
  kQoi,
  kWav,
  kOgg,
  kQoa,
};

// Infer format from file extension.
Format FormatFromExtension(std::string_view ext) {
  if (ext == "png") return Format::kPng;
  if (ext == "jpg" || ext == "jpeg") return Format::kJpeg;
  if (ext == "bmp") return Format::kBmp;
  if (ext == "gif") return Format::kGif;
  if (ext == "tga") return Format::kTga;
  if (ext == "qoi") return Format::kQoi;
  if (ext == "wav") return Format::kWav;
  if (ext == "ogg") return Format::kOgg;
  if (ext == "qoa") return Format::kQoa;
  return Format::kUnknown;
}

bool IsImageFormat(Format f) {
  return f == Format::kPng || f == Format::kJpeg || f == Format::kBmp ||
         f == Format::kGif || f == Format::kTga || f == Format::kQoi;
}

bool IsAudioFormat(Format f) {
  return f == Format::kWav || f == Format::kOgg || f == Format::kQoa;
}

const char* DefaultOutputExtension(Format input) {
  if (IsImageFormat(input) && input != Format::kQoi) return "qoi";
  if (input == Format::kQoi) return "png";
  if (IsAudioFormat(input) && input != Format::kQoa) return "qoa";
  if (input == Format::kQoa) return "wav";
  return nullptr;
}

// Convert image data (any stb_image-supported format) to QOI.
bool ConvertImageToQoi(const uint8_t* data, size_t size, const char* out_path,
                       Allocator* allocator) {
  int w, h, channels;
  auto* pixels =
      stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels,
                            /*desired_channels=*/0);
  if (pixels == nullptr) {
    fprintf(stderr, "Error: failed to decode image: %s\n",
            stbi_failure_reason());
    return false;
  }
  DEFER([pixels] { stbi_image_free(pixels); });
  QoiDesc desc;
  desc.width = w;
  desc.height = h;
  desc.channels = channels;
  desc.colorspace = QoiColorspace::kLinear;
  int out_len;
  auto* encoded = QoiEncode(pixels, &desc, &out_len, allocator);
  if (encoded == nullptr) {
    fprintf(stderr, "Error: failed to encode QOI\n");
    return false;
  }
  return !WriteEntireFile(out_path, encoded, out_len).is_error();
}

// Convert QOI to PNG.
bool ConvertQoiToPng(const uint8_t* data, size_t size, const char* out_path,
                     Allocator* allocator) {
  QoiDesc desc;
  auto* pixels = QoiDecode(data, static_cast<int>(size), &desc,
                           /*channels=*/0, allocator);
  if (pixels == nullptr) {
    fprintf(stderr, "Error: failed to decode QOI\n");
    return false;
  }
  int ok = stbi_write_png(out_path, static_cast<int>(desc.width),
                          static_cast<int>(desc.height), desc.channels, pixels,
                          static_cast<int>(desc.width) * desc.channels);
  if (!ok) {
    fprintf(stderr, "Error: failed to write PNG '%s'\n", out_path);
    return false;
  }
  return true;
}

// Convert WAV to QOA.
bool ConvertWavToQoa(const uint8_t* data, size_t size, const char* out_path,
                     Allocator* allocator) {
  drwav wav;
  if (!drwav_init_memory(&wav, data, size, nullptr)) {
    fprintf(stderr, "Error: failed to decode WAV\n");
    return false;
  }
  DEFER([&wav] { drwav_uninit(&wav); });
  size_t total_frames = wav.totalPCMFrameCount;
  size_t total_samples = total_frames * wav.channels;
  auto* pcm = allocator->NewArray<int16_t>(total_samples);
  drwav_read_pcm_frames_s16(&wav, total_frames, pcm);
  QoaDesc desc;
  desc.channels = wav.channels;
  desc.samplerate = wav.sampleRate;
  desc.samples = static_cast<uint32_t>(total_frames);
  Slice<int16_t> samples(pcm, total_samples);
  FixedArray<uint8_t> encoded = QoaEncode(samples, &desc, allocator);
  if (encoded.empty()) {
    fprintf(stderr, "Error: failed to encode QOA\n");
    return false;
  }
  return !WriteEntireFile(out_path, encoded.data(), encoded.size()).is_error();
}

// Convert OGG Vorbis to QOA.
bool ConvertOggToQoa(const uint8_t* data, size_t size, const char* out_path,
                     Allocator* allocator) {
  int error;
  stb_vorbis* v =
      stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
  if (v == nullptr) {
    fprintf(stderr, "Error: failed to decode OGG (error %d)\n", error);
    return false;
  }
  DEFER([v] { stb_vorbis_close(v); });
  stb_vorbis_info info = stb_vorbis_get_info(v);
  unsigned int total_frames = stb_vorbis_stream_length_in_samples(v);
  if (total_frames == 0) {
    fprintf(stderr, "Error: empty OGG file\n");
    return false;
  }
  size_t total_samples = static_cast<size_t>(total_frames) * info.channels;
  auto* pcm = allocator->NewArray<int16_t>(total_samples);
  stb_vorbis_get_samples_short_interleaved(v, info.channels, pcm,
                                           static_cast<int>(total_samples));
  QoaDesc desc;
  desc.channels = static_cast<uint32_t>(info.channels);
  desc.samplerate = info.sample_rate;
  desc.samples = total_frames;
  Slice<int16_t> samples(pcm, total_samples);
  FixedArray<uint8_t> encoded = QoaEncode(samples, &desc, allocator);
  if (encoded.empty()) {
    fprintf(stderr, "Error: failed to encode QOA\n");
    return false;
  }
  return !WriteEntireFile(out_path, encoded.data(), encoded.size()).is_error();
}

// Convert QOA to WAV.
bool ConvertQoaToWav(const uint8_t* data, size_t size, const char* out_path,
                     Allocator* allocator) {
  QoaDesc desc;
  ByteSlice input(data, size);
  FixedArray<int16_t> samples = QoaDecode(input, &desc, allocator);
  if (samples.empty()) {
    fprintf(stderr, "Error: failed to decode QOA\n");
    return false;
  }
  drwav wav;
  drwav_data_format format;
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_PCM;
  format.channels = desc.channels;
  format.sampleRate = desc.samplerate;
  format.bitsPerSample = 16;
  if (!drwav_init_file_write(&wav, out_path, &format, nullptr)) {
    fprintf(stderr, "Error: could not open '%s' for writing\n", out_path);
    return false;
  }
  drwav_write_pcm_frames(&wav, desc.samples, samples.data());
  drwav_uninit(&wav);
  return true;
}

}  // namespace

int CmdConvert(Slice<const char*> args, Allocator* allocator) {
  const char* input_path = nullptr;
  const char* output_path = nullptr;
  const char* format_str = nullptr;

  for (size_t i = 1; i < args.size(); ++i) {
    std::string_view arg = args[i];
    if ((arg == "-o" || arg == "--output") && i + 1 < args.size()) {
      output_path = args[++i];
    } else if ((arg == "-f" || arg == "--format") && i + 1 < args.size()) {
      format_str = args[++i];
    } else if (arg[0] != '-') {
      input_path = args[i];
    }
  }

  if (input_path == nullptr) {
    fprintf(stderr, "Error: no input file specified.\n");
    fprintf(stderr, "Usage: game convert <input> [-o output] [-f format]\n");
    return 1;
  }

  if (!FileExists(input_path)) {
    fprintf(stderr, "Error: file not found: '%s'\n", input_path);
    return 1;
  }

  // Determine input format from extension.
  std::string_view input_ext = Extension(input_path);
  Format input_format = FormatFromExtension(input_ext);
  if (input_format == Format::kUnknown) {
    fprintf(stderr, "Error: unrecognized input format '.%.*s'\n",
            static_cast<int>(input_ext.size()), input_ext.data());
    return 1;
  }

  // Determine output format.
  Format output_format = Format::kUnknown;
  if (format_str != nullptr) {
    output_format = FormatFromExtension(format_str);
  } else if (output_path != nullptr) {
    output_format = FormatFromExtension(Extension(output_path));
  }
  if (output_format == Format::kUnknown) {
    const char* default_ext = DefaultOutputExtension(input_format);
    if (default_ext == nullptr) {
      fprintf(stderr, "Error: cannot infer output format. Use -f or -o.\n");
      return 1;
    }
    output_format = FormatFromExtension(default_ext);
  }

  // Validate conversion is supported.
  if (IsImageFormat(input_format) && !IsImageFormat(output_format)) {
    fprintf(stderr, "Error: cannot convert image to audio format.\n");
    return 1;
  }
  if (IsAudioFormat(input_format) && !IsAudioFormat(output_format)) {
    fprintf(stderr, "Error: cannot convert audio to image format.\n");
    return 1;
  }

  // Build output path if not given.
  FixedStringBuffer<1024> out_buf;
  if (output_path == nullptr) {
    const char* default_ext = DefaultOutputExtension(input_format);
    out_buf.Append(WithoutExt(input_path), ".", default_ext);
    output_path = out_buf.str();
  }

  // Read input.
  ArenaAllocator arena(allocator, Megabytes(64));
  uint8_t* input_data = nullptr;
  auto read_result = ReadEntireFile(input_path, &input_data, &arena);
  if (read_result.is_error()) {
    fprintf(stderr, "Error: could not read '%s'\n", input_path);
    return 1;
  }
  size_t input_size = read_result.value();

  bool ok = false;
  if (input_format == Format::kQoi && output_format == Format::kPng) {
    ok = ConvertQoiToPng(input_data, input_size, output_path, &arena);
  } else if (IsImageFormat(input_format) && output_format == Format::kQoi) {
    ok = ConvertImageToQoi(input_data, input_size, output_path, &arena);
  } else if (input_format == Format::kQoa && output_format == Format::kWav) {
    ok = ConvertQoaToWav(input_data, input_size, output_path, &arena);
  } else if (input_format == Format::kWav && output_format == Format::kQoa) {
    ok = ConvertWavToQoa(input_data, input_size, output_path, &arena);
  } else if (input_format == Format::kOgg && output_format == Format::kQoa) {
    ok = ConvertOggToQoa(input_data, input_size, output_path, &arena);
  } else {
    fprintf(stderr, "Error: unsupported conversion.\n");
    return 1;
  }

  if (!ok) return 1;
  printf("%s -> %s\n", input_path, output_path);
  return 0;
}

}  // namespace G
