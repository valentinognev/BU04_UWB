#!/usr/bin/env bash
# Script to update Antigravity to Version 2.0 (2.0.11)

set -e

DOWNLOADS_DIR="/home/valentin/Downloads"
TARBALL="${DOWNLOADS_DIR}/Antigravity (1).tar.gz"
FALLBACK_TARBALL="${DOWNLOADS_DIR}/Antigravity.tar.gz"
INSTALL_DIR="/usr/share/antigravity"
OLD_INSTALL_DIR="/usr/share/antigravity.old"
BIN_LINK="/usr/bin/antigravity"

echo "=== Antigravity IDE Update Script ==="

# Check which tarball to use
if [ -f "$TARBALL" ]; then
    echo "Using latest installer: $TARBALL"
elif [ -f "$FALLBACK_TARBALL" ]; then
    TARBALL="$FALLBACK_TARBALL"
    echo "Using installer: $TARBALL"
else
    echo "Error: Could not find Antigravity tarball in ${DOWNLOADS_DIR}"
    exit 1
fi

# Create a secure temporary directory for extraction
TMP_DIR=$(mktemp -d -p "/home/valentin/RL/UWB" tmp_antigravity_upgrade_XXXXXX)
echo "Extracting files to temporary directory: ${TMP_DIR}..."

# Extract tarball
tar -xf "$TARBALL" -C "$TMP_DIR"

EXTRACTED_DIR="${TMP_DIR}/Antigravity-x64"
if [ ! -d "$EXTRACTED_DIR" ]; then
    echo "Error: Extracted directory structure not as expected."
    rm -rf "$TMP_DIR"
    exit 1
fi

echo "Requesting superuser privileges to install..."
# Remove old backup if it exists
if [ -d "$OLD_INSTALL_DIR" ]; then
    sudo rm -rf "$OLD_INSTALL_DIR"
fi

# Backup current installation
if [ -d "$INSTALL_DIR" ]; then
    echo "Backing up current version to ${OLD_INSTALL_DIR}..."
    sudo mv "$INSTALL_DIR" "$OLD_INSTALL_DIR"
fi

# Install new version
echo "Installing new version to ${INSTALL_DIR}..."
sudo cp -r "$EXTRACTED_DIR" "$INSTALL_DIR"

# Set correct owner and SUID permissions for chrome-sandbox
echo "Setting permissions for chrome-sandbox..."
sudo chown root:root "${INSTALL_DIR}/chrome-sandbox"
sudo chmod 4755 "${INSTALL_DIR}/chrome-sandbox"

# Create / update symlink
echo "Updating symlink at ${BIN_LINK}..."
sudo ln -sf "${INSTALL_DIR}/antigravity" "$BIN_LINK"

# Clean up
echo "Cleaning up temporary files..."
rm -rf "$TMP_DIR"
rm -rf "/home/valentin/RL/UWB/temp_antigravity_1"
rm -rf "/home/valentin/RL/UWB/temp_antigravity_2"

echo "=== Update Completed Successfully! ==="
echo "Please restart Antigravity to apply the changes."
