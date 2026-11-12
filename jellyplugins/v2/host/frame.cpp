#include "frame.h"

#include "bootstrap.generated.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace JellyPlugins::Host {
namespace {

bool ReadAll(char *data, size_t size) {
	while (size) {
		const auto count = std::fread(data, 1, size, stdin);
		if (!count) {
			if (std::ferror(stdin) && errno == EINTR) {
				std::clearerr(stdin);
				continue;
			}
			return false;
		}
		data += count;
		size -= count;
	}
	return true;
}

bool WriteAll(const char *data, size_t size) {
	while (size) {
		const auto count = std::fwrite(data, 1, size, stdout);
		if (!count) {
			if (std::ferror(stdout) && errno == EINTR) {
				std::clearerr(stdout);
				continue;
			}
			return false;
		}
		data += count;
		size -= count;
	}
	return true;
}

bool ValidUtf8(std::string_view text) {
	for (auto offset = size_t(0); offset != text.size();) {
		const auto first = static_cast<unsigned char>(text[offset]);
		if (first < 0x80) {
			++offset;
			continue;
		}
		const auto length = (first >= 0xc2 && first <= 0xdf) ? 2
			: (first >= 0xe0 && first <= 0xef) ? 3
			: (first >= 0xf0 && first <= 0xf4) ? 4 : 0;
		if (!length || offset + length > text.size()) {
			return false;
		}
		for (auto index = 1; index != length; ++index) {
			if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80) {
				return false;
			}
		}
		const auto second = static_cast<unsigned char>(text[offset + 1]);
		if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)
			|| (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) {
			return false;
		}
		offset += length;
	}
	return true;
}

} // namespace

bool PrepareTransport() {
#ifdef _WIN32
	if (_setmode(_fileno(stdin), _O_BINARY) == -1
		|| _setmode(_fileno(stdout), _O_BINARY) == -1) {
		return false;
	}
#endif
	return std::setvbuf(stdin, nullptr, _IONBF, 0) == 0
		&& std::setvbuf(stdout, nullptr, _IONBF, 0) == 0;
}

ReadResult ReadFrame(std::string &frame) {
	auto header = std::array<unsigned char, 4>();
	const auto first = std::fgetc(stdin);
	if (first == EOF) {
		return std::feof(stdin) ? ReadResult::End : ReadResult::Invalid;
	}
	header[0] = static_cast<unsigned char>(first);
	if (!ReadAll(reinterpret_cast<char*>(header.data() + 1), 3)) {
		return ReadResult::Invalid;
	}
	const auto size = (uint32_t(header[0]) << 24)
		| (uint32_t(header[1]) << 16)
		| (uint32_t(header[2]) << 8)
		| uint32_t(header[3]);
	if (!size || size > kFrameBytes) {
		return ReadResult::Invalid;
	}
	frame.resize(size);
	return ReadAll(frame.data(), size) && ValidUtf8(frame)
		? ReadResult::Frame : ReadResult::Invalid;
}

bool WriteFrame(const std::string &frame) {
	if (frame.empty() || frame.size() > kFrameBytes) {
		return false;
	}
	const auto size = uint32_t(frame.size());
	const auto header = std::array<unsigned char, 4>{
		static_cast<unsigned char>(size >> 24),
		static_cast<unsigned char>(size >> 16),
		static_cast<unsigned char>(size >> 8),
		static_cast<unsigned char>(size),
	};
	return WriteAll(reinterpret_cast<const char*>(header.data()), header.size())
		&& WriteAll(frame.data(), frame.size())
		&& std::fflush(stdout) == 0;
}

} // namespace JellyPlugins::Host
