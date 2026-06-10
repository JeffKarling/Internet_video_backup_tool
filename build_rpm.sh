#!/bin/bash
set -e

# Get the absolute path of the workspace directory
WORKSPACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${WORKSPACE_DIR}/rpmbuild"

echo "Setting up local rpmbuild directory structure under ${BUILD_DIR}..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}/BUILD" "${BUILD_DIR}/RPMS" "${BUILD_DIR}/SOURCES" "${BUILD_DIR}/SPECS" "${BUILD_DIR}/SRPMS"

echo "Downloading the latest compiled yt-dlp_linux binary from GitHub..."
curl -fsSL -o "${BUILD_DIR}/SOURCES/yt-dlp" "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux"
chmod +x "${BUILD_DIR}/SOURCES/yt-dlp"

echo "Copying source files to ${BUILD_DIR}/SOURCES..."
cp "${WORKSPACE_DIR}/systemd/yt-dlp@.service" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp@.path" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp@.timer" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp-vpn@.service" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp-user-mount@.service" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp.target" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/user-mount-hook.conf" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/docs/README.txt" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/docs/design.txt" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/docs/install.txt" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/templates/yt-dlp-template.conf" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/LICENSE" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/vb_generator/vpn-helper.sh" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp-update.service" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/yt-dlp-update.timer" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/vb_generator/vb_generator.cpp" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/systemd/vb-generator.service" "${BUILD_DIR}/SOURCES/"
cp "${WORKSPACE_DIR}/templates/profiles-template.json" "${BUILD_DIR}/SOURCES/"



echo "Copying spec file to ${BUILD_DIR}/SPECS..."
cp "${WORKSPACE_DIR}/yt-dlp-vpn-automation.spec" "${BUILD_DIR}/SPECS/"

echo "Building RPM package..."
rpmbuild --define "_topdir ${BUILD_DIR}" --define "__brp_add_determinism /usr/bin/true" -bb "${BUILD_DIR}/SPECS/yt-dlp-vpn-automation.spec"

echo "------------------------------------------------------------"
echo "Build complete! Generated RPM package:"
find "${BUILD_DIR}/RPMS" -name "*.rpm"
echo "------------------------------------------------------------"
