#pragma once

#include <complex>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace dsp {

using Vec = std::vector<double>;
using CVec = std::vector<std::complex<double>>;

enum class Algorithm {
    Lossless,
    SoftMask,
    SpectralSubtraction,
    WienerFilter,
    FrequencyWeighted,
    BinaryMask,
    PhaseSensitive,
};

[[nodiscard]] std::optional<Algorithm> algorithmFromString(std::string_view value);
[[nodiscard]] std::string_view algorithmKey(Algorithm algorithm);

// 周期 Hann 窗: w[k] = 0.5*(1 - cos(2*pi*k/n)), k = 0..n-1
Vec hann(int n);

// numpy 'reflect' 填充索引映射（越界迭代映射直至落回 [0, n)）
int reflectIndex(int i, int n);

// 双侧 reflect 填充 nFft/2，并向右补足最后一帧，避免重建时丢失尾部不足 hop 的样本。
// 返回 frame-major: frames[f][k], bins = nFft/2 + 1
std::vector<CVec> stft(const Vec& x, int nFft, int hop,
                       const std::stop_token& stopToken = {});

// librosa.istft 等价 (length=None, center=True): OLA + window^2 归一化,
// 输出长度 = hop*(nFrames-1)
Vec istft(const std::vector<CVec>& frames, int hop, const std::stop_token& stopToken = {});

// f[k] = sr*k/nFft, k = 0..nFft/2
std::vector<double> fftFrequencies(int sr, int nFft);

// scipy.ndimage.gaussian_filter(order=0, truncate=4.0, mode='reflect') 等价
// data 为行主序 rows*cols 缓冲; sigmaRow 沿行(轴0), sigmaCol 沿列(轴1)
void gaussianFilter2D(Vec& data, int rows, int cols, double sigmaRow, double sigmaCol,
                      const std::stop_token& stopToken = {});

// 镜像参考 align_audio: 30s 窗口单声道互相关, lag = argmax - (M-1)
std::vector<Vec> alignAudio(const std::vector<Vec>& song, const std::vector<Vec>& acc, int sr,
                            const std::stop_token& stopToken = {});

// 立体声联合处理。Lossless 使用左右声道共同估计 2x2 参考传递矩阵，其他算法保持
// 逐声道兼容行为。输入声道数允许为 1 或 2，输出声道数与 song 一致。
std::vector<Vec> processStereo(const std::vector<Vec>& song, const std::vector<Vec>& acc, int sr,
                               int nFft, int hop, double strength, Algorithm algo,
                               double sigmaTime, const std::stop_token& stopToken = {});

// 镜像参考 process_channel (strength 已归一化到 [0,1])
Vec processChannel(const Vec& song, const Vec& acc, int sr, int nFft, int hop, double strength,
                   Algorithm algo, double sigmaTime,
                   const std::stop_token& stopToken = {});

} // namespace dsp
