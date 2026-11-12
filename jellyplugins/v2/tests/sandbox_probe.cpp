#include "sandbox.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

int main(int argc, char **argv) {
	if (argc != 3) return 64;
#ifdef _WIN32
	auto winsock = WSADATA();
	if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 70;
#endif
	auto mechanism = std::string();
	auto error = std::string();
	if (!JellyPlugins::Host::EnterSandbox(mechanism, error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 77;
	}
	const auto readable = std::fopen(argv[1], "rb");
	if (readable) std::fclose(readable);
	const auto writable = std::fopen(argv[1], "ab");
	if (writable) std::fclose(writable);
	auto address = sockaddr_in();
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(std::atoi(argv[2])));
	address.sin_addr.s_addr = htonl(0x7f000001);
	const auto connection = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
	auto networkDenied = true;
	if (connection != INVALID_SOCKET) {
		auto nonblocking = u_long(1);
		if (ioctlsocket(connection, FIONBIO, &nonblocking) != 0) return 70;
		const auto connected = connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
		if (connected == 0) networkDenied = false;
		else if (WSAGetLastError() == WSAEWOULDBLOCK) {
			auto writable = fd_set();
			auto failed = fd_set();
			FD_ZERO(&writable);
			FD_ZERO(&failed);
			FD_SET(connection, &writable);
			FD_SET(connection, &failed);
			auto timeout = timeval{ 2, 0 };
			const auto selected = select(0, nullptr, &writable, &failed, &timeout);
			if (selected == SOCKET_ERROR) return 70;
			if (selected > 0) {
				auto error = int(0);
				auto length = int(sizeof(error));
				if (getsockopt(connection, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) return 70;
				networkDenied = error != 0;
			}
		}
	}
	if (connection != INVALID_SOCKET) closesocket(connection);
#else
	const auto networkDenied = connection < 0
		|| connect(connection, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0;
	if (connection >= 0) close(connection);
#endif
	std::printf("{\"readDenied\":%s,\"writeDenied\":%s,\"networkDenied\":%s}\n",
		readable ? "false" : "true", writable ? "false" : "true", networkDenied ? "true" : "false");
	return !readable && !writable && networkDenied ? 0 : 1;
}
