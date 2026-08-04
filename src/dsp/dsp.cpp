#include "dsp.h"

#include <algorithm>
#include <cmath>
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

Vec processChannel(const Vec& song, const Vec& acc, int sr, int nFft, int hop, double strength,
                   Algorithm algo, double sigmaTime, const std::stop_token& stopToken) {
    if (song.empty() || acc.empty() || sr <= 0 || !std::isfinite(strength) ||
        !std::isfinite(sigmaTime))
        return {};
    strength = std::clamp(strength, 0.0, 1.0);
    sigmaTime = std::max(0.0, sigmaTime);
    if (algo == Algorithm::Lossless) {
        constexpr int kNFft = 4096;
        constexpr int kHop = 1024;
        auto Y_mix = stft(song, kNFft, kHop, stopToken);
        auto Y_inst = stft(acc, kNFft, kHop, stopToken);
        const size_t minCols = std::min(Y_mix.size(), Y_inst.size());
        if (minCols == 0)
            return {};
        Y_mix.resize(minCols);
        Y_inst.resize(minCols);
        const int bins = static_cast<int>(Y_mix[0].size());
        const int cols = static_cast<int>(minCols);
        // 2D 缓冲按行主序 (rows=bins, cols=frames) 存放, 索引 k*cols + f
        Vec pmixRe(static_cast<size_t>(bins) * static_cast<size_t>(cols));
        Vec pmixIm(static_cast<size_t>(bins) * static_cast<size_t>(cols));
        Vec pinstRe(static_cast<size_t>(bins) * static_cast<size_t>(cols));
        for (int f = 0; f < cols; ++f) {
            if ((f & 63) == 0 && stopToken.stop_requested())
                return {};
            for (int k = 0; k < bins; ++k) {
                const std::complex<double> mix = Y_mix[static_cast<size_t>(f)][static_cast<size_t>(k)];
                const std::complex<double> inst = Y_inst[static_cast<size_t>(f)][static_cast<size_t>(k)];
                const std::complex<double> pm = mix * std::conj(inst);
                const size_t idx = static_cast<size_t>(k) * static_cast<size_t>(cols) + static_cast<size_t>(f);
                pmixRe[idx] = pm.real();
                pmixIm[idx] = pm.imag();
                pinstRe[idx] = std::norm(inst);
            }
        }
        gaussianFilter2D(pmixRe, bins, cols, 1.0, sigmaTime, stopToken);
        gaussianFilter2D(pmixIm, bins, cols, 1.0, sigmaTime, stopToken);
        gaussianFilter2D(pinstRe, bins, cols, 1.0, sigmaTime, stopToken);
        if (stopToken.stop_requested())
            return {};
        constexpr double kEpsilon = 1e-10;
        std::vector<CVec> Yvocal(static_cast<size_t>(cols), CVec(static_cast<size_t>(bins)));
        for (int f = 0; f < cols; ++f) {
            if ((f & 63) == 0 && stopToken.stop_requested())
                return {};
            for (int k = 0; k < bins; ++k) {
                const size_t idx = static_cast<size_t>(k) * static_cast<size_t>(cols) + static_cast<size_t>(f);
                const double denom = pinstRe[idx] + kEpsilon;
                const double hr = pmixRe[idx] / denom;
                const double hi = pmixIm[idx] / denom;
                double hm = std::hypot(hr, hi);
                hm = std::clamp(hm, 0.0, 2.0);
                const double hp = std::atan2(hi, hr);
                const std::complex<double> H(hm * std::cos(hp), hm * std::sin(hp));
                const std::complex<double> Yest = H * Y_inst[static_cast<size_t>(f)][static_cast<size_t>(k)];
                Yvocal[static_cast<size_t>(f)][static_cast<size_t>(k)] =
                    (Y_mix[static_cast<size_t>(f)][static_cast<size_t>(k)] - strength * Yest) * 2.0;
            }
        }
        return istft(Yvocal, kHop, stopToken);
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
