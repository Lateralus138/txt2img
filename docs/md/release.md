# Release Information

## Current Release: v1.1.0

### Release Date
March 22, 2026

### Overview
This release introduces significant new features, including perceptually accurate color matching using the CIEDE2000 formula, smart image resizing, and direct image output support.

### Downloads
- **Windows**: `imggetpixels-windows.exe` - Native Windows x64 executable
- **Linux**: `imggetpixels-linux` - Native Linux x64 binary

### System Requirements
- **Windows**: Windows 10 or later (x64)
- **Linux**: Any modern distribution (x64) with standard C++ libraries

### New in v1.1.0
- **CIEDE2000 Color Matching**: Highly accurate color matching against palettes.
- **Smart Resizing**: Resize images on-the-fly with smooth interpolation.
- **Direct Image Output**: Save your results directly to PNG, BMP, TGA, or JPG.
- **Enhanced CLI**: New options for mixed palette formats and improved help documentation.

### Quick Start (New Features)
```bash
# Resize and save to a new file
imggetpixels -R 64x64 -O thumb.png input.jpg

# Match pixels against a palette and save
imggetpixels -C "#000000;#FFFFFF;#FF0000" -O mapped.png input.png

# Combine resizing and palette matching
imggetpixels -R 32x32 -C "0x000000;0x00FF00" -O sprite.png input.png
```

## Changelog

See [Changelog](./docs/md/reference/changelog.md)

---

## Previous Releases

- **v1.0.0** (January 9, 2026): Initial stable release.

## Support

For issues, feature requests, or questions:
- [GitHub Issues](https://github.com/Lateralus138/imggetpixels/issues)
- [GitHub Discussions](https://github.com/Lateralus138/imggetpixels/discussions)

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](../../LICENSE) for details.
