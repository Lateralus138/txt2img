# Release Information

## Current Release: v1.0.0

### Release Date
May 17, 2026

### Overview
This is the initial release of Text 2 Image, a lightweight CLI tool to render text (including ANSI-styled text) into high-quality images with word wrapping and multi-page support.

### Downloads
- **Windows**: `txt2img-windows.exe` - Native Windows x64 executable
- **Linux**: `txt2img-linux` - Native Linux x64 binary
- **macOS**: `txt2img-macos` - Native macOS x64 binary

### System Requirements
- **Windows**: Windows 10 or later (x64)
- **Linux**: Any modern distribution (x64) with FreeType
- **macOS**: macOS 10.15 or later (x64/ARM)

### Features
- **ANSI Styling**: Preserve terminal colors and styles in your images.
- **Smart Wrapping**: Automatically wrap long lines of text.
- **Multi-paging**: Render long documents into sequential image pages.
- **Highly Configurable**: Adjust colors, fonts, padding, and more.

### Quick Start
```bash
# Render a file with wrapping at 800px width
txt2img -W 800 input.txt -n output.png

# Render styled text from stdin
echo -e "\e[31mRed Text\e[0m" | txt2img -W 400 -n styled.png
```

## Changelog

See [Changelog](./docs/md/reference/changelog.md)

---

## License

This project is dual-licensed under AGPLv3 and a Commercial License. See [LICENSE](../../LICENSE) and [COMMERCIAL_LICENSE.md](../../COMMERCIAL_LICENSE.md) for details.
