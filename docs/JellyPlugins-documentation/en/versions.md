# Versions and compatibility

Package, API and IPC versions are 2. Old packages/API 1 are rejected. No settings/permission migration or compatibility runtime.

Windows/Linux/macOS launcher, host and packaging are connected in source. Native and visual validation remain required. CI: Windows x64, Linux x64, macOS arm64/x64. Other architectures and Store/Flatpak/AppImage are not validated by this matrix.

Contract changes invalidate previous full consent and rules. Package changes require enabling the new revision, clear permissions and keep one rollback snapshot. Manifest version is not a substitute for content SHA-256.
