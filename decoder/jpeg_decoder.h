#pragma once

#include "../image.h"
#include "bit_reader.h"
#include "huffman.h"

#include <array>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

class JpegDecoder {
public:
    explicit JpegDecoder(std::istream& in);

    Image Decode();

private:
    struct Component {
        int id = 0;
        int h = 1;
        int v = 1;
        int quant_id = 0;
        int dc_table = 0;
        int ac_table = 0;
        size_t blocks_wide = 0;
        size_t blocks_tall = 0;
        size_t scan_blocks_wide = 0;
        size_t scan_blocks_tall = 0;
        bool dc_started = false;
        int64_t dc_predictor = 0;
        std::vector<int32_t> coefficients;
    };

    void ParseStream();
    uint8_t ReadMarkerCode();
    void SkipSegment();
    void ReadCom();
    void ReadDqt();
    void ReadDht();
    void ReadSof(bool progressive);
    uint8_t ReadSosAndDecodeScan();

    uint8_t DecodeScan(const std::vector<size_t>& scan_components);
    void DecodeBlock(Component& comp, size_t block_row, size_t block_col, ScanBitReader& bits);
    void DecodeBaselineBlock(Component& comp, int32_t* block, ScanBitReader& bits);
    void DecodeDcFirst(Component& comp, int32_t* block, ScanBitReader& bits);
    void DecodeDcRefine(int32_t* block, ScanBitReader& bits);
    void DecodeAcFirst(Component& comp, int32_t* block, ScanBitReader& bits);
    void DecodeAcRefine(Component& comp, int32_t* block, ScanBitReader& bits);

    Image BuildImage();

    StreamReader reader_;

    std::array<std::optional<std::array<uint16_t, 64>>, 4> quant_tables_;
    std::array<HuffmanTree, 4> dc_trees_;
    std::array<HuffmanTree, 4> ac_trees_;
    std::array<bool, 4> dc_defined_ = {};
    std::array<bool, 4> ac_defined_ = {};

    bool have_frame_ = false;
    bool progressive_ = false;
    size_t width_ = 0;
    size_t height_ = 0;
    std::vector<Component> components_;
    size_t hmax_ = 1;
    size_t vmax_ = 1;
    size_t mcus_wide_ = 0;
    size_t mcus_tall_ = 0;

    size_t scan_count_ = 0;
    std::string comment_;

    int ss_ = 0;
    int se_ = 63;
    int ah_ = 0;
    int al_ = 0;
    uint32_t eobrun_ = 0;
};
