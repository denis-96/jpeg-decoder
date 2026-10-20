# JPEG Decoder

An educational JPEG decoder written in modern C++20. The project supports
baseline sequential JPEG and progressive JPEG images, including grayscale and
YCbCr images with common chroma subsampling modes.

The decoder is implemented from scratch for learning purposes. It does not
delegate JPEG parsing or entropy decoding to `libjpeg`; `libjpeg` is used only
by the test suite as a reference implementation.

## Features

- Baseline sequential JPEG (SOF0).
- Progressive JPEG (SOF2), including successive approximation.
- Huffman decoding in a standalone `HuffmanTree` class.
- Inverse DCT using FFTW, as required by the original assignment.
- Validation of JPEG markers, segment lengths, dimensions, tables and scan
  parameters.
- Explicit exceptions for malformed or unsupported input instead of process
  termination.
- COM marker extraction through `Image::GetComment()`.

## Requirements

- CMake 3.20 or newer.
- A C++20 compiler.
- FFTW3 development files.
- libjpeg development files.
- libpng development files.
- Catch2 v3 (only required for tests).

On Debian or Ubuntu:

```sh
sudo apt install catch2 libfftw3-dev libjpeg-dev libpng-dev
```

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If Catch2 v3 is not installed, CMake still builds the decoder library and
prints a warning explaining why the tests were skipped.

## Public API

The minimal API is declared in `decoder.h`:

```cpp
#include "decoder.h"

Image image = Decode("photo.jpg");
```

`Image` stores 8-bit RGB pixels and an optional JPEG comment. The decoder
throws `std::runtime_error` or `std::invalid_argument` when the input cannot
be decoded safely.

## Project structure

```text
decoder/       JPEG parser, bit reader, Huffman decoder and inverse DCT
tests/         baseline/progressive tests and reference image fixtures
decoder.h      public file-based Decode() entry point
image.h        RGB image value type
```

## Scope and limitations

This is a decoder project, not a general-purpose image library. It currently
accepts 8-bit grayscale and three-component JPEG images and intentionally
rejects unsupported JPEG modes and malformed files. The fixture images are
included to make the test suite reproducible and to document the supported
formats.
