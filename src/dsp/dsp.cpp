#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fftw3.h>
#include <limits>
#include <numbers>

namespace dsp {

namespace {
constexpr double kPi = std::numbers::pi_v<double>;

Vec gaussKernel(double sigma) {
    if (sigma <= 0.0 || !std::isfinite(sigma))
        return {1.0};
    const int radius = static_cast<int>(std::lround(4.0 * sigma));
    Vec k(static_cast<size_t>(2 * radius + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-(static_cast<double>(i * i)) / (2.0 * sigma * sigma));
        const int kernelIndex = i + radius;
        k[static_cast<size_t>(kernelIndex)] = v;
        sum += v;
    }
    for (double& v : k)
        v /= sum;
    return k;
}
} // namespace

std::optional<Algorithm> algorithmFromString(std::string_view value) {
    if (value == "lossless")
        return Algorithm::Lossless;
    if (value == "soft_mask")
        return Algorithm::SoftMask;
    if (value == "spectral_subtraction")
        return Algorithm::SpectralSubtraction;
    if (value == "wiener_filter")
        return Algorithm::WienerFilter;
    if (value == "frequency_weighted")
        return Algorithm::FrequencyWeighted;
    if (value == "binary_mask")
        return Algorithm::BinaryMask;
    if (value == "phase_sensitive")
        return Algorithm::PhaseSensitive;
    return std::nullopt;
}

std::string_view algorithmKey(Algorithm algorithm) {
    switch (algorithm) {
    case Algorithm::Lossless:
        return "lossless";
    case Algorithm::SoftMask:
        return "soft_mask";
    case Algorithm::SpectralSubtraction:
        return "spectral_subtraction";
    case Algorithm::WienerFilter:
        return "wiener_filter";
    case Algorithm::FrequencyWeighted:
        return "frequency_weighted";
    case Algorithm::BinaryMask:
        return "binary_mask";
    case Algorithm::PhaseSensitive:
        return "phase_sensitive";
    }
    return {};
}

Vec hann(int n) {
    if (n <= 0)
        return {};
    Vec w(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k)
        w[static_cast<size_t>(k)] = 0.5 * (1.0 - std::cos(2.0 * kPi * k / n));
    return w;
}

int reflectIndex(int i, int n) {
    if (n <= 1)
        return 0;
    const auto period = static_cast<long long>(n - 1) * 2LL;
    auto reflected = static_cast<long long>(i) % period;
    if (reflected < 0)
        reflected += period;
    if (reflected >= n)
        reflected = period - reflected;
    return static_cast<int>(reflected);
}

std::vector<CVec> stft(const Vec& x, int nFft, int hop, const std::stop_token& stopToken) {
    if (nFft < 2 || nFft % 2 != 0 || hop <= 0 || x.empty() ||
        x.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return {};
    const int n = static_cast<int>(x.size());
    const int pad = nFft / 2;
    if (n > std::numeric_limits<int>::max() - 2 * pad)
        return {};
    const int paddedLen = n + 2 * pad;
    const int nFrames = 1 + (paddedLen - nFft) / hop;
    if (nFrames <= 0)
        return {};
    const Vec window = hann(nFft);
    const int bins = nFft / 2 + 1;
    std::vector<CVec> frames(static_cast<size_t>(nFrames), CVec(static_cast<size_t>(bins)));
    Vec frame(static_cast<size_t>(nFft));
    std::vector<double> out(static_cast<size_t>(2 * bins));
    fftw_plan plan = fftw_plan_dft_r2c_1d(
        nFft, frame.data(), reinterpret_cast<fftw_complex*>(out.data()), FFTW_ESTIMATE);
    if (plan == nullptr)
        return {};
    for (int f = 0; f < nFrames; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested()) {
            fftw_destroy_plan(plan);
            return {};
        }
        for (int j = 0; j < nFft; ++j) {
            const int idx = f * hop + j - pad;
            const double v = (idx >= 0 && idx < n) ? x[static_cast<size_t>(idx)]
                                                   : x[static_cast<size_t>(reflectIndex(idx, n))];
            frame[static_cast<size_t>(j)] = v * window[static_cast<size_t>(j)];
        }
        fftw_execute(plan);
        for (int k = 0; k < bins; ++k) {
            const size_t packedIndex = static_cast<size_t>(k) * 2U;
            frames[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                std::complex<double>(out[packedIndex], out[packedIndex + 1U]);
        }
    }
    fftw_destroy_plan(plan);
    return frames;
}

Vec istft(const std::vector<CVec>& frames, int hop, const std::stop_token& stopToken) {
    if (frames.empty() || frames[0].size() < 2 || hop <= 0)
        return {};
    if (frames.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return {};
    const size_t frameBins = frames[0].size();
    if (frameBins > static_cast<size_t>(std::numeric_limits<int>::max()) / 2U + 1U)
        return {};
    for (const CVec& frameBinsData : frames) {
        if (frameBinsData.size() != frameBins)
            return {};
    }
    const int nFrames = static_cast<int>(frames.size());
    const int nFft = 2 * (static_cast<int>(frames[0].size()) - 1);
    if (nFrames > 1 && hop > (std::numeric_limits<int>::max() - nFft) / (nFrames - 1))
        return {};
    const int expected = nFft + hop * (nFrames - 1);
    const int bins = nFft / 2 + 1;
    const Vec window = hann(nFft);
    Vec y(static_cast<size_t>(expected), 0.0);
    Vec wsum(static_cast<size_t>(expected), 0.0);
    std::vector<double> in(static_cast<size_t>(2 * bins));
    std::vector<double> frame(static_cast<size_t>(nFft));
    fftw_plan plan = fftw_plan_dft_c2r_1d(
        nFft, reinterpret_cast<fftw_complex*>(in.data()), frame.data(), FFTW_ESTIMATE);
    if (plan == nullptr)
        return {};
    for (int f = 0; f < nFrames; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested()) {
            fftw_destroy_plan(plan);
            return {};
        }
        for (int k = 0; k < bins; ++k) {
            const size_t packedIndex = static_cast<size_t>(k) * 2U;
            in[packedIndex] = frames[static_cast<size_t>(f)][static_cast<size_t>(k)].real();
            in[packedIndex + 1U] = frames[static_cast<size_t>(f)][static_cast<size_t>(k)].imag();
        }
        fftw_execute(plan); // FFTW c2r 不归一化 (输出 = nFft × 真值), numpy irfft 除以 n
        for (int j = 0; j < nFft; ++j) {
            const int idx = f * hop + j;
            y[static_cast<size_t>(idx)] +=
                window[static_cast<size_t>(j)] * (frame[static_cast<size_t>(j)] / nFft);
            wsum[static_cast<size_t>(idx)] += window[static_cast<size_t>(j)] * window[static_cast<size_t>(j)];
        }
    }
    fftw_destroy_plan(plan);
    for (int j = 0; j < expected; ++j) {
        if (wsum[static_cast<size_t>(j)] > 1e-10)
            y[static_cast<size_t>(j)] /= wsum[static_cast<size_t>(j)];
        else
            y[static_cast<size_t>(j)] = 0.0;
    }
    Vec out(static_cast<size_t>(expected - nFft));
    std::copy(y.begin() + nFft / 2, y.end() - nFft / 2, out.begin());
    return out;
}

std::vector<double> fftFrequencies(int sr, int nFft) {
    if (sr <= 0 || nFft < 2 || nFft % 2 != 0)
        return {};
    const int bins = nFft / 2 + 1;
    std::vector<double> f(static_cast<size_t>(bins));
    for (int k = 0; k < bins; ++k)
        f[static_cast<size_t>(k)] = static_cast<double>(sr) * k / nFft;
    return f;
}

void gaussianFilter2D(Vec& data, int rows, int cols, double sigmaRow, double sigmaCol,
                      const std::stop_token& stopToken) {
    if (rows <= 0 || cols <= 0 ||
        data.size() < static_cast<size_t>(rows) * static_cast<size_t>(cols))
        return;
    const Vec kr = gaussKernel(sigmaRow);
    const Vec kc = gaussKernel(sigmaCol);
    const int krMid = static_cast<int>(kr.size() / 2);
    const int kcMid = static_cast<int>(kc.size() / 2);
    Vec tmp(static_cast<size_t>(rows) * static_cast<size_t>(cols));
    // 沿行（轴0 = 频点）
    for (int r = 0; r < rows; ++r) {
        if ((r & 31) == 0 && stopToken.stop_requested())
            return;
        for (int c = 0; c < cols; ++c) {
            double acc = 0.0;
            for (int t = -krMid; t <= krMid; ++t) {
                const int rr = reflectIndex(r + t, rows);
                const int kernelIndex = t + krMid;
                acc += kr[static_cast<size_t>(kernelIndex)] *
                       data[static_cast<size_t>(rr) * static_cast<size_t>(cols) + static_cast<size_t>(c)];
            }
            tmp[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)] = acc;
        }
    }
    // 沿列（轴1 = 时间）
    for (int r = 0; r < rows; ++r) {
        if ((r & 31) == 0 && stopToken.stop_requested())
            return;
        for (int c = 0; c < cols; ++c) {
            double acc = 0.0;
            for (int t = -kcMid; t <= kcMid; ++t) {
                const int cc = reflectIndex(c + t, cols);
                const int kernelIndex = t + kcMid;
                acc += kc[static_cast<size_t>(kernelIndex)] *
                       tmp[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(cc)];
            }
            data[static_cast<size_t>(r) * static_cast<size_t>(cols) + static_cast<size_t>(c)] = acc;
        }
    }
}

std::vector<Vec> alignAudio(const std::vector<Vec>& song, const std::vector<Vec>& acc, int sr,
                            const std::stop_token& stopToken) {
    if (song.empty() || acc.empty() || sr <= 0)
        return acc;
    const size_t songLen = song[0].size();
    size_t check = songLen;
    for (const Vec& channel : song)
        check = std::min(check, channel.size());
    for (const Vec& channel : acc)
        check = std::min(check, channel.size());
    const size_t accLen = check;
    if (songLen == 0 || accLen == 0)
        return acc;
    check = std::min(static_cast<size_t>(30) * static_cast<size_t>(sr), check);
    const int nChSong = static_cast<int>(song.size());
    const int nChAcc = static_cast<int>(acc.size());
    Vec songMono(check), accMono(check);
    for (size_t i = 0; i < check; ++i) {
        double s = 0.0;
        for (int c = 0; c < nChSong; ++c)
            s += song[static_cast<size_t>(c)][i];
        songMono[i] = s / nChSong;
        double a = 0.0;
        for (int c = 0; c < nChAcc; ++c)
            a += acc[static_cast<size_t>(c)][i];
        accMono[i] = a / nChAcc;
    }
    auto normalize = [](Vec& v) {
        double mean = 0.0;
        for (double x : v)
            mean += x;
        mean /= static_cast<double>(v.size());
        double var = 0.0;
        for (double x : v)
            var += (x - mean) * (x - mean);
        var /= static_cast<double>(v.size());
        const double stdv = std::sqrt(var) + 1e-10;
        for (double& x : v)
            x = (x - mean) / stdv;
    };
    normalize(songMono);
    normalize(accMono);
    // FFT 互相关: C = irfft(rfft(song) * conj(rfft(acc))), 取前 N+M-1 个
    const size_t N = check;
    const size_t M = check;
    size_t npad = 1;
    while (npad < N + M - 1)
        npad <<= 1;
    if (npad > static_cast<size_t>(std::numeric_limits<int>::max()))
        return acc;
    std::vector<double> a(npad, 0.0), v(npad, 0.0);
    std::copy(songMono.begin(), songMono.end(), a.begin());
    // scipy.signal.correlate(x, y) = 卷积 x 与反转的 y: irfft(rfft(x) * rfft(y[::-1]))
    std::copy(accMono.rbegin(), accMono.rend(), v.begin());
    std::vector<double> A(2 * (npad / 2 + 1)), V(2 * (npad / 2 + 1));
    fftw_plan pa = fftw_plan_dft_r2c_1d(
        static_cast<int>(npad), a.data(), reinterpret_cast<fftw_complex*>(A.data()), FFTW_ESTIMATE);
    fftw_plan pv = fftw_plan_dft_r2c_1d(
        static_cast<int>(npad), v.data(), reinterpret_cast<fftw_complex*>(V.data()), FFTW_ESTIMATE);
    if (pa == nullptr || pv == nullptr) {
        if (pa != nullptr)
            fftw_destroy_plan(pa);
        if (pv != nullptr)
            fftw_destroy_plan(pv);
        return acc;
    }
    fftw_execute(pa);
    fftw_execute(pv);
    if (stopToken.stop_requested()) {
        fftw_destroy_plan(pa);
        fftw_destroy_plan(pv);
        return acc;
    }
    const size_t bins = npad / 2 + 1;
    std::vector<double> Cbuf(2 * bins);
    for (size_t k = 0; k < bins; ++k) {
        const double ar = A[2 * k], ai = A[2 * k + 1];
        const double vr = V[2 * k], vi = V[2 * k + 1];
        Cbuf[2 * k] = ar * vr - ai * vi;     // Re(A * V)
        Cbuf[2 * k + 1] = ai * vr + ar * vi; // Im(A * V)
    }
    fftw_plan pc = fftw_plan_dft_c2r_1d(
        static_cast<int>(npad), reinterpret_cast<fftw_complex*>(Cbuf.data()), a.data(), FFTW_ESTIMATE);
    if (pc == nullptr) {
        fftw_destroy_plan(pa);
        fftw_destroy_plan(pv);
        return acc;
    }
    fftw_execute(pc); // FFTW c2r 不归一化, 除以 npad
    for (size_t i = 0; i < npad; ++i)
        a[i] /= static_cast<double>(npad);
    fftw_destroy_plan(pa);
    fftw_destroy_plan(pv);
    fftw_destroy_plan(pc);
    size_t bestIdx = 0;
    double bestVal = a[0];
    for (size_t i = 1; i < N + M - 1; ++i) {
        if (a[i] > bestVal) {
            bestVal = a[i];
            bestIdx = i;
        }
    }
    const long lag = static_cast<long>(bestIdx) - static_cast<long>(M - 1);
    std::vector<Vec> result = acc;
    if (lag > 0) {
        const size_t lagU = static_cast<size_t>(lag);
        for (Vec& ch : result) {
            Vec padded(lagU + ch.size(), 0.0);
            std::copy(ch.begin(), ch.end(), padded.begin() + static_cast<long>(lagU));
            ch = std::move(padded);
            if (ch.size() > songLen)
                ch.resize(songLen);
        }
    } else if (lag < 0) {
        const size_t lagU = static_cast<size_t>(-lag);
        for (Vec& ch : result) {
            if (ch.size() > lagU)
                ch.erase(ch.begin(), ch.begin() + static_cast<long>(lagU));
            else
                ch.clear();
            const size_t padLen = songLen > ch.size() ? songLen - ch.size() : 0;
            ch.insert(ch.end(), padLen, 0.0);
            if (ch.size() > songLen)
                ch.resize(songLen);
        }
    }
    return result;
}

// ============================================================================
// Lossless（无损模式）: 参考伴奏 + 幅度谱相干性对消 (coherence_cancel v3)
//   阶段一: 分块速率漂移补偿(相位斜坡 de-rotation) + Mel 带域相干统计(γ² 去偏 +
//           smoothstep 门限) + 逐帧增益 + 功率一致性钳位
//   阶段二: 观众噪声抑制 (YIN F0 + 谐波梳 + 噪声 PSD Wiener)
// 设计依据: 论文 #1(K-pop 对齐+缩放+相减, tempo 失配需 time-stretch),
//           #7(W≤V 钳位, 中位数鲁棒), #11/#13(低频细带/mel 带域统计),
//           #14(相位关键), #15(分段对齐+增益网格+非负钳位), #3(混合一致性)
// ============================================================================
namespace {

constexpr int kLosslessNfft = 4096;
constexpr int kLosslessHop = 1024;
constexpr int kMelBands = 80;
constexpr double kMelFmin = 30.0;
constexpr double kCohLo = 0.15;
constexpr double kCohHi = 0.70;
constexpr double kGainLo = 0.5;
constexpr double kGainHi = 2.0;
constexpr int kRateIters = 3;
constexpr double kRateFloorRel = 1e-6;
constexpr double kHmagMax = 2.0;

// Slaney mel 尺度 (librosa 等价)
double hzToMelSlaney(double f) {
    const double fMin = 0.0, fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0;
    const double minLogMel = (minLogHz - fMin) / fSp;
    const double logstep = std::log(6.4) / 27.0;
    double mels = (f - fMin) / fSp;
    if (f >= minLogHz)
        mels = minLogMel + std::log(f / minLogHz) / logstep;
    return mels;
}
double melToHzSlaney(double m) {
    const double fMin = 0.0, fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0;
    const double minLogMel = (minLogHz - fMin) / fSp;
    const double logstep = std::log(6.4) / 27.0;
    double f = fMin + fSp * m;
    if (m >= minLogMel)
        f = minLogHz * std::exp(logstep * (m - minLogMel));
    return f;
}

struct MelBand {
    int start = 0;
    int end = 0;
    Vec w; // bins [start, end) 的三角权重
};

struct MelBank {
    std::vector<MelBand> bands;
    Vec sumW;  // 每 bin: Σ_b w(b,k)
    Vec nEffF; // 每 bin: 1/Σ_b w'(b,k)², w' 为按 bin 归一化权重
};

MelBank buildMelBank(int sr, int nFft, int nBands, double fmin) {
    const int bins = nFft / 2 + 1;
    MelBank mb;
    mb.bands.resize(static_cast<size_t>(nBands));
    mb.sumW.assign(static_cast<size_t>(bins), 0.0);
    mb.nEffF.assign(static_cast<size_t>(bins), 1.0);
    const double fmax = sr / 2.0;
    const double m0 = hzToMelSlaney(fmin), m1 = hzToMelSlaney(fmax);
    const double dm = (m1 - m0) / (nBands + 1);
    Vec melPts(static_cast<size_t>(nBands + 2));
    for (int i = 0; i < nBands + 2; ++i)
        melPts[static_cast<size_t>(i)] = melToHzSlaney(m0 + i * dm);
    for (int b = 0; b < nBands; ++b) {
        const double lo = melPts[static_cast<size_t>(b)];
        const double ct = melPts[static_cast<size_t>(b) + 1U];
        const double hi = melPts[static_cast<size_t>(b) + 2U];
        int start = bins, end = 0;
        Vec w(static_cast<size_t>(bins), 0.0);
        for (int k = 0; k < bins; ++k) {
            const double f = static_cast<double>(sr) * k / nFft;
            double v = 0.0;
            if (f >= lo && f <= ct && ct > lo)
                v = (f - lo) / (ct - lo);
            else if (f > ct && f <= hi && hi > ct)
                v = (hi - f) / (hi - ct);
            if (v > 0.0) {
                w[static_cast<size_t>(k)] = v;
                mb.sumW[static_cast<size_t>(k)] += v;
                start = std::min(start, k);
                end = k + 1;
            }
        }
        MelBand& band = mb.bands[static_cast<size_t>(b)];
        band.start = start;
        band.end = end;
        band.w.assign(w.begin() + start, w.begin() + end);
    }
    for (int k = 0; k < bins; ++k) {
        if (mb.sumW[static_cast<size_t>(k)] > 0.0) {
            double s = 0.0;
            for (const MelBand& band : mb.bands) {
                if (k >= band.start && k < band.end) {
                    const double wn = band.w[static_cast<size_t>(k - band.start)] /
                                      mb.sumW[static_cast<size_t>(k)];
                    s += wn * wn;
                }
            }
            mb.nEffF[static_cast<size_t>(k)] = s > 0.0 ? 1.0 / s : 1.0;
        }
    }
    return mb;
}

double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void smooth1D(Vec& v, double sigma) {
    if (sigma <= 0.0 || v.size() < 2)
        return;
    const Vec k = gaussKernel(sigma);
    const int mid = static_cast<int>(k.size() / 2);
    Vec out(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        double acc = 0.0;
        for (int t = -mid; t <= mid; ++t) {
            const int ii = reflectIndex(static_cast<int>(i) + t, static_cast<int>(v.size()));
            acc += k[static_cast<size_t>(t) + static_cast<size_t>(mid)] * v[static_cast<size_t>(ii)];
        }
        out[i] = acc;
    }
    v = std::move(out);
}

// 每 bin 内容频率: 参考 STFT 自身逐帧相位推进的中位数, 按 bin 中心去混叠
// (音调偏离 bin 中心时, 用 bin 中心频率做斜坡会留下随累积延迟增长的残余相位)
std::vector<double> contentFrequencies(const std::vector<CVec>& ref, int sr, int hop,
                                       const std::vector<double>& refPower, double accFloor) {
    const int bins = static_cast<int>(ref[0].size());
    const int frames = static_cast<int>(ref.size());
    const int nFft = 2 * (bins - 1);
    std::vector<double> fc(static_cast<size_t>(bins));
    std::vector<double> adv;
    for (int k = 0; k < bins; ++k) {
        if (refPower[static_cast<size_t>(k)] <= accFloor || frames < 3) {
            fc[static_cast<size_t>(k)] = static_cast<double>(sr) * k / nFft;
            continue;
        }
        adv.clear();
        for (int f = 0; f + 1 < frames; ++f) {
            const std::complex<double> c = ref[f + 1][k] * std::conj(ref[f][k]);
            adv.push_back(std::atan2(c.imag(), c.real()));
        }
        const size_t m = adv.size() / 2;
        std::nth_element(adv.begin(), adv.begin() + static_cast<long>(m), adv.end());
        const double med = adv[m];
        const double expected = 2.0 * kPi * k * hop / nFft;
        const double mTurns = std::round((expected - med) / (2.0 * kPi));
        const double phasePerFrame = med + 2.0 * kPi * mTurns;
        fc[static_cast<size_t>(k)] =
            std::clamp(phasePerFrame * sr / (2.0 * kPi * hop), 0.0, sr / 2.0);
    }
    return fc;
}

// 残差互谱 C = Y·conj(Xd) 的每帧相位推进 → 每 bin 速率, 参考能量加权中位数聚合
// (低能量 bin 用 accFloor 门限剔除; 中位数对分段相位阶跃鲁棒)
double estimateRate(const std::vector<CVec>& Y, int yOffset, const std::vector<CVec>& Xd,
                    const std::vector<double>& fc, const std::vector<double>& refPower, int sr,
                    int hop, double accFloor, double fcLimit, const std::stop_token& stopToken) {
    const int bins = static_cast<int>(Y[0].size());
    const int frames = static_cast<int>(Xd.size());
    if (frames < 4)
        return 0.0;
    std::vector<std::pair<double, double>> rates; // (rate, weight)
    rates.reserve(static_cast<size_t>(bins));
    std::vector<double> dphi;
    dphi.reserve(static_cast<size_t>(frames - 1));
    for (int k = 0; k < bins; ++k) {
        if ((k & 127) == 0 && stopToken.stop_requested())
            return 0.0;
        const double fk = fc[static_cast<size_t>(k)];
        const double pw = refPower[static_cast<size_t>(k)];
        if (pw <= accFloor || fk <= 0.0)
            continue;
        if (fcLimit > 0.0 && fk > fcLimit)
            continue;
        dphi.clear();
        for (int f = 0; f + 1 < frames; ++f) {
            const std::complex<double> c1 = Y[static_cast<size_t>(yOffset) + static_cast<size_t>(f)][static_cast<size_t>(k)] *
                                            std::conj(Xd[static_cast<size_t>(f)][static_cast<size_t>(k)]);
            const std::complex<double> c2 =
                Y[static_cast<size_t>(yOffset) + static_cast<size_t>(f) + 1U][static_cast<size_t>(k)] *
                std::conj(Xd[static_cast<size_t>(f) + 1U][static_cast<size_t>(k)]);
            const std::complex<double> p = c2 * std::conj(c1);
            dphi.push_back(std::atan2(p.imag(), p.real()));
        }
        const size_t m = dphi.size() / 2;
        std::nth_element(dphi.begin(), dphi.begin() + static_cast<long>(m), dphi.end());
        const double rate = dphi[m] * sr / (2.0 * kPi * fk * hop);
        rates.emplace_back(rate, pw);
    }
    if (rates.empty())
        return 0.0;
    std::sort(rates.begin(), rates.end());
    double total = 0.0;
    for (const auto& r : rates)
        total += r.second;
    double acc = 0.0;
    for (const auto& r : rates) {
        acc += r.second;
        if (acc >= total / 2.0)
            return r.first;
    }
    return rates.back().first;
}

// YIN 基频估计 (de Cheveigné & Kawahara 2002, 简化版), 无可靠基频返回 0
double yinPitch(const Vec& x, size_t start, int frame, int sr) {
    const int tmax = std::min(static_cast<int>(sr / 60.0), frame / 2);
    const int tmin = std::max(1, static_cast<int>(sr / 1000.0));
    if (tmax <= tmin)
        return 0.0;
    Vec d(static_cast<size_t>(tmax + 1), 0.0);
    for (int t = 1; t <= tmax; ++t) {
        double acc = 0.0;
        for (int j = 0; j + t < frame; ++j) {
            const double a = x[start + static_cast<size_t>(j)];
            const double b = x[start + static_cast<size_t>(j) + static_cast<size_t>(t)];
            const double diff = a - b;
            acc += diff * diff;
        }
        d[static_cast<size_t>(t)] = acc;
    }
    Vec cm(static_cast<size_t>(tmax + 1), 1.0);
    double run = 0.0;
    for (int t = 1; t <= tmax; ++t) {
        run += d[static_cast<size_t>(t)];
        cm[static_cast<size_t>(t)] = run > 0.0 ? d[static_cast<size_t>(t)] * t / run : 1.0;
    }
    double tau = 0.0;
    bool found = false;
    for (int t = tmin; t <= tmax; ++t) {
        if (cm[static_cast<size_t>(t)] < 0.10) {
            tau = t;
            found = true;
            break;
        }
    }
    if (found) {
        // 首个低于阈值的 τ 附近可能有更深的 dip (如 440+880 在 τ=48/50 都有 dip),
        // 在 ±4 窗内取 cm 最小点再插值
        const int lo = std::max(tmin, static_cast<int>(tau) - 4);
        const int hi = std::min(tmax, static_cast<int>(tau) + 4);
        auto it = std::min_element(cm.begin() + lo, cm.begin() + hi + 1);
        if (it != cm.end())
            tau = static_cast<double>(it - cm.begin());
    } else {
        auto it = std::min_element(cm.begin() + tmin, cm.begin() + tmax + 1);
        if (it == cm.end() || *it > 0.5)
            return 0.0;
        tau = static_cast<double>(it - cm.begin());
    }
    const int tInt = static_cast<int>(tau);
    if (tInt > 0 && tInt < tmax) {
        const double a = cm[static_cast<size_t>(tInt - 1)];
        const double b = cm[static_cast<size_t>(tInt)];
        const double c = cm[static_cast<size_t>(tInt) + 1U];
        const double den = a - 2.0 * b + c;
        if (std::abs(den) > 1e-12)
            tau += std::clamp(0.5 * (a - c) / den, -1.0, 1.0);
    }
    return tau > 0.0 ? static_cast<double>(sr) / tau : 0.0;
}

// 二阶 Butterworth 高通 (RBJ cookbook), fc=140Hz 防低频伴奏基频劫持 YIN
Vec biquadHighpass(const Vec& x, int sr, double fc) {
    const double w0 = 2.0 * kPi * fc / sr;
    const double alpha = std::sin(w0) / std::numbers::sqrt2;
    const double c = std::cos(w0);
    const double b0 = (1.0 + c) / 2.0, b1 = -(1.0 + c), b2 = b0;
    const double a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
    Vec y(x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double xn = x[i];
        const double yn = (b0 * xn + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1;
        x1 = xn;
        y2 = y1;
        y1 = yn;
        y[i] = yn;
    }
    return y;
}

// 阶段二: 观众/环境噪声抑制 — 谐波(歌声)保留, 非谐波噪声按 PSD Wiener 衰减
Vec suppressAudience(const Vec& x, int sr, const std::stop_token& stopToken) {
    if (x.empty())
        return x;
    const Vec hp = biquadHighpass(x, sr, 140.0);
    const int yF = 2048, yH = 512;
    const int yFrames = hp.size() > static_cast<size_t>(yF)
                            ? static_cast<int>((hp.size() - static_cast<size_t>(yF)) / yH) + 1
                            : 0;
    std::vector<double> f0(static_cast<size_t>(yFrames), 0.0);
    for (int i = 0; i < yFrames; ++i) {
        if ((i & 63) == 0 && stopToken.stop_requested())
            return x;
        f0[static_cast<size_t>(i)] = yinPitch(hp, static_cast<size_t>(i) * yH, yF, sr);
    }
    auto X = stft(x, kLosslessNfft, kLosslessHop, stopToken);
    if (X.empty())
        return x;
    const int frames = static_cast<int>(X.size());
    const int bins = static_cast<int>(X[0].size());
    const double binHz = static_cast<double>(sr) / kLosslessNfft;
    constexpr double kNoiseAlpha = 0.1;  // 噪声帧 PSD 更新率
    constexpr double kVoiceAlpha = 0.05; // 人声帧谐波间隙 PSD 更新率
    constexpr double kHarmRatio = 0.25;
    constexpr int kF0Median = 5; // F0 中值滤波窗 (防梳窗抖动把谐波 bin 漏进 PSD)
    Vec noisePsd(static_cast<size_t>(bins), 0.0);
    Vec f0Hist(static_cast<size_t>(kF0Median), 0.0); // 环形缓冲
    double f0Cur = 0.0; // 前向跟踪的当前有效 F0 (噪声帧也用它排除谐波, 防 PSD 吸收人声)
    for (int f = 0; f < frames; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested())
            return x;
        const int yIdx = std::clamp(f * kLosslessHop / yH, 0, yFrames - 1);
        const double f0h = f0[static_cast<size_t>(yIdx)];
        bool isVoice = false;
        if (f0h > 40.0) {
            double comb = 0.0, total = 0.0;
            // 梳半宽与 F0 成比例 (容 YIN 估计误差), 下限一个 bin
            const double combHalf = std::max(0.05 * f0h, binHz);
            for (int k = 0; k < bins; ++k) {
                const double p = std::norm(X[static_cast<size_t>(f)][static_cast<size_t>(k)]);
                total += p;
                const double fk = k * binHz;
                const double n = fk / f0h;
                if (std::abs(n - std::round(n)) * f0h <= combHalf)
                    comb += p;
            }
            // 谐波性门: 梳状能量占比足够高才算人声帧, 防垃圾 F0 误伤
            isVoice = total > 1e-12 && comb / total >= kHarmRatio;
            if (isVoice) {
                f0Hist[static_cast<size_t>(f % kF0Median)] = f0h;
                // 仅对有效 F0 取中值 (未填充/失败的槽位为 0, 必须剔除)
                Vec valid;
                valid.reserve(f0Hist.size());
                for (double v : f0Hist)
                    if (v > 40.0)
                        valid.push_back(v);
                if (!valid.empty()) {
                    std::sort(valid.begin(), valid.end());
                    f0Cur = valid[valid.size() / 2];
                }
            }
        }
        const double alpha = isVoice ? kVoiceAlpha : kNoiseAlpha;
        const double combHalf = std::max(0.05 * f0Cur, binHz);
        for (int k = 0; k < bins; ++k) {
            // 仅在谐波间隙更新 PSD: 谐波 bin 保留人声, 永不被 PSD 吸收
            if (f0Cur > 40.0) {
                const double fk = k * binHz;
                const double n = fk / f0Cur;
                if (std::abs(n - std::round(n)) * f0Cur <= combHalf)
                    continue;
            }
            const double p = std::norm(X[static_cast<size_t>(f)][static_cast<size_t>(k)]);
            noisePsd[static_cast<size_t>(k)] =
                (1.0 - alpha) * noisePsd[static_cast<size_t>(k)] + alpha * p;
        }
    }
    if (stopToken.stop_requested())
        return x;
    for (int f = 0; f < frames; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested())
            return x;
        for (int k = 0; k < bins; ++k) {
            const double p = std::norm(X[static_cast<size_t>(f)][static_cast<size_t>(k)]);
            // Wiener: 分母加 0.5·N 在中低信噪比下比纯谱减更强
            const double v = std::max(0.0, p - noisePsd[static_cast<size_t>(k)]);
            const double g = v / (p + 0.5 * noisePsd[static_cast<size_t>(k)] + 1e-12);
            X[static_cast<size_t>(f)][static_cast<size_t>(k)] *= g;
        }
    }
    return istft(X, kLosslessHop, stopToken);
}

// 无损模式主流程 (coherence_cancel v3)
Vec losslessCancel(const Vec& song, const Vec& acc, int sr, double strength, double sigmaTime,
                   const std::stop_token& stopToken) {
    auto Y = stft(song, kLosslessNfft, kLosslessHop, stopToken);
    auto A = stft(acc, kLosslessNfft, kLosslessHop, stopToken);
    const size_t minCols = std::min(Y.size(), A.size());
    if (minCols == 0)
        return {};
    Y.resize(minCols);
    A.resize(minCols);
    const int bins = static_cast<int>(Y[0].size());
    const int frames = static_cast<int>(minCols);
    const int nFft = 2 * (bins - 1);
    // 参考功率 (速率估计/内容频率的能量门限)
    std::vector<double> refPower(static_cast<size_t>(bins), 0.0);
    for (int f = 0; f < frames; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested())
            return {};
        for (int k = 0; k < bins; ++k)
            refPower[static_cast<size_t>(k)] +=
                std::norm(A[static_cast<size_t>(f)][static_cast<size_t>(k)]);
    }
    const double maxRef = *std::max_element(refPower.begin(), refPower.end());
    const double accFloor = kRateFloorRel * maxRef;
    const std::vector<double> fc = contentFrequencies(A, sr, kLosslessHop, refPower, accFloor);
    const MelBank mel = buildMelBank(sr, nFft, kMelBands, kMelFmin);
    // 时间高斯核的有效样本数 nEffT = (Σw)²/Σw² = 1/Σk² (核已归一化)
    const Vec timeKernel = gaussKernel(sigmaTime);
    double k2sum = 0.0;
    for (double kk : timeKernel)
        k2sum += kk * kk;
    const double nEffT = k2sum > 0.0 ? 1.0 / k2sum : 1.0;
    // 分块: 块长 15s, 50% 重叠, 帧域 overlap-average (论文 #12 OA deframing)
    const int blockFrames = std::max(32, static_cast<int>(15.0 * sr / kLosslessHop));
    const int blockHop = std::max(blockFrames / 2, 1);
    std::vector<CVec> Yv(static_cast<size_t>(frames), CVec(static_cast<size_t>(bins)));
    std::vector<double> wsum(static_cast<size_t>(frames), 0.0);
    double rhoPrev = 0.0;
    for (int fb = 0; fb < frames; fb += blockHop) {
        if (stopToken.stop_requested())
            return {};
        const int nf = std::min(frames - fb, blockFrames);
        // ---- 速率漂移估计 + 相位斜坡 de-rotation ----
        // θ = 2π·Δ(f)·f_content(k), Δ = ρ·(t - t_block) 为累积延迟 (论文 #1/#15)
        std::vector<CVec> Xd(static_cast<size_t>(nf), CVec(static_cast<size_t>(bins)));
        const auto derotate = [&](double rho) {
            for (int f = 0; f < nf; ++f) {
                const double t = static_cast<double>(f) * kLosslessHop / sr;
                for (int k = 0; k < bins; ++k) {
                    const double th = 2.0 * kPi * fc[static_cast<size_t>(k)] * rho * t;
                    Xd[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                        A[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)] *
                        std::complex<double>(std::cos(th), std::sin(th));
                }
            }
        };
        double rho = rhoPrev;
        for (int it = 0; it < kRateIters; ++it) {
            derotate(rho);
            // 第一次迭代只信任低频 bin (每帧相位推进 < π, 防混叠), 之后全频带
            const double fcLimit = (it == 0) ? 1200.0 : 0.0;
            const double dr = estimateRate(Y, fb, Xd, fc, refPower, sr, kLosslessHop, accFloor,
                                           fcLimit, stopToken);
            rho += dr;
        }
        derotate(rho);
        rhoPrev = rho;
        // ---- Mel 带域相干统计 (S_ya / S_aa / S_yy), 时间轴高斯平滑 ----
        const int nb = static_cast<int>(mel.bands.size());
        const size_t bandStride = static_cast<size_t>(nf);
        Vec syaR(static_cast<size_t>(nb) * bandStride, 0.0);
        Vec syaI(static_cast<size_t>(nb) * bandStride, 0.0);
        Vec saa(static_cast<size_t>(nb) * bandStride, 0.0);
        Vec syy(static_cast<size_t>(nb) * bandStride, 0.0);
        for (int f = 0; f < nf; ++f) {
            if ((f & 63) == 0 && stopToken.stop_requested())
                return {};
            for (int b = 0; b < nb; ++b) {
                const MelBand& band = mel.bands[static_cast<size_t>(b)];
                double ar = 0.0, ai = 0.0, aa = 0.0, yy = 0.0;
                for (int kk = band.start; kk < band.end; ++kk) {
                    const double w = band.w[static_cast<size_t>(kk - band.start)];
                    const std::complex<double> yc =
                        Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(kk)] *
                        std::conj(Xd[static_cast<size_t>(f)][static_cast<size_t>(kk)]);
                    ar += w * yc.real();
                    ai += w * yc.imag();
                    aa += w * std::norm(Xd[static_cast<size_t>(f)][static_cast<size_t>(kk)]);
                    yy += w * std::norm(Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(kk)]);
                }
                const size_t idx = static_cast<size_t>(b) * bandStride + static_cast<size_t>(f);
                syaR[idx] = ar;
                syaI[idx] = ai;
                saa[idx] = aa;
                syy[idx] = yy;
            }
        }
        // 频率轴 σ=0 恒等 (分带即频率平滑), 时间轴 σ=sigmaTime
        gaussianFilter2D(syaR, nb, nf, 0.0, sigmaTime, stopToken);
        gaussianFilter2D(syaI, nb, nf, 0.0, sigmaTime, stopToken);
        gaussianFilter2D(saa, nb, nf, 0.0, sigmaTime, stopToken);
        gaussianFilter2D(syy, nb, nf, 0.0, sigmaTime, stopToken);
        if (stopToken.stop_requested())
            return {};
        // ---- 映射回 bin 域 + γ² 去偏 + 门限 + H + 逐帧增益 + 功率钳位 ----
        // 每 bin 覆盖的带
        std::vector<std::vector<int>> cov(static_cast<size_t>(bins));
        for (int b = 0; b < nb; ++b)
            for (int k = mel.bands[static_cast<size_t>(b)].start;
                 k < mel.bands[static_cast<size_t>(b)].end; ++k)
                cov[static_cast<size_t>(k)].push_back(b);
        Vec num(static_cast<size_t>(nf), 0.0), den(static_cast<size_t>(nf), 0.0);
        Vec gateBuf(static_cast<size_t>(bins) * bandStride, 0.0);
        Vec hReBuf(static_cast<size_t>(bins) * bandStride, 0.0);
        Vec hImBuf(static_cast<size_t>(bins) * bandStride, 0.0);
        constexpr double kEps = 1e-10;
        for (int k = 0; k < bins; ++k) {
            const double sw = mel.sumW[static_cast<size_t>(k)];
            const std::vector<int>& covering = cov[static_cast<size_t>(k)];
            for (int f = 0; f < nf; ++f) {
                const size_t fIdx = static_cast<size_t>(f);
                double syar = 0.0, syai = 0.0, saaf = 0.0, syyf = 0.0;
                if (sw > 0.0) {
                    for (int b : covering) {
                        const size_t idx = static_cast<size_t>(b) * bandStride + fIdx;
                        const double wb = mel.bands[static_cast<size_t>(b)]
                                              .w[static_cast<size_t>(k - mel.bands[static_cast<size_t>(b)].start)] /
                                          sw;
                        syar += wb * syaR[idx];
                        syai += wb * syaI[idx];
                        saaf += wb * saa[idx];
                        syyf += wb * syy[idx];
                    }
                } else {
                    // 带外 bin (低于 fmin): 用未平滑的原始值
                    const std::complex<double> yc =
                        Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)] *
                        std::conj(Xd[fIdx][static_cast<size_t>(k)]);
                    syar = yc.real();
                    syai = yc.imag();
                    saaf = std::norm(Xd[fIdx][static_cast<size_t>(k)]);
                    syyf = std::norm(Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)]);
                }
                const size_t kIdx = static_cast<size_t>(k) * bandStride + fIdx;
                double gate = 0.0;
                if (saaf > 1e-12) {
                    const double g2 = (syar * syar + syai * syai) / (syyf * saaf + kEps);
                    const double nEff = mel.nEffF[static_cast<size_t>(k)] * nEffT;
                    double g2d = nEff > 1.0 + kEps ? (g2 * nEff - 1.0) / (nEff - 1.0) : g2;
                    g2d = std::clamp(g2d, 0.0, 1.0);
                    gate = smoothstep(kCohLo, kCohHi, g2d);
                    // H = S_ya / S_aa (复数对消滤波, 携带相位)
                    double hm = std::hypot(syar, syai) / (saaf + kEps);
                    hm = std::min(hm, kHmagMax);
                    const double hPh = std::atan2(syai, syar);
                    hReBuf[kIdx] = hm * std::cos(hPh);
                    hImBuf[kIdx] = hm * std::sin(hPh);
                    // 逐帧增益累加 (相干 bin 上度量 H 对当前帧增益的低估/高估)
                    num[fIdx] += g2d * syyf;
                    den[fIdx] += g2d * hm * hm * saaf;
                }
                gateBuf[kIdx] = gate;
            }
        }
        Vec gain(static_cast<size_t>(nf));
        for (int f = 0; f < nf; ++f)
            gain[static_cast<size_t>(f)] = std::sqrt(num[static_cast<size_t>(f)] /
                                                     (den[static_cast<size_t>(f)] + kEps));
        smooth1D(gain, 2.0);
        for (double& g : gain)
            g = std::clamp(g, kGainLo, kGainHi);
        for (int k = 0; k < bins; ++k) {
            for (int f = 0; f < nf; ++f) {
                const size_t kIdx = static_cast<size_t>(k) * bandStride + static_cast<size_t>(f);
                const std::complex<double> H(hReBuf[kIdx], hImBuf[kIdx]);
                std::complex<double> cancel = gateBuf[kIdx] * H * gain[static_cast<size_t>(f)] *
                                              Xd[static_cast<size_t>(f)][static_cast<size_t>(k)];
                // 功率一致性钳位: |对消量| ≤ |混合| (W≤V, 防过度对消相位翻转, 论文 #7/#3/#9)
                const double cm = std::abs(cancel);
                const double ym = std::abs(Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)]);
                if (cm > ym && ym > 0.0)
                    cancel *= ym / cm;
                Yv[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)] +=
                    Y[static_cast<size_t>(fb) + static_cast<size_t>(f)][static_cast<size_t>(k)] - strength * cancel;
            }
        }
        for (int f = 0; f < nf; ++f)
            wsum[static_cast<size_t>(fb) + static_cast<size_t>(f)] += 1.0;
    }
    for (int f = 0; f < frames; ++f) {
        if (wsum[static_cast<size_t>(f)] > 0.0)
            for (int k = 0; k < bins; ++k)
                Yv[static_cast<size_t>(f)][static_cast<size_t>(k)] /= wsum[static_cast<size_t>(f)];
    }
    Vec vocal = istft(Yv, kLosslessHop, stopToken);
    return suppressAudience(vocal, sr, stopToken);
}

} // namespace

Vec processChannel(const Vec& song, const Vec& acc, int sr, int nFft, int hop, double strength,
                   Algorithm algo, double sigmaTime, const std::stop_token& stopToken) {
    if (song.empty() || acc.empty() || sr <= 0 || !std::isfinite(strength) ||
        !std::isfinite(sigmaTime))
        return {};
    strength = std::clamp(strength, 0.0, 1.0);
    sigmaTime = std::max(0.0, sigmaTime);
    if (algo == Algorithm::Lossless) {
        return losslessCancel(song, acc, sr, strength, sigmaTime, stopToken);
    }

    auto Y_song = stft(song, nFft, hop, stopToken);
    auto Y_acc = stft(acc, nFft, hop, stopToken);
    const size_t minCols = std::min(Y_song.size(), Y_acc.size());
    if (minCols == 0)
        return {};
    Y_song.resize(minCols);
    Y_acc.resize(minCols);
    const int bins = static_cast<int>(Y_song[0].size());
    const int cols = static_cast<int>(minCols);
    std::vector<CVec> Yvocal(static_cast<size_t>(cols), CVec(static_cast<size_t>(bins)));

    if (algo == Algorithm::BinaryMask) {
        Vec mask(static_cast<size_t>(bins) * static_cast<size_t>(cols));
        for (int f = 0; f < cols; ++f) {
            if ((f & 63) == 0 && stopToken.stop_requested())
                return {};
            for (int k = 0; k < bins; ++k) {
                const double songMag = std::abs(Y_song[static_cast<size_t>(f)][static_cast<size_t>(k)]);
                const double accMag = std::abs(Y_acc[static_cast<size_t>(f)][static_cast<size_t>(k)]);
                const double adj = accMag * strength;
                mask[static_cast<size_t>(k) * static_cast<size_t>(cols) + static_cast<size_t>(f)] =
                    (songMag > 1.2 * adj) ? 1.0 : 0.0;
            }
        }
        gaussianFilter2D(mask, bins, cols, 0.5, 0.5, stopToken);
        if (stopToken.stop_requested())
            return {};
        for (int f = 0; f < cols; ++f) {
            if ((f & 63) == 0 && stopToken.stop_requested())
                return {};
            for (int k = 0; k < bins; ++k) {
                const double m = mask[static_cast<size_t>(k) * static_cast<size_t>(cols) + static_cast<size_t>(f)];
                Yvocal[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                    Y_song[static_cast<size_t>(f)][static_cast<size_t>(k)] * m;
            }
        }
        return istft(Yvocal, hop, stopToken);
    }

    const std::vector<double> freqs = fftFrequencies(sr, nFft);
    if (freqs.size() != static_cast<size_t>(bins))
        return {};
    for (int f = 0; f < cols; ++f) {
        if ((f & 63) == 0 && stopToken.stop_requested())
            return {};
        for (int k = 0; k < bins; ++k) {
            const std::complex<double> Ys = Y_song[static_cast<size_t>(f)][static_cast<size_t>(k)];
            const std::complex<double> Ya = Y_acc[static_cast<size_t>(f)][static_cast<size_t>(k)];
            const double songMag = std::abs(Ys);
            const double accMag = std::abs(Ya);
            const double adj = accMag * strength;
            const double adjSq = adj * adj;
            const double songPhase = std::atan2(Ys.imag(), Ys.real());
            std::complex<double> out;
            switch (algo) {
            case Algorithm::SoftMask: {
                const double mask = std::max(0.0, songMag * songMag / (songMag * songMag + adjSq + 1e-10));
                out = Ys * mask;
                break;
            }
            case Algorithm::SpectralSubtraction: {
                const double vMag = std::max(0.0, songMag - adj);
                out = std::complex<double>(vMag * std::cos(songPhase), vMag * std::sin(songPhase));
                break;
            }
            case Algorithm::WienerFilter: {
                const double vPow = std::max(0.0, songMag * songMag - adjSq);
                double mask = vPow / (vPow + adjSq + 1e-10);
                mask = std::max(mask, 0.01);
                out = Ys * mask;
                break;
            }
            case Algorithm::FrequencyWeighted: {
                const double w = (freqs[static_cast<size_t>(k)] >= 80.0 && freqs[static_cast<size_t>(k)] <= 1100.0) ? 1.5 : 1.0;
                const double wsong = songMag * w;
                const double mask = std::max(0.0, wsong * wsong / (wsong * wsong + adjSq + 1e-10));
                out = Ys * mask;
                break;
            }
            case Algorithm::PhaseSensitive: {
                const double accPhase = std::atan2(Ya.imag(), Ya.real());
                double phaseDiff = std::abs(songPhase - accPhase);
                phaseDiff = std::min(phaseDiff, 2.0 * kPi - phaseDiff) / kPi;
                const double ampMask = songMag * songMag / (songMag * songMag + adjSq + 1e-10);
                const double mask = std::clamp(ampMask * (0.7 + 0.3 * phaseDiff), 0.0, 1.0);
                out = Ys * mask;
                break;
            }
            default: { // 兜底同 SoftMask
                const double mask = std::max(0.0, songMag * songMag / (songMag * songMag + adjSq + 1e-10));
                out = Ys * mask;
                break;
            }
            }
            Yvocal[static_cast<size_t>(f)][static_cast<size_t>(k)] = out;
        }
    }
    return istft(Yvocal, hop, stopToken);
}

} // namespace dsp
