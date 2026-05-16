# Changelog

All notable changes to Image Get Pixels will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-03-22

### Added
- **Perceptual Color Matching** - Implemented CIEDE2000 color difference formula for highly accurate color matching against a palette.
- **Smart Image Resizing** - Added `-R` / `--resize` switch to resize images before processing or outputting.
  - Resizing is performed *after* color transformations to preserve color intent while providing smooth interpolation.
- **Image Output Support** - Added `-O` / `--output` switch to save processed pixel data back into an image file.
  - Automatically detects output format from extension (PNG, BMP, TGA, JPG).
- **Extended Palette Support** - The `-C` / `--closest-list` switch now supports:
  - Hexadecimal values with `0x`, `0X`, or `#` prefixes.
  - Mixed RGB/RGBA and Hex values in the same semicolon-delimited list.
- **Improved Build System** - Updated CI/CD workflow to build and test the `dev` branch.

### Changed
- Refactored image processing pipeline to separate processing, resizing, and output stages.
- Updated help messages to reflect new features.

### Fixed
- Fixed missing field initializer warnings in `stb_image_write.h`.
- Fixed variable shadowing issues in color conversion logic.

## [1.0.0] - 2026-01-09

### Added
- **Initial release of Image Get Pixels** - A cross-platform command-line utility for extracting pixel data and resolution information from images.
- **Cross-platform support** - Built and distributed for Windows and Linux.
- **Command-line interface** - Simple and intuitive CLI for image querying operations.
- **Pixel Color Extraction** - Ability to retrieve colors in Hex, RGB, and BGR formats.
- **Resolution Querying** - Quickly retrieve image dimensions.
- **Formatting Options**: Opaque mode and custom prefixes.
- **Coordinate Mapping**: Optional format to associate colors with their `x,y` coordinates.

---

## [Unreleased]

### Planned - Considerations
- Batch processing for entire directories.
- Export functionality to JSON, CSV, and XML.
- Dominant color extraction and palette generation.
- Support for additional formats (GIF, HDR).
- Sub-region extraction (rectangles/circles).
- SIMD optimization for high-resolution images.
