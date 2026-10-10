/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ayu/plugins/plugin_worker.h"
#include "core/launcher.h"

#include <cstring>

extern "C" {
#include <openssl/crypto.h>
} // extern "C"

int main(int argc, char *argv[]) {
	if (argc == 2 && std::strcmp(argv[1], "--jelly-plugin-worker") == 0) {
		return JellyPlugins::RunWorker(argc, argv);
	}
	// OpenSSL's own atexit handler runs OPENSSL_cleanup() on the main thread
	// and frees the library globals while detached background tasks can still
	// be inside OpenSSL, so an ordinary quit can fault on a worker thread.
	// Suppressing that registration leaves the library state alive until the
	// process exits, which is the trade we want: the OS reclaims it anyway.
	OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, nullptr);

	const auto launcher = Core::Launcher::Create(argc, argv);
	return launcher ? launcher->exec() : 1;
}
