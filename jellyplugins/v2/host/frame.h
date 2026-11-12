#pragma once

#include <string>

namespace JellyPlugins::Host {

enum class ReadResult { Frame, End, Invalid };

[[nodiscard]] bool PrepareTransport();
[[nodiscard]] ReadResult ReadFrame(std::string &frame);
[[nodiscard]] bool WriteFrame(const std::string &frame);

} // namespace JellyPlugins::Host
