#include "frame.h"
#include "sandbox.h"

#include <string>

int main() {
	if (!JellyPlugins::Host::PrepareTransport()) return 73;
	auto mechanism = std::string();
	auto error = std::string();
	if (!JellyPlugins::Host::EnterSandbox(mechanism, error)) return 77;
	if (!JellyPlugins::Host::WriteFrame("{\"protocolVersion\":2,\"type\":\"hello\",\"apiVersion\":2,\"packageVersion\":2,\"sandbox\":\"" + mechanism + "\"}")) return 73;
	auto frame = std::string();
	while (JellyPlugins::Host::ReadFrame(frame) == JellyPlugins::Host::ReadResult::Frame) {}
	return 0;
}
