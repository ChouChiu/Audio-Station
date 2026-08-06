// DSP 等价性测试: 复刻 tools/gen_reference.py 的合成场景, 输出同结构 JSON
// 用法: dsp_test > dsp-cpp.json
#include <algorithm>
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
    for (const dsp::Algorithm algo : algos) {
        const Vec out = dsp::processChannel(song[0], aligned[0], sr, 2048, 512, 0.5, algo, 8);
        const size_t m = std::min(out.size(), n);
        const double rV = corr(out, vocal, m);
        const double rI = corr(out, inst, m);
        expect(!out.empty(), "algorithm returned no samples");
        expect(std::isfinite(rV) && std::isfinite(rI), "algorithm returned non-finite metrics");
        expect(rV > rI, "algorithm retained more accompaniment than vocals");
        std::printf("    \"%s\": {\"r_vocal\": %.6f, \"r_inst\": %.6f},\n", algoKey(algo), rV, rI);
    }
    // ---- 研究驱动场景: 无损算法 (coherence_cancel v3) 在漂移/抖动/观众噪声下的表现 ----
    const auto lagged = [&](Vec v, size_t l) {
        Vec o(v.size(), 0.0);
        if (l < v.size())
            std::copy(v.begin(), v.end() - static_cast<long>(l), o.begin() + static_cast<long>(l));
        return o;
    };
    const auto addv = [](const Vec& a, const Vec& b) {
        Vec o(a.size(), 0.0);
        for (size_t i = 0; i < a.size(); ++i)
            o[i] = a[i] + (i < b.size() ? b[i] : 0.0);
        return o;
    };
    // 保音高时间伸缩: 采样位置 i/(1+rho) 的线性插值 (论文 #1 的 tempo 失配场景)
    const auto timeStretch = [&](const Vec& v, double rho) {
        Vec o(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            const double pos = static_cast<double>(i) / (1.0 + rho);
            const long i0 = std::clamp(static_cast<long>(pos), 0L, static_cast<long>(v.size()) - 1);
            const long i1 = std::clamp(i0 + 1, 0L, static_cast<long>(v.size()) - 1);
            const double fr = pos - std::floor(pos);
            o[i] = v[static_cast<size_t>(i0)] * (1.0 - fr) + v[static_cast<size_t>(i1)] * fr;
        }
        return o;
    };
    // 分段时序抖动: 1s 段确定性偏移 ±4ms (论文 #15 的分段相位阶跃;
    // 实测 ±8ms 在 110Hz 处产生 5.5 rad 阶跃, 超出 σ_t 平滑窗跟踪能力, 非真实场景)
    const auto jitter = [&](const Vec& v) {
        Vec o(v.size(), 0.0);
        const size_t seg = static_cast<size_t>(sr);
        for (size_t s = 0; s < v.size(); s += seg) {
            const int off = (static_cast<int>(s / seg) % 5 - 2) * sr / 500;
            for (size_t i = s; i < std::min(v.size(), s + seg); ++i) {
                // off ∈ [-88, 88]: 负偏移在 size_t 下回绕为超大值, 越界检查统一为 u < size
                const size_t u =
                    static_cast<size_t>(static_cast<long long>(i) + static_cast<long long>(off));
                o[i] = u < v.size() ? v[u] : 0.0;
            }
        }
        return o;
    };
    // 漂移/抖动/观众场景: 参考用已知 lag 预对齐 (纯音参考与拉伸混音的互相关存在
    // 拍频周期歧义, 会误导全局对齐; 这些场景验证的是漂移补偿而非对齐 — 对齐由 base 场景验证)
    const auto processLossless = [&](const Vec& mixCh, const Vec& refAligned) {
        return dsp::processChannel(mixCh, refAligned, sr, 2048, 512, 1.0,
                                   dsp::Algorithm::Lossless, 8);
    };
    const Vec refPre = lagged(inst, lag);
    const Vec instD04 = timeStretch(inst, 0.004);
    const Vec outD04 = processLossless(addv(vocal, lagged(instD04, lag)), refPre);
    const double d04V = corr(outD04, vocal, std::min(outD04.size(), n));
    const double d04I = corr(outD04, inst, std::min(outD04.size(), n));
    expect(d04V > 0.95, "drift 0.4%: vocal damaged");
    expect(d04I < 0.05, "drift 0.4%: residual accompaniment too high");
    const Vec instD1 = timeStretch(inst, 0.010);
    const Vec outD1 = processLossless(addv(vocal, lagged(instD1, lag)), refPre);
    const double d1V = corr(outD1, vocal, std::min(outD1.size(), n));
    const double d1I = corr(outD1, inst, std::min(outD1.size(), n));
    expect(d1V > 0.95, "drift 1%: vocal damaged");
    expect(d1I < 0.10, "drift 1%: residual accompaniment too high");
    const Vec outJit = processLossless(addv(vocal, lagged(jitter(inst), lag)), refPre);
    const double jitV = corr(outJit, vocal, std::min(outJit.size(), n));
    const double jitI = corr(outJit, inst, std::min(outJit.size(), n));
    expect(jitV > 0.95, "jitter: vocal damaged");
    expect(jitI < 0.05, "jitter: residual accompaniment too high");
    // 观众/环境噪声: 白噪声叠加 (论文 #1 的人群噪声场景)
    Vec noise(n);
    uint32_t seed = 42u;
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        noise[i] = 0.15 * (static_cast<double>(seed) / 4294967295.0 * 2.0 - 1.0);
    }
    const Vec mixAud = addv(addv(vocal, lagged(inst, lag)), noise);
    const Vec outAud = processLossless(mixAud, refPre);
    const double audV = corr(outAud, vocal, std::min(outAud.size(), n));
    const double audI = corr(outAud, inst, std::min(outAud.size(), n));
    const double audNoiseIn = corr(mixAud, noise, std::min(mixAud.size(), n));
    const double audNoiseOut = corr(outAud, noise, std::min(outAud.size(), n));
    expect(audV > 0.95, "audience: vocal damaged");
    expect(audNoiseOut < 0.5 * audNoiseIn, "audience: noise not halved");
    std::printf("  },\n");
    std::printf("  \"scenes\": {\n");
    std::printf("    \"tempo_drift_0p4\": {\"r_vocal\": %.6f, \"r_inst\": %.6f},\n", d04V, d04I);
    std::printf("    \"tempo_drift_1pct\": {\"r_vocal\": %.6f, \"r_inst\": %.6f},\n", d1V, d1I);
    std::printf("    \"timing_jitter\": {\"r_vocal\": %.6f, \"r_inst\": %.6f},\n", jitV, jitI);
    std::printf("    \"audience_noise\": {\"r_vocal\": %.6f, \"r_inst\": %.6f, "
                "\"r_noise_in\": %.6f, \"r_noise_out\": %.6f}\n",
                audV, audI, audNoiseIn, audNoiseOut);
    std::printf("  }\n}\n");
    return passed ? 0 : 1;
}
