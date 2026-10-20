#include "huffman.h"

#include <stdexcept>

void HuffmanTree::Build(const std::vector<uint8_t>& code_lengths,
                        const std::vector<uint8_t>& values) {
    if (code_lengths.size() > 16) {
        throw std::invalid_argument("too many code lengths");
    }
    size_t total_codes = 0;
    for (auto count : code_lengths) {
        total_codes += count;
    }
    if (total_codes != values.size()) {
        throw std::invalid_argument("number of values does not match code lengths");
    }

    root_ = std::make_unique<Node>();
    position_ = root_.get();

    uint32_t code = 0;
    size_t next_value = 0;
    for (size_t length = 1; length <= code_lengths.size(); ++length) {
        for (int i = 0; i < code_lengths[length - 1]; ++i) {
            if (code >= (1u << length)) {
                root_.reset();
                position_ = nullptr;
                throw std::invalid_argument("code lengths overflow");
            }
            Node* node = root_.get();
            for (size_t bit = length; bit > 0; --bit) {
                if (node->terminal) {
                    root_.reset();
                    position_ = nullptr;
                    throw std::invalid_argument("not a prefix code");
                }
                auto& child = node->children[(code >> (bit - 1)) & 1];
                if (!child) {
                    child = std::make_unique<Node>();
                }
                node = child.get();
            }
            node->terminal = true;
            node->value = values[next_value++];
            ++code;
        }
        code <<= 1;
    }
}

bool HuffmanTree::Move(bool bit, int& value) {
    if (!root_) {
        throw std::invalid_argument("tree is not built");
    }
    Node* next = position_->children[bit].get();
    if (!next) {
        position_ = root_.get();
        throw std::invalid_argument("no such code in the tree");
    }
    if (next->terminal) {
        value = next->value;
        position_ = root_.get();
        return true;
    }
    position_ = next;
    return false;
}
