#include "bit_reader.h"

#include <stdexcept>

StreamReader::StreamReader(std::istream& in) : in_(in) {
}

uint8_t StreamReader::ReadByte() {
    int c = in_.get();
    if (c == std::char_traits<char>::eof()) {
        throw std::runtime_error("unexpected end of file");
    }
    return static_cast<uint8_t>(c);
}

uint16_t StreamReader::ReadWord() {
    uint16_t high = ReadByte();
    return static_cast<uint16_t>((high << 8) | ReadByte());
}

void StreamReader::Skip(size_t count) {
    in_.ignore(static_cast<std::streamsize>(count));
    if (static_cast<size_t>(in_.gcount()) != count) {
        throw std::runtime_error("unexpected end of file");
    }
}

ScanBitReader::ScanBitReader(StreamReader& reader) : reader_(reader) {
}

int ScanBitReader::NextDataByte() {
    if (pending_marker_ != -1) {
        return -1;
    }
    uint8_t byte = reader_.ReadByte();
    if (byte != 0xFF) {
        return byte;
    }
    uint8_t next = reader_.ReadByte();
    while (next == 0xFF) {
        next = reader_.ReadByte();
    }
    if (next == 0x00) {
        return 0xFF;
    }
    pending_marker_ = next;
    return -1;
}

int ScanBitReader::ReadBit() {
    if (bits_left_ == 0) {
        int byte = NextDataByte();
        if (byte < 0) {
            throw std::runtime_error("data ended unexpectedly");
        }
        current_ = static_cast<uint8_t>(byte);
        bits_left_ = 8;
    }
    --bits_left_;
    return (current_ >> bits_left_) & 1;
}

int ScanBitReader::ReadBits(int count) {
    int result = 0;
    for (int i = 0; i < count; ++i) {
        result = (result << 1) | ReadBit();
    }
    return result;
}

uint8_t ScanBitReader::ReadMarker() {
    bits_left_ = 0;
    if (pending_marker_ != -1) {
        uint8_t marker = static_cast<uint8_t>(pending_marker_);
        pending_marker_ = -1;
        return marker;
    }
    if (reader_.ReadByte() != 0xFF) {
        throw std::runtime_error("marker expected in scan");
    }
    uint8_t code = reader_.ReadByte();
    while (code == 0xFF) {
        code = reader_.ReadByte();
    }
    if (code == 0x00) {
        throw std::runtime_error("marker expected in scan");
    }
    return code;
}

uint8_t ScanBitReader::FinishScan() {
    return ReadMarker();
}
