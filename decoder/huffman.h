#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class HuffmanTree {
public:
    void Build(const std::vector<uint8_t>& code_lengths, const std::vector<uint8_t>& values);
    bool Move(bool bit, int& value);

private:
    struct Node {
        std::unique_ptr<Node> children[2];
        int value = 0;
        bool terminal = false;
    };
    std::unique_ptr<Node> root_;
    Node* position_ = nullptr;
};
