#pragma once

#include <cstddef>
#include <vector>

#include <fftw3.h>

class DctCalculator {
public:
    DctCalculator(size_t width, std::vector<double>* input, std::vector<double>* output);
    DctCalculator(const DctCalculator&) = delete;
    DctCalculator& operator=(const DctCalculator&) = delete;
    ~DctCalculator();
    void Inverse();

private:
    size_t width_;
    std::vector<double>* input_;
    std::vector<double>* output_;
    fftw_plan plan_;
};
