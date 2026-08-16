#include "openwow/runtime/bootstrap/startup_trace.h"

#include <fstream>

namespace openwow::runtime::bootstrap {

std::string StartupTrace::SerializeTsv() const {
  std::string output;
  output.reserve(events_.size() * 32);
  for (std::size_t index = 0; index < events_.size(); ++index) {
    output += std::to_string(index);
    output += '\t';
    AppendEscaped(output, events_[index].phase);
    output += '\t';
    AppendEscaped(output, events_[index].label);
    output += '\n';
  }
  return output;
}

std::error_code StartupTrace::WriteTsvFile(
    const std::filesystem::path& path) const {
  std::error_code error;
  if (const auto directory = path.parent_path(); !directory.empty()) {
    std::filesystem::create_directories(directory, error);
    if (error) {
      return error;
    }
  }

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return std::make_error_code(std::io_errc::stream);
  }

  const std::string payload = SerializeTsv();
  stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  if (!stream.good()) {
    return std::make_error_code(std::io_errc::stream);
  }
  return {};
}

void StartupTrace::AppendEscaped(std::string& output,
                                 const std::string_view value) {
  for (const char character : value) {
    switch (character) {
      case '\\':
        output += "\\\\";
        break;
      case '\t':
        output += "\\t";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      default:
        output += character;
        break;
    }
  }
}

}
