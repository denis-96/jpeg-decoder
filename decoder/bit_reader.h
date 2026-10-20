#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>

class StreamReader {
public:
    explicit StreamReader(std::istream& in);

    uint8_t ReadByte();
    uint16_t ReadWord();
    void Skip(size_t count);

private:
    std::istream& in_;
};

class ScanBitReader {
public:
    explicit ScanBitReader(StreamReader& reader);

    int ReadBit();
    int ReadBits(int count);
    uint8_t FinishScan();

private:
    int NextDataByte();
    uint8_t ReadMarker();

    StreamReader& reader_;
    uint8_t current_ = 0;
    int bits_left_ = 0;
    int pending_marker_ = -1;
};
