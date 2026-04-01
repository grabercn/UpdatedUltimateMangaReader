#!/bin/bash
# build.sh - Cross-compile UltimateMangaReader for Kobo Libra H2O
# Run from WSL or Linux. Uses Docker for a clean reproducible build.
#
# Usage:
#   ./build.sh kobo     - Build for Kobo (ARM, cross-compile via Docker)
#   ./build.sh desktop  - Build for Linux desktop (native)
#   ./build.sh clean    - Clean build artifacts

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
OUTPUT_DIR="$PROJECT_DIR/output"

mkdir -p "$OUTPUT_DIR"

build_kobo_docker() {
    echo "=== Building for Kobo Libra H2O (Docker) ==="
    echo "This will take a LONG time on first run (building toolchain + Qt)."
    echo "Subsequent builds will be much faster due to Docker layer caching."
    echo ""

    # Build the Docker image (cached after first run)
    docker build -t umr-kobo-builder -f "$PROJECT_DIR/Dockerfile.kobo" "$PROJECT_DIR"

    # Run the build, mounting source and output
    docker run --rm \
        -v "$PROJECT_DIR:/src:ro" \
        -v "$OUTPUT_DIR:/output" \
        umr-kobo-builder

    echo ""
    echo "=== Build complete! ==="
    echo "Output: $OUTPUT_DIR/UltimateMangaReader"
    echo ""
    echo "To install on Kobo Libra H2O:"
    echo "1. Connect your Kobo via USB"
    echo "2. Copy the binary to /mnt/onboard/.adds/UltimateMangaReader/"
    echo "3. Ensure KFMon is installed for launching"
}

build_desktop() {
    echo "=== Building for Linux desktop ==="

    # Check for Qt
    if ! command -v qmake &>/dev/null; then
        echo "ERROR: qmake not found. Install Qt 5.15+ development packages."
        echo "  Ubuntu/Debian: sudo apt install qt5-default libqt5svg5-dev libturbojpeg0-dev libpng-dev"
        echo "  Fedora: sudo dnf install qt5-qtbase-devel qt5-qtsvg-devel turbojpeg-devel libpng-devel"
        exit 1
    fi

    BUILD_DIR="$PROJECT_DIR/build-desktop"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    qmake "$PROJECT_DIR/UltimateMangaReader.pro" CONFIG+=desktop
    make -j"$(nproc)"

    echo ""
    echo "=== Build complete! ==="
    echo "Binary: $BUILD_DIR/UltimateMangaReader"
}

clean() {
    echo "Cleaning build artifacts..."
    rm -rf "$PROJECT_DIR/build-desktop"
    rm -rf "$PROJECT_DIR/build-windows"
    rm -rf "$OUTPUT_DIR"
    echo "Done."
}

case "${1:-kobo}" in
    kobo)
        build_kobo_docker
        ;;
    desktop)
        build_desktop
        ;;
    clean)
        clean
        ;;
    *)
        echo "Usage: $0 {kobo|desktop|clean}"
        echo ""
        echo "  kobo    - Cross-compile for Kobo Libra H2O via Docker"
        echo "  desktop - Build for Linux desktop"
        echo "  clean   - Remove build artifacts"
        exit 1
        ;;
esac
