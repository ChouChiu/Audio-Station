#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fftw3.h>
#include <limits>
#include <numbers>
#include <utility>

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
    // 最后一帧向右 reflect 补齐，保证 iSTFT 可按原始长度裁剪而不是丢掉尾部 < hop 的样本。
    const int nFrames = 1 + static_cast<int>((static_cast<long long>(n) + hop - 1LL) / hop);
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
    const size_t commonLen = check;
    if (songLen == 0 || commonLen == 0)
        return acc;
    const size_t globalCheck =
        std::min(static_cast<size_t>(8) * static_cast<size_t>(sr), commonLen);
    const int nChSong = static_cast<int>(song.size());
    const int nChAcc = static_cast<int>(acc.size());
    Vec songMono(commonLen), accMono(commonLen);
    for (size_t i = 0; i < commonLen; ++i) {
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
    const size_t N = globalCheck;
    const size_t M = globalCheck;
    size_t npad = 1;
    while (npad < N + M - 1)
        npad <<= 1;
    if (npad > static_cast<size_t>(std::numeric_limits<int>::max()))
        return acc;
    std::vector<double> a(npad, 0.0), v(npad, 0.0);
    std::copy(songMono.begin(), songMono.begin() + static_cast<long>(N), a.begin());
    // scipy.signal.correlate(x, y) = 卷积 x 与反转的 y: irfft(rfft(x) * rfft(y[::-1]))
    std::reverse_copy(accMono.begin(), accMono.begin() + static_cast<long>(M), v.begin());
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
    // 峰值抛物线插值获得小数采样延迟。整数移位在 10 kHz 处即使只差半个采样，
    // 也会留下接近 90 度的相位误差，后续滤波很难稳定消除。
    double peakDelta = 0.0;
    if (bestIdx > 0 && bestIdx + 1 < N + M - 1) {
        const double ym = a[bestIdx - 1], y0 = a[bestIdx], yp = a[bestIdx + 1];
        const double denom = ym - 2.0 * y0 + yp;
        if (std::abs(denom) > 1e-12)
            peakDelta = std::clamp(0.5 * (ym - yp) / denom, -1.0, 1.0);
    }
    const double globalLag = static_cast<double>(bestIdx) + peakDelta - static_cast<double>(M - 1);

    // 局部延迟轨迹：0.1 秒一个锚点、0.1 秒相关窗，在约 2 kHz 的抽取信号上搜索。
    // 这一步修正现场录音常见的时钟漂移与局部剪辑抖动；最终仍以原采样率插值。
    const int decim = std::max(1, sr / 2000);
    const size_t anchorStep = std::max<size_t>(1, static_cast<size_t>(sr) / 10U);
    const size_t halfWindow = std::max<size_t>(1, static_cast<size_t>(sr) / 20U);
    struct Anchor {
        double position;
        double lag;
    };
    std::vector<Anchor> anchors;
    anchors.push_back({.position = 0.0, .lag = globalLag});
    double predictedLag = globalLag;
    for (size_t center = std::min(halfWindow, commonLen - 1); center < commonLen;
         center += anchorStep) {
        if (stopToken.stop_requested())
            return acc;
        const size_t begin = center > halfWindow ? center - halfWindow : 0;
        const size_t end = std::min(commonLen, center + halfWindow);
        const double searchSeconds = anchors.size() == 1 ? 0.25 : 0.06;
        const int radiusSteps = std::max(2, static_cast<int>(std::ceil(searchSeconds * sr / decim)));
        double bestScore = -std::numeric_limits<double>::infinity();
        double bestCorr = -1.0;
        int bestOffset = 0;
        std::vector<double> scores(static_cast<size_t>(2 * radiusSteps + 1), -1.0);
        for (int d = -radiusSteps; d <= radiusSteps; ++d) {
            const long lagSamples = std::lround(predictedLag + d * static_cast<double>(decim));
            double xy = 0.0, xx = 0.0, yy = 0.0;
            size_t used = 0;
            for (size_t i = begin; i < end; i += static_cast<size_t>(decim)) {
                const long long j = static_cast<long long>(i) - lagSamples;
                if (j < 0 || std::cmp_greater_equal(j, accMono.size()))
                    continue;
                const double x = songMono[i];
                const double y = accMono[static_cast<size_t>(j)];
                xy += x * y;
                xx += x * x;
                yy += y * y;
                ++used;
            }
            const double corr = used > 32 ? xy / std::sqrt(xx * yy + 1e-20) : -1.0;
            const size_t scoreSlot =
                static_cast<size_t>(static_cast<long long>(d) + radiusSteps);
            scores[scoreSlot] = corr;
            // 周期性音乐会产生多个相近峰；轻微惩罚远离预测轨迹的候选，避免跳周期。
            const double score = corr - 0.25 * std::abs(d) / std::max(radiusSteps, 1);
            if (score > bestScore) {
                bestScore = score;
                bestCorr = corr;
                bestOffset = d;
            }
        }
        double localLag = predictedLag + bestOffset * static_cast<double>(decim);
        const int scoreIndex = bestOffset + radiusSteps;
        if (bestOffset != 0 && scoreIndex > 0 && scoreIndex + 1 < static_cast<int>(scores.size())) {
            const double ym = scores[static_cast<size_t>(scoreIndex - 1)];
            const double y0 = scores[static_cast<size_t>(scoreIndex)];
            const double yp = scores[static_cast<size_t>(scoreIndex) + 1U];
            const double denom = ym - 2.0 * y0 + yp;
            if (std::abs(denom) > 1e-9)
                localLag += std::clamp(0.5 * (ym - yp) / denom, -1.0, 1.0) * decim;
        }
        const double predictedCorr = scores[static_cast<size_t>(radiusSteps)];
        if (bestCorr < 0.06 || bestCorr < predictedCorr + 0.02)
            localLag = predictedLag;
        // 最大允许 2% 的局部时钟偏差，拒绝由弱相关/节拍重复造成的离群跳变。
        const bool firstLocalAnchor = anchors.size() == 1;
        const double dt = static_cast<double>(center) - anchors.back().position;
        const double maxChange = firstLocalAnchor ? 0.30 * sr : std::max(2.0, 0.02 * dt);
        localLag = std::clamp(localLag, anchors.back().lag - maxChange,
                              anchors.back().lag + maxChange);
        if (firstLocalAnchor)
            anchors[0].lag = localLag;
        anchors.push_back({.position = static_cast<double>(center), .lag = localLag});
        predictedLag = localLag;
        if (center > commonLen - 1 - std::min(anchorStep, commonLen - 1))
            break;
    }
    if (anchors.back().position < static_cast<double>(songLen - 1))
        anchors.push_back(
            {.position = static_cast<double>(songLen - 1), .lag = anchors.back().lag});
    if (anchors.size() > 3) {
        std::vector<Anchor> filtered = anchors;
        for (size_t i = 1; i + 1 < anchors.size(); ++i) {
            double values[3] = {anchors[i - 1].lag, anchors[i].lag, anchors[i + 1].lag};
            std::sort(std::begin(values), std::end(values));
            filtered[i].lag = values[1];
        }
        anchors = std::move(filtered);
    }

    std::vector<Vec> result;
    result.reserve(acc.size());
    for (const Vec& channel : acc) {
        Vec warped(songLen, 0.0);
        size_t ai = 0;
        for (size_t i = 0; i < songLen; ++i) {
            while (ai + 1 < anchors.size() && static_cast<double>(i) > anchors[ai + 1].position)
                ++ai;
            const Anchor& p0 = anchors[ai];
            const Anchor& p1 = anchors[std::min(ai + 1, anchors.size() - 1)];
            const double span = p1.position - p0.position;
            const double u = span > 0.0 ? (static_cast<double>(i) - p0.position) / span : 0.0;
            const double lag = p0.lag + std::clamp(u, 0.0, 1.0) * (p1.lag - p0.lag);
            const double source = static_cast<double>(i) - lag;
            const long long centerSample = static_cast<long long>(std::floor(source));
            constexpr int kLanczosRadius = 8;
            if (centerSample - kLanczosRadius + 1 >= 0 &&
                centerSample + kLanczosRadius < static_cast<long long>(channel.size())) {
                double value = 0.0, weightSum = 0.0;
                for (int tap = -kLanczosRadius + 1; tap <= kLanczosRadius; ++tap) {
                    const long long j = centerSample + tap;
                    const double d = source - static_cast<double>(j);
                    const auto sinc = [](double x) {
                        return std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                    };
                    const double weight = sinc(d) * sinc(d / kLanczosRadius);
                    value += weight * channel[static_cast<size_t>(j)];
                    weightSum += weight;
                }
                warped[i] = std::abs(weightSum) > 1e-12 ? value / weightSum : 0.0;
            } else {
                const long long j0 = centerSample;
                const long long j1 = j0 + 1;
                if (j0 >= 0 && std::cmp_less(j1, channel.size())) {
                    const double frac = source - static_cast<double>(j0);
                    warped[i] = channel[static_cast<size_t>(j0)] * (1.0 - frac) +
                                channel[static_cast<size_t>(j1)] * frac;
                } else if (j0 >= 0 && std::cmp_less(j0, channel.size())) {
                    warped[i] = channel[static_cast<size_t>(j0)];
                }
            }
        }
        result.push_back(std::move(warped));
    }
    return result;
}

// ============================================================================
// Lossless（无损模式）: 参考伴奏 + 幅度谱相干性对消 (coherence_cancel v3)
//   阶段一: 分块速率漂移补偿(相位斜坡 de-rotation) + Mel 带域相干统计(γ² 去偏 +
//           smoothstep 门限) + 逐帧增益 + 功率一致性钳位
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

// O(n) 的对称盒式平滑。协方差统计会对每个频点调用多次，不能使用 O(n*r) 高斯卷积。
void boxSmooth(Vec& v, int radius) {
    if (radius <= 0 || v.size() < 2)
        return;
    Vec prefix(v.size() + 1U, 0.0), out(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        prefix[i + 1U] = prefix[i] + v[i];
    const size_t radiusSize = static_cast<size_t>(radius);
    for (size_t i = 0; i < v.size(); ++i) {
        const size_t lo = i > radiusSize ? i - radiusSize : 0;
        const size_t hi = std::min(v.size(), i + radiusSize + 1U);
        out[i] = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
    }
    v = std::move(out);
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
    vocal.resize(std::min(song.size(), acc.size()), 0.0);
    return vocal;
}

// 立体声 MIMO 参考对消。对每个频点估计 Y=[H_L,H_R]A，协方差仅沿时间平滑，
// 不再把不同频率的复相位先混入 Mel 带，从根源上避免高频相位互相抵消。
std::vector<Vec> losslessCancelStereo(const std::vector<Vec>& song,
                                      const std::vector<Vec>& acc, double strength, double sigmaTime,
                                      const std::stop_token& stopToken) {
    if (song.empty() || acc.empty())
        return {};
    const size_t outChannels = std::min<size_t>(2, song.size());
    const size_t refChannels = std::min<size_t>(2, acc.size());
    size_t targetLength = song[0].size();
    for (size_t c = 0; c < outChannels; ++c)
        targetLength = std::min(targetLength, song[c].size());
    for (size_t c = 0; c < refChannels; ++c)
        targetLength = std::min(targetLength, acc[c].size());
    if (targetLength == 0)
        return {};

    std::vector<std::vector<CVec>> Y;
    std::vector<std::vector<CVec>> A;
    size_t frameCount = std::numeric_limits<size_t>::max();
    for (size_t c = 0; c < outChannels; ++c) {
        auto spectrum = stft(song[c], kLosslessNfft, kLosslessHop, stopToken);
        if (spectrum.empty())
            return {};
        frameCount = std::min(frameCount, spectrum.size());
        Y.push_back(std::move(spectrum));
    }
    for (size_t c = 0; c < refChannels; ++c) {
        auto spectrum = stft(acc[c], kLosslessNfft, kLosslessHop, stopToken);
        if (spectrum.empty())
            return {};
        frameCount = std::min(frameCount, spectrum.size());
        A.push_back(std::move(spectrum));
    }
    const int frames = static_cast<int>(frameCount);
    const int bins = static_cast<int>(Y[0][0].size());
    for (auto& channel : Y)
        channel.resize(frameCount);
    for (auto& channel : A)
        channel.resize(frameCount);
    const int radius = std::max(2, static_cast<int>(std::lround(2.0 * sigmaTime)));

    Vec r00(frameCount), r11(frameCount), r01r(frameCount), r01i(frameCount);
    Vec syy(frameCount), s0r(frameCount), s0i(frameCount), s1r(frameCount), s1i(frameCount);
    constexpr double kGateLow = 0.08;
    constexpr double kGateHigh = 0.58;
    constexpr double kRidge = 0.006;
    constexpr double kEps = 1e-12;
    for (int k = 0; k < bins; ++k) {
        if ((k & 31) == 0 && stopToken.stop_requested())
            return {};
        for (int f = 0; f < frames; ++f) {
            const std::complex<double> a0 = A[0][static_cast<size_t>(f)][static_cast<size_t>(k)];
            const std::complex<double> a1 =
                refChannels > 1 ? A[1][static_cast<size_t>(f)][static_cast<size_t>(k)] : std::complex<double>{};
            r00[static_cast<size_t>(f)] = std::norm(a0);
            r11[static_cast<size_t>(f)] = std::norm(a1);
            const std::complex<double> r01 = a0 * std::conj(a1);
            r01r[static_cast<size_t>(f)] = r01.real();
            r01i[static_cast<size_t>(f)] = r01.imag();
        }
        boxSmooth(r00, radius);
        boxSmooth(r00, radius);
        if (refChannels > 1) {
            boxSmooth(r11, radius);
            boxSmooth(r11, radius);
            boxSmooth(r01r, radius);
            boxSmooth(r01r, radius);
            boxSmooth(r01i, radius);
            boxSmooth(r01i, radius);
        }

        for (size_t oc = 0; oc < outChannels; ++oc) {
            for (int f = 0; f < frames; ++f) {
                const std::complex<double> y = Y[oc][static_cast<size_t>(f)][static_cast<size_t>(k)];
                const std::complex<double> a0 = A[0][static_cast<size_t>(f)][static_cast<size_t>(k)];
                const std::complex<double> a1 =
                    refChannels > 1 ? A[1][static_cast<size_t>(f)][static_cast<size_t>(k)] : std::complex<double>{};
                const std::complex<double> s0 = y * std::conj(a0);
                const std::complex<double> s1 = y * std::conj(a1);
                syy[static_cast<size_t>(f)] = std::norm(y);
                s0r[static_cast<size_t>(f)] = s0.real();
                s0i[static_cast<size_t>(f)] = s0.imag();
                s1r[static_cast<size_t>(f)] = s1.real();
                s1i[static_cast<size_t>(f)] = s1.imag();
            }
            boxSmooth(syy, radius);
            boxSmooth(syy, radius);
            boxSmooth(s0r, radius);
            boxSmooth(s0r, radius);
            boxSmooth(s0i, radius);
            boxSmooth(s0i, radius);
            if (refChannels > 1) {
                boxSmooth(s1r, radius);
                boxSmooth(s1r, radius);
                boxSmooth(s1i, radius);
                boxSmooth(s1i, radius);
            }

            for (int f = 0; f < frames; ++f) {
                const size_t fi = static_cast<size_t>(f);
                const std::complex<double> s0(s0r[fi], s0i[fi]);
                const std::complex<double> s1(s1r[fi], s1i[fi]);
                std::complex<double> h0, h1;
                double explained = 0.0;
                if (refChannels == 1) {
                    const double lambda = kRidge * r00[fi] + kEps;
                    h0 = s0 / (r00[fi] + lambda);
                    explained = std::real(h0 * std::conj(s0));
                } else {
                    const double trace = r00[fi] + r11[fi];
                    const double lambda = kRidge * trace + kEps;
                    const double d0 = r00[fi] + lambda;
                    const double d1 = r11[fi] + lambda;
                    const std::complex<double> c01(r01r[fi], r01i[fi]);
                    const double det = std::max(kEps, d0 * d1 - std::norm(c01));
                    h0 = (s0 * d1 - s1 * std::conj(c01)) / det;
                    h1 = (s1 * d0 - s0 * c01) / det;
                    explained = std::real(h0 * std::conj(s0) + h1 * std::conj(s1));
                }
                auto capTransfer = [](std::complex<double>& h) {
                    constexpr double kMax = 3.0;
                    const double magnitude = std::abs(h);
                    if (magnitude > kMax)
                        h *= kMax / magnitude;
                };
                capTransfer(h0);
                capTransfer(h1);
                const double coherence = std::clamp(explained / (syy[fi] + kEps), 0.0, 1.0);
                const double gate = smoothstep(kGateLow, kGateHigh, coherence);
                const std::complex<double> a0 = A[0][fi][static_cast<size_t>(k)];
                const std::complex<double> a1 =
                    refChannels > 1 ? A[1][fi][static_cast<size_t>(k)] : std::complex<double>{};
                std::complex<double> cancel = gate * (h0 * a0 + h1 * a1);
                const std::complex<double> y = Y[oc][fi][static_cast<size_t>(k)];
                const double cm = std::abs(cancel);
                const double limit = 1.08 * std::abs(y);
                if (cm > limit && limit > 0.0)
                    cancel *= limit / cm;
                Y[oc][fi][static_cast<size_t>(k)] = y - strength * cancel;
            }
        }
    }

    std::vector<Vec> timeDomain;
    timeDomain.reserve(outChannels);
    for (auto& channel : Y) {
        Vec y = istft(channel, kLosslessHop, stopToken);
        y.resize(targetLength, 0.0);
        timeDomain.push_back(std::move(y));
    }
    return timeDomain;
}

} // namespace

Vec processChannel(const Vec& song, const Vec& acc, int sr, int nFft, int hop, double strength, Algorithm algo,
                   double sigmaTime, const std::stop_token& stopToken) {
    if (song.empty() || acc.empty() || sr <= 0 || !std::isfinite(strength) || !std::isfinite(sigmaTime))
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
        Vec output = istft(Yvocal, hop, stopToken);
        output.resize(std::min(song.size(), acc.size()), 0.0);
        return output;
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
    Vec output = istft(Yvocal, hop, stopToken);
    output.resize(std::min(song.size(), acc.size()), 0.0);
    return output;
}

std::vector<Vec> processStereo(const std::vector<Vec>& song, const std::vector<Vec>& acc, int sr,
                               int nFft, int hop, double strength, Algorithm algo,
                               double sigmaTime, const std::stop_token& stopToken) {
    if (song.empty() || acc.empty() || sr <= 0 || !std::isfinite(strength) ||
        !std::isfinite(sigmaTime))
        return {};
    strength = std::clamp(strength, 0.0, 1.0);
    sigmaTime = std::max(0.0, sigmaTime);
    if (algo == Algorithm::Lossless)
        return losslessCancelStereo(song, acc, strength, sigmaTime, stopToken);

    std::vector<Vec> output;
    output.reserve(song.size());
    for (size_t c = 0; c < song.size(); ++c) {
        const Vec& reference = acc[std::min(c, acc.size() - 1U)];
        Vec channel = processChannel(song[c], reference, sr, nFft, hop, strength, algo,
                                     sigmaTime, stopToken);
        if (channel.empty())
            return {};
        output.push_back(std::move(channel));
    }
    return output;
}

} // namespace dsp
