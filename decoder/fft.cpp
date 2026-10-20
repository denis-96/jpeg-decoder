#include "fft.h"

#include <cmath>
#include <stdexcept>

DctCalculator::DctCalculator(size_t width, std::vector<double>* input, std::vector<double>* output)
    : width_(width), input_(input), output_(output) {
    if (width == 0 || input == nullptr || output == nullptr || input->size() != width * width ||
        output->size() != width * width) {
        throw std::invalid_argument("invalid DCT buffer size");
    }
    plan_ = fftw_plan_r2r_2d(static_cast<int>(width), static_cast<int>(width), input->data(),
                             output->data(), FFTW_REDFT01, FFTW_REDFT01, FFTW_ESTIMATE);
    if (plan_ == nullptr) {
        throw std::runtime_error("failed to create an FFTW plan");
    }
}

DctCalculator::~DctCalculator() {
    fftw_destroy_plan(plan_);
}

void DctCalculator::Inverse() {
    const double first = std::sqrt(0.5);
    const double rest = 0.5;
    auto& in = *input_;
    for (size_t u = 0; u < width_; ++u) {
        const double row_factor = (u == 0) ? first : rest;
        for (size_t v = 0; v < width_; ++v) {
            in[u * width_ + v] *= row_factor * ((v == 0) ? first : rest);
        }
    }
    fftw_execute(plan_);
    const double scale = 2.0 / static_cast<double>(width_);
    for (auto& x : *output_) {
        x *= scale;
    }
}
