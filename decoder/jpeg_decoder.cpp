#include "jpeg_decoder.h"

#include "fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr uint8_t kSof0 = 0xC0;
constexpr uint8_t kSof2 = 0xC2;
constexpr uint8_t kDht = 0xC4;
constexpr uint8_t kSoi = 0xD8;
constexpr uint8_t kEoi = 0xD9;
constexpr uint8_t kSos = 0xDA;
constexpr uint8_t kDqt = 0xDB;
constexpr uint8_t kApp0 = 0xE0;
constexpr uint8_t kApp15 = 0xEF;
constexpr uint8_t kCom = 0xFE;

constexpr uint64_t kMaxPixels = uint64_t{1} << 26;
constexpr int kZigZag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

// знаковое восстановление значения по размеру
int RecoverValue(int value, int size) {
    return value < (1 << (size - 1)) ? value - (1 << size) + 1 : value;
}

// спуск по дереву Хаффмана
int DecodeSymbol(HuffmanTree& tree, ScanBitReader& bits) {
    int value = 0;
    while (!tree.Move(bits.ReadBit() != 0, value)) {
    }
    return value;
}

uint8_t Clamp8(double value) {
    if (value <= 0) {
        return 0;
    }
    if (value >= 255) {
        return 255;
    }
    return static_cast<uint8_t>(std::lround(value));
}

}  // namespace

JpegDecoder::JpegDecoder(std::istream& in) : reader_(in) {
}

Image JpegDecoder::Decode() {
    ParseStream();
    return BuildImage();
}

void JpegDecoder::ParseStream() {
    if (reader_.ReadByte() != 0xFF || reader_.ReadByte() != kSoi) {
        throw std::runtime_error("missing SOI");
    }
    // маркер, подсмотренный внутри ReadSosAndDecodeScan
    std::optional<uint8_t> pending;
    while (true) {
        uint8_t marker;
        if (pending) {
            marker = *pending;
            pending.reset();
        } else {
            marker = ReadMarkerCode();
        }
        if (marker == kEoi) {
            return;
        } else if (marker >= kApp0 && marker <= kApp15) {
            SkipSegment();
        } else if (marker == kCom) {
            ReadCom();
        } else if (marker == kDqt) {
            ReadDqt();
        } else if (marker == kDht) {
            ReadDht();
        } else if (marker == kSof0 || marker == kSof2) {
            ReadSof(marker == kSof2);
        } else if (marker == kSos) {
            pending = ReadSosAndDecodeScan();
        } else {
            throw std::runtime_error("unsupported or unexpected marker");
        }
    }
}

uint8_t JpegDecoder::ReadMarkerCode() {
    if (reader_.ReadByte() != 0xFF) {
        throw std::runtime_error("marker expected");
    }
    uint8_t code = reader_.ReadByte();
    while (code == 0xFF) {  // пропускаем байты 0xFF
        code = reader_.ReadByte();
    }
    if (code == 0x00) {
        throw std::runtime_error("invalid marker");
    }
    return code;
}

void JpegDecoder::SkipSegment() {
    uint16_t length = reader_.ReadWord();
    if (length < 2) {
        throw std::runtime_error("invalid segment length");
    }
    reader_.Skip(length - 2u);
}

void JpegDecoder::ReadCom() {
    uint16_t length = reader_.ReadWord();
    if (length < 2) {
        throw std::runtime_error("invalid COM length");
    }
    size_t count = length - 2u;
    comment_.reserve(comment_.size() + count);
    for (size_t i = 0; i < count; ++i) {
        comment_.push_back(static_cast<char>(reader_.ReadByte()));
    }
}

void JpegDecoder::ReadDqt() {
    uint16_t length = reader_.ReadWord();
    if (length < 2) {
        throw std::runtime_error("invalid DQT length");
    }
    size_t remaining = length - 2u;  // сегмент может содержать несколько таблиц подряд
    if (remaining == 0) {
        throw std::runtime_error("empty DQT");
    }
    while (remaining > 0) {
        uint8_t first_byte = reader_.ReadByte();
        --remaining;
        int precision = first_byte >> 4;
        int id = first_byte & 0x0F;
        if (precision > 1 || id > 3) {
            throw std::runtime_error("invalid quantization table spec");
        }
        size_t need = 64u * (precision + 1);  // precision: 0 - 1 байт, 1 - 2 байта на значение
        if (remaining < need) {
            throw std::runtime_error("invalid DQT length");
        }
        auto& table = quant_tables_[id].emplace();
        for (int i = 0; i < 64; ++i) {
            uint16_t value = precision ? reader_.ReadWord() : reader_.ReadByte();
            if (value == 0) {
                throw std::runtime_error("zero quantization value");
            }
            table[kZigZag[i]] = value;  // перевод в обычный порядок
        }
        remaining -= need;
    }
}

void JpegDecoder::ReadDht() {
    uint16_t length = reader_.ReadWord();
    if (length < 2) {
        throw std::runtime_error("invalid DHT length");
    }
    size_t remaining = length - 2u;
    if (remaining == 0) {
        throw std::runtime_error("empty DHT");
    }
    while (remaining > 0) {
        uint8_t first_byte = reader_.ReadByte();
        --remaining;
        int klass = first_byte >> 4;  // 0 = DC, 1 = AC
        int id = first_byte & 0x0F;
        if (klass > 1 || id > 3) {
            throw std::runtime_error("invalid Huffman table spec");
        }
        if (remaining < 16) {
            throw std::runtime_error("invalid DHT length");
        }
        std::vector<uint8_t> counts(16);  // counts[i] - число кодов длины i+1
        size_t total = 0;
        for (auto& count : counts) {
            count = reader_.ReadByte();
            total += count;
        }
        remaining -= 16;
        if (total > 256 || remaining < total) {
            throw std::runtime_error("invalid DHT length");
        }
        std::vector<uint8_t> values(total);  // символы в порядке возрастания кода
        for (auto& value : values) {
            value = reader_.ReadByte();
        }
        remaining -= total;
        if (klass == 0) {
            dc_trees_[id].Build(counts, values);
            dc_defined_[id] = true;
        } else {
            ac_trees_[id].Build(counts, values);
            ac_defined_[id] = true;
        }
    }
}

void JpegDecoder::ReadSof(bool progressive) {
    if (have_frame_) {
        throw std::runtime_error("multiple SOF markers");
    }
    uint16_t length = reader_.ReadWord();
    if (reader_.ReadByte() != 8) {
        throw std::runtime_error("unsupported sample precision");
    }
    height_ = reader_.ReadWord();
    width_ = reader_.ReadWord();
    int component_count = reader_.ReadByte();
    if (width_ == 0 || height_ == 0) {
        throw std::runtime_error("invalid image dimensions");
    }
    if (static_cast<uint64_t>(width_) * height_ > kMaxPixels) {
        throw std::runtime_error("image is too large");
    }
    if (component_count != 1 && component_count != 3) {
        throw std::runtime_error("unsupported number of components");
    }
    if (length != 8 + 3 * component_count) {
        throw std::runtime_error("invalid SOF length");
    }

    for (int i = 0; i < component_count; ++i) {
        Component comp;
        comp.id = reader_.ReadByte();
        uint8_t hv = reader_.ReadByte();
        comp.h = hv >> 4;  // коэффициенты downsampling-а
        comp.v = hv & 0x0F;
        comp.quant_id = reader_.ReadByte();
        if (comp.h < 1 || comp.h > 4 || comp.v < 1 || comp.v > 4 || comp.quant_id > 3) {
            throw std::runtime_error("invalid component parameters");
        }
        for (const auto& other : components_) {
            if (other.id == comp.id) {
                throw std::runtime_error("duplicate component id");
            }
        }
        components_.push_back(std::move(comp));
    }

    hmax_ = vmax_ = 1;  // размер MCU = 8*hmax_ * 8*vmax_
    for (const auto& comp : components_) {
        hmax_ = std::max(hmax_, static_cast<size_t>(comp.h));
        vmax_ = std::max(vmax_, static_cast<size_t>(comp.v));
    }

    mcus_wide_ = (width_ + 8 * hmax_ - 1) / (8 * hmax_);  // ceil
    mcus_tall_ = (height_ + 8 * vmax_ - 1) / (8 * vmax_);
    for (auto& comp : components_) {
        // выровнено по MCU, включая доп. блоки за краем изображения
        comp.blocks_wide = mcus_wide_ * comp.h;
        comp.blocks_tall = mcus_tall_ * comp.v;
        size_t samples_wide = (width_ * comp.h + hmax_ - 1) / hmax_;
        size_t samples_tall = (height_ * comp.v + vmax_ - 1) / vmax_;
        // фактический размер без выравнивания по MCU
        comp.scan_blocks_wide = (samples_wide + 7) / 8;
        comp.scan_blocks_tall = (samples_tall + 7) / 8;
        comp.coefficients.assign(comp.blocks_wide * comp.blocks_tall * 64, 0);
    }

    progressive_ = progressive;
    have_frame_ = true;
}

uint8_t JpegDecoder::ReadSosAndDecodeScan() {
    if (!have_frame_) {
        throw std::runtime_error("SOS before SOF");
    }
    uint16_t length = reader_.ReadWord();
    int scan_component_count = reader_.ReadByte();
    if (scan_component_count < 1 || scan_component_count > 4 ||
        length != 6 + 2 * scan_component_count) {
        throw std::runtime_error("invalid SOS header");
    }
    std::vector<size_t> scan_components;
    for (int i = 0; i < scan_component_count; ++i) {
        int id = reader_.ReadByte();
        size_t index = components_.size();
        for (size_t j = 0; j < components_.size(); ++j) {
            if (components_[j].id == id) {
                index = j;
                break;
            }
        }
        if (index == components_.size()) {
            throw std::runtime_error("scan references unknown component");
        }
        for (auto used : scan_components) {
            if (used == index) {
                throw std::runtime_error("duplicate component in scan");
            }
        }
        uint8_t first_byte = reader_.ReadByte();
        components_[index].dc_table = first_byte >> 4;
        components_[index].ac_table = first_byte & 0x0F;
        if (components_[index].dc_table > 3 || components_[index].ac_table > 3) {
            throw std::runtime_error("invalid Huffman table selector");
        }
        scan_components.push_back(index);
    }
    // ss_, se_ - диапазон индексов в этом скане (spectral selection)
    // ah_, al_ - битовая точность (successive approximation)
    // al_ - до какого бита уже передано значение
    // ah_ - точность предыдущего скана (0 если первый)
    ss_ = reader_.ReadByte();
    se_ = reader_.ReadByte();
    uint8_t ah_al = reader_.ReadByte();
    ah_ = ah_al >> 4;
    al_ = ah_al & 0x0F;

    if (!progressive_) {
        if (ss_ != 0 || se_ != 63 || ah_ != 0 || al_ != 0) {
            throw std::runtime_error("invalid spectral parameters for baseline scan");
        }
    } else {
        if (ss_ > 63 || se_ > 63 || ss_ > se_) {
            throw std::runtime_error("invalid spectral selection");
        }
        if (ss_ == 0 && se_ != 0) {
            throw std::runtime_error("DC scan must cover only coefficient 0");
        }
        if (ss_ > 0 &&
            scan_components.size() != 1) {  // AC сканы всегда содержат только одну компоненту
            throw std::runtime_error("AC scans must not be interleaved");
        }
        // уточнение обязано идти ровно на 1 бит точнее предыдущего скана этого диапазона
        if (ah_ > 13 || al_ > 13 || (ah_ != 0 && ah_ != al_ + 1)) {
            throw std::runtime_error("invalid successive approximation parameters");
        }
    }

    for (auto index : scan_components) {
        auto& comp = components_[index];
        if (!quant_tables_[comp.quant_id]) {
            throw std::runtime_error("scan references missing quantization table");
        }
        if (!progressive_) {
            if (!dc_defined_[comp.dc_table] || !ac_defined_[comp.ac_table]) {
                throw std::runtime_error("scan references missing Huffman table");
            }
        } else {
            if (ss_ == 0 && ah_ == 0 && !dc_defined_[comp.dc_table]) {
                throw std::runtime_error("scan references missing Huffman table");
            }
            if (ss_ > 0 && !ac_defined_[comp.ac_table]) {
                throw std::runtime_error("scan references missing Huffman table");
            }
        }
        if (progressive_) {
            // AC refine сканы компоненты запрещены до её первого DC скана
            if (ss_ == 0 && ah_ == 0) {
                comp.dc_started = true;
            } else if (!comp.dc_started) {
                throw std::runtime_error("progressive scan before the DC scan of a component");
            }
        }
    }

    return DecodeScan(scan_components);
}

uint8_t JpegDecoder::DecodeScan(const std::vector<size_t>& scan_components) {
    ScanBitReader bits(reader_);
    for (auto index : scan_components) {
        components_[index].dc_predictor = 0;  // DC кодируется как разность
    }
    eobrun_ = 0;  // счётчик end-of-band прохода
    if (scan_components.size() == 1) {
        // проход по своей сетке компоненты, без привязки к MCU
        auto& comp = components_[scan_components[0]];
        for (size_t row = 0; row < comp.scan_blocks_tall; ++row) {
            for (size_t col = 0; col < comp.scan_blocks_wide; ++col) {
                DecodeBlock(comp, row, col, bits);
            }
        }
    } else {
        // по MCU, внутри каждого MCU все блоки всех компонент по очереди
        for (size_t mcu_row = 0; mcu_row < mcus_tall_; ++mcu_row) {
            for (size_t mcu_col = 0; mcu_col < mcus_wide_; ++mcu_col) {
                for (auto index : scan_components) {
                    auto& comp = components_[index];
                    for (int y = 0; y < comp.v; ++y) {
                        for (int x = 0; x < comp.h; ++x) {
                            DecodeBlock(comp, mcu_row * comp.v + y, mcu_col * comp.h + x, bits);
                        }
                    }
                }
            }
        }
    }

    ++scan_count_;
    return bits.FinishScan();
}

void JpegDecoder::DecodeBlock(Component& comp, size_t block_row, size_t block_col,
                              ScanBitReader& bits) {
    int32_t* block = comp.coefficients.data() + (block_row * comp.blocks_wide + block_col) * 64;
    if (!progressive_) {
        DecodeBaselineBlock(comp, block, bits);
    } else if (ss_ == 0) {
        if (ah_ == 0) {
            DecodeDcFirst(comp, block, bits);
        } else {
            DecodeDcRefine(block, bits);
        }
    } else {
        if (ah_ == 0) {
            DecodeAcFirst(comp, block, bits);
        } else {
            DecodeAcRefine(comp, block, bits);
        }
    }
}

void JpegDecoder::DecodeBaselineBlock(Component& comp, int32_t* block, ScanBitReader& bits) {
    // DC символ = размер разности + биты самой разности
    int size = DecodeSymbol(dc_trees_[comp.dc_table], bits);
    if (size > 11) {
        throw std::runtime_error("invalid DC coefficient size");
    }
    int diff = size ? RecoverValue(bits.ReadBits(size), size) : 0;
    comp.dc_predictor += diff;
    block[0] = static_cast<int32_t>(comp.dc_predictor);

    // AC символ = (zeros, size)
    int i = 1;
    while (i <= 63) {
        int symbol = DecodeSymbol(ac_trees_[comp.ac_table], bits);
        int zeros = symbol >> 4;
        int size = symbol & 0x0F;
        if (size == 0) {
            if (zeros == 15) {
                i += 16;
                continue;
            }
            if (zeros != 0) {
                throw std::runtime_error("invalid AC symbol in baseline scan");
            }
            break;  // EOB
        }
        i += zeros;
        if (i > 63 || size > 10) {
            throw std::runtime_error("invalid AC coefficient");
        }
        block[kZigZag[i]] = RecoverValue(bits.ReadBits(size), size);
        ++i;
    }
}

void JpegDecoder::DecodeDcFirst(Component& comp, int32_t* block, ScanBitReader& bits) {
    int size = DecodeSymbol(dc_trees_[comp.dc_table], bits);
    if (size > 11) {
        throw std::runtime_error("invalid DC coefficient size");
    }
    int diff = size ? RecoverValue(bits.ReadBits(size), size) : 0;
    comp.dc_predictor += diff;
    block[0] =
        static_cast<int32_t>(comp.dc_predictor * (int64_t{1} << al_));  // резерв под refine биты
}

void JpegDecoder::DecodeDcRefine(int32_t* block, ScanBitReader& bits) {
    if (bits.ReadBit()) {  // без Хаффмана, 1 сырой бит на блок
        block[0] |= int32_t{1} << al_;
    }
}

void JpegDecoder::DecodeAcFirst(Component& comp, int32_t* block, ScanBitReader& bits) {
    if (eobrun_ > 0) {  // блок состоит только из нулей
        --eobrun_;
        return;
    }
    int i = ss_;
    while (i <= se_) {
        int symbol = DecodeSymbol(ac_trees_[comp.ac_table], bits);
        int zeros = symbol >> 4;
        int size = symbol & 0x0F;
        if (size == 0) {
            if (zeros != 15) {
                // блоки без AC данных
                eobrun_ = (1u << zeros) - 1;
                if (zeros) {
                    eobrun_ += bits.ReadBits(zeros);
                }
                break;
            }
            i += 16;
            continue;
        }
        i += zeros;
        if (i > se_ || size > 10) {
            throw std::runtime_error("invalid AC coefficient");
        }
        block[kZigZag[i]] = RecoverValue(bits.ReadBits(size), size) * (1 << al_);
        ++i;
    }
}

void JpegDecoder::DecodeAcRefine(Component& comp, int32_t* block, ScanBitReader& bits) {
    const int32_t positive = int32_t{1} << al_;
    const int32_t negative = -positive;
    auto refine = [&](int32_t& coefficient) {
        if (bits.ReadBit() && (coefficient & positive) == 0) {
            coefficient += coefficient >= 0 ? positive : negative;
        }
    };

    int i = ss_;
    if (eobrun_ == 0) {
        while (i <= se_) {
            int symbol = DecodeSymbol(ac_trees_[comp.ac_table], bits);
            int zeros = symbol >> 4;
            int size = symbol & 0x0F;
            int32_t new_value = 0;
            if (size != 0) {
                if (size != 1) {  // новый коэффициент кодирует только знак
                    throw std::runtime_error("invalid refinement symbol");
                }
                new_value = bits.ReadBit() ? positive : negative;
            } else if (zeros != 15) {
                eobrun_ = (1u << zeros);
                if (zeros) {
                    eobrun_ += bits.ReadBits(zeros);
                }
                break;
            }
            // zeros - число ещё нулевых позиций для пропуска
            // встреченные ненулевые не расходуют zeros, а просто уточняются
            while (i <= se_) {
                int32_t& coefficient = block[kZigZag[i]];
                if (coefficient != 0) {
                    refine(coefficient);
                } else if (--zeros < 0) {
                    break;  // нашли нужную нулевую позицию
                }
                ++i;
            }
            if (new_value != 0) {
                if (i > se_) {
                    throw std::runtime_error("refinement value out of band");
                }
                block[kZigZag[i]] = new_value;
            }
            ++i;
        }
    }
    if (eobrun_ > 0) {
        // только доуточняем существующие коэффициенты, новых уже не появляется
        while (i <= se_) {
            int32_t& coefficient = block[kZigZag[i]];
            if (coefficient != 0) {
                refine(coefficient);
            }
            ++i;
        }
        --eobrun_;
    }
}

Image JpegDecoder::BuildImage() {
    if (!have_frame_ || scan_count_ == 0) {
        throw std::runtime_error("no image data");
    }

    std::vector<double> dct_input(64);
    std::vector<double> dct_output(64);
    DctCalculator dct(8, &dct_input, &dct_output);  // план FFTW создаётся один раз на все блоки

    std::vector<std::vector<uint8_t>> planes(components_.size());
    for (size_t ci = 0; ci < components_.size(); ++ci) {
        auto& comp = components_[ci];
        const auto& quant = quant_tables_[comp.quant_id];
        if (!quant) {
            throw std::runtime_error("missing quantization table");
        }
        size_t stride = comp.blocks_wide * 8;
        planes[ci].assign(stride * comp.blocks_tall * 8, 0);  // сетка выровнена по MCU
        for (size_t row = 0; row < comp.blocks_tall; ++row) {
            for (size_t col = 0; col < comp.blocks_wide; ++col) {
                const int32_t* block =
                    comp.coefficients.data() + (row * comp.blocks_wide + col) * 64;
                for (int i = 0; i < 64; ++i) {
                    dct_input[i] = static_cast<double>(block[i]) * (*quant)[i];  // деквантизация
                }
                dct.Inverse();
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        // добавляем 128 обратно
                        planes[ci][(row * 8 + y) * stride + col * 8 + x] =
                            Clamp8(dct_output[y * 8 + x] + 128.0);
                    }
                }
            }
        }
    }

    Image image(width_, height_);
    image.SetComment(comment_);
    for (size_t y = 0; y < height_; ++y) {
        for (size_t x = 0; x < width_; ++x) {
            // upsampling
            auto sample = [&](size_t ci) -> int {
                const auto& comp = components_[ci];
                size_t sx = x * comp.h / hmax_;
                size_t sy = y * comp.v / vmax_;
                return planes[ci][sy * comp.blocks_wide * 8 + sx];
            };
            RGB pixel;
            if (components_.size() == 1) {
                pixel.r = pixel.g = pixel.b = sample(0);  // grayscale
            } else {
                // YCbCr -> RGB, Cb Cr сдвинуты на +128
                double luma = sample(0);
                double cb = sample(1) - 128.0;
                double cr = sample(2) - 128.0;
                pixel.r = Clamp8(luma + 1.402 * cr);
                pixel.g = Clamp8(luma - 0.344136 * cb - 0.714136 * cr);
                pixel.b = Clamp8(luma + 1.772 * cb);
            }
            image.SetPixel(y, x, pixel);
        }
    }
    return image;
}
