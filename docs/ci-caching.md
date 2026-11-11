# Release workflow caches

`.github/workflows/release.yml` separates dependency caches from compiler
caches. Finished dependency builds are saved before application compilation,
so a later compiler or packaging error does not discard them.

## Dependencies

- Linux saves the exported Docker layer cache immediately after building the
  image. Cache mounts restored by cache-dance are auxiliary; their post-build
  extraction can still be skipped on a failed job. The saved layer cache
  contains the completed dependency builds independently of those mounts.
- macOS saves RNNoise, WebRTC, TDE2E and the combined Rust libraries after each
  successful build. Installed prefixes are restored only on exact cache-key
  matches, avoiding incompatible leftover files from older installations.
- Windows retains separate tools, libraries and Qt caches, including the
  existing partial preparation caches.

RNNoise and WebRTC on macOS are fetched at the revisions declared in the job
environment, including WebRTC's recorded submodule revisions. Updating them
requires changing those values. Homebrew formula versions still come from
the runner's available Homebrew state; they are not a fully pinned toolchain.
The workflow disables automatic updates, omits `brew update`, and includes
installed tool/library versions, Xcode, Clang, architecture and the workspace
path in the macOS dependency fingerprint.

## Compiler results

All three platforms use ccache with a 2 GiB limit per platform. Windows uses
ccache 4.13.6 with a checked archive SHA-256 and Ninja compiler launchers.
macOS uses C, C++, Objective-C and Objective-C++ launchers. Its Clang flags
disable timestamps embedded in precompiled headers.

`pch_defines,time_macros` enables ccache's precompiled-header support. This is
ccache's documented relaxation of PCH macro/time checks; compiler flags,
source inputs and header dependencies still participate in cache validation.
MSVC PCH reuse was checked locally with a small standalone fixture, not a
native application build. The fixture used MSVC 14.50; Actions uses 14.44.

Compiler cache keys have a compatible environment prefix followed by the
commit, run ID and attempt. Prefix restoration reuses the latest compatible
cache, while unique save keys allow it to grow across retries. ccache also
checks compiler identity and compilation inputs; restoring a cache does not
restore a CMake build tree or reuse generated source files as build outputs.

Compiler caches and statistics run after either successful or failed
application compilation, but not after cancellation or a skipped build.
Packaging and artifact upload remain gated on build success.

## Checking effectiveness

Compare `compiler cache statistics` and compile-step duration across two
successive runs with the same toolchain and a small source change. The first
run populates the caches; large header changes can invalidate many entries.
Do not infer an application-wide speedup from the standalone fixture.

GitHub can evict caches under repository storage limits. Docker layers, Qt
and other dependency caches share that storage with compiler results.
Repeated misses should be investigated through cache-restore logs and ccache
statistics before increasing cache size.

To invalidate compiler caches, bump the platform's `ccache-vN` key prefix.
For macOS dependency preparation changes, bump `MANUAL_CACHING` as well.
Use a cold compiler cache when verifying changes that depend on time macros
or unusual PCH macro behavior.
