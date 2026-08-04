// DSP 等价性测试: 复刻 tools/gen_reference.py 的合成场景, 输出同结构 JSON
// 用法: dsp_test > dsp-cpp.json
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include "dsp.h"

using dsp::Vec;

namespace {
constexpr double kPi = std::numbers::pi_v<double>;

// 皮尔逊相关系数 (总体均值/方差, ddof=0), 与前 n 个样本
double corr(const Vec& a, const Vec& b, size_t n) {
    if (n == 0)
        return 0.0;
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double va = 0.0, vb = 0.0, cov = 0.0;
    for (size_t i = 0; i < n; ++i) {
        va += (a[i] - ma) * (a[i] - ma);
        vb += (b[i] - mb) * (b[i] - mb);
        cov += (a[i] - ma) * (b[i] - mb);
    }
    va /= static_cast<double>(n);
    vb /= static_cast<double>(n);
    cov /= static_cast<double>(n);
    const double den = std::sqrt(va) * std::sqrt(vb);
    return den > 0.0 ? cov / den : 0.0;
}

const char* algoKey(dsp::Algorithm a) {
    return dsp::algorithmKey(a).data();
}
} // namespace

int main() {
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", message);
            passed = false;
        }
    };
    expect(dsp::hann(0).empty(), "hann rejects a non-positive window size");
    expect(!dsp::algorithmFromString("unknown").has_value(),
           "unknown algorithm key was silently accepted");
    expect(dsp::reflectIndex(-42, 5) == 2, "reflectIndex mishandles a large negative index");
    expect(dsp::reflectIndex(-42, 1) == 0, "reflectIndex handles a single sample");
    expect(dsp::stft({1.0}, 7, 2).empty(), "stft rejects an odd FFT size");
    Vec unchanged = {1.0, 2.0, 3.0, 4.0};
    dsp::gaussianFilter2D(unchanged, 2, 2, 0.0, 0.0);
    expect(unchanged == Vec({1.0, 2.0, 3.0, 4.0}), "zero-sigma filter is an identity");

    const int sr = 22050;
    const int dur = 10;
    const size_t n = static_cast<size_t>(sr) * dur;
    Vec t(n);
    for (size_t i = 0; i < n; ++i)
        t[i] = static_cast<double>(i) / sr;
    Vec vocal(n), inst(n);
    for (size_t i = 0; i < n; ++i) {
        vocal[i] = 0.5 * std::sin(2.0 * kPi * 440.0 * t[i]) + 0.2 * std::sin(2.0 * kPi * 880.0 * t[i]);
        inst[i] = 0.4 * std::sin(2.0 * kPi * 110.0 * t[i]) + 0.1 * std::sin(2.0 * kPi * 220.0 * t[i]);
    }
    Vec mix(n);
    for (size_t i = 0; i < n; ++i)
        mix[i] = vocal[i] + inst[i];
    const size_t lag = static_cast<size_t>(0.05 * sr);
    Vec instL(n, 0.0);
    std::copy(inst.begin(), inst.end() - static_cast<long>(lag), instL.begin() + static_cast<long>(lag));
    std::vector<Vec> song = {mix, mix};
    std::vector<Vec> acc = {instL, instL};

    const std::vector<Vec> aligned = dsp::alignAudio(song, acc, sr);
    const double alignCorr = corr(aligned[0], inst, static_cast<size_t>(sr));
    expect(alignCorr > 0.99, "alignment correlation is too low");

    const dsp::Algorithm algos[] = {dsp::Algorithm::SoftMask,        dsp::Algorithm::SpectralSubtraction,
                                    dsp::Algorithm::WienerFilter,    dsp::Algorithm::FrequencyWeighted,
                                    dsp::Algorithm::BinaryMask,      dsp::Algorithm::PhaseSensitive,
                                    dsp::Algorithm::Lossless};
    std::printf("{\n  \"align_corr\": %.6f,\n  \"algorithms\": {\n", alignCorr);
    for (size_t a = 0; a < 7; ++a) {
        const Vec out = dsp::processChannel(song[0], aligned[0], sr, 2048, 512, 0.5, algos[a], 8);
        const size_t m = std::min(out.size(), n);
        const double rV = corr(out, vocal, m);
        const double rI = corr(out, inst, m);
        expect(!out.empty(), "algorithm returned no samples");
        expect(std::isfinite(rV) && std::isfinite(rI), "algorithm returned non-finite metrics");
        expect(rV > rI, "algorithm retained more accompaniment than vocals");
        std::printf("    \"%s\": {\"r_vocal\": %.6f, \"r_inst\": %.6f}%s\n", algoKey(algos[a]), rV, rI,
                    a == 6 ? "" : ",");
    }
    std::printf("  }\n}\n");
    return passed ? 0 : 1;
}
