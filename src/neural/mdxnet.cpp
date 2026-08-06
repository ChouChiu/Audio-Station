#include "mdxnet.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <memory>
#include <utility>

namespace neural {
namespace {

// 最小 JSON 解析器 (仅解析, 不做序列化)。用于读取 UVR model_data.json。
namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    // unique_ptr 允许 Value 在自身定义内递归 (std::pair<..., Value> 需完整类型)
    std::vector<std::pair<std::string, std::unique_ptr<Value>>> object;

    [[nodiscard]] const Value* find(const std::string& key) const {
        if (type != Type::Object)
            return nullptr;
        for (const auto& [k, v] : object) {
            if (k == key)
                return v.get();
        }
        return nullptr;
    }
};

struct Parser {
    const std::string& text;
    std::size_t pos = 0;
    std::string* errorOut;

    [[nodiscard]] bool fail(const std::string& msg) {
        if (errorOut)
            *errorOut = msg + " at offset " + std::to_string(pos);
        return false;
    }

    void skipWs() {
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }

    [[nodiscard]] bool parseValue(Value& out) {
        skipWs();
        if (pos >= text.size())
            return fail("unexpected end of input");
        switch (text[pos]) {
        case '{':
            return parseObject(out);
        case '[':
            return parseArray(out);
        case '"':
            return parseString(out.string) && (out.type = Value::Type::String, true);
        case 't':
            return literal("true") && (out.type = Value::Type::Bool, out.boolean = true, true);
        case 'f':
            return literal("false") && (out.type = Value::Type::Bool, out.boolean = false, true);
        case 'n':
            return literal("null") && (out.type = Value::Type::Null, true);
        default:
            return parseNumber(out);
        }
    }

    [[nodiscard]] bool literal(const char* lit) {
        const std::size_t len = std::char_traits<char>::length(lit);
        if (text.compare(pos, len, lit) != 0)
            return fail("invalid literal");
        pos += len;
        return true;
    }

    [[nodiscard]] bool parseString(std::string& out) {
        if (pos >= text.size() || text[pos] != '"')
            return fail("expected string");
        ++pos;
        out.clear();
        while (pos < text.size()) {
            const char c = text[pos++];
            if (c == '"')
                return true;
            if (c == '\\') {
                if (pos >= text.size())
                    return fail("unterminated escape");
                const char e = text[pos++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > text.size())
                        return fail("truncated unicode escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text[pos + i];
                        code <<= 4;
                        if (h >= '0' && h <= '9')
                            code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            code |= static_cast<unsigned>(h - 'A' + 10);
                        else
                            return fail("invalid unicode escape");
                    }
                    pos += 4;
                    // 仅处理 BMP 基本平面 (model_data.json 无代理对)
                    if (code >= 0xD800 && code <= 0xDFFF)
                        return fail("unsupported surrogate pair");
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else if (code < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: return fail("invalid escape");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    [[nodiscard]] bool parseNumber(Value& out) {
        const std::size_t start = pos;
        if (pos < text.size() && text[pos] == '-')
            ++pos;
        while (pos < text.size() && (text[pos] >= '0' && text[pos] <= '9'))
            ++pos;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && (text[pos] >= '0' && text[pos] <= '9'))
                ++pos;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
                ++pos;
            while (pos < text.size() && (text[pos] >= '0' && text[pos] <= '9'))
                ++pos;
        }
        if (pos == start || (pos == start + 1 && text[start] == '-'))
            return fail("invalid number");
        out.type = Value::Type::Number;
        out.number = std::strtod(text.substr(start, pos - start).c_str(), nullptr);
        return true;
    }

    [[nodiscard]] bool parseArray(Value& out) {
        ++pos; // '['
        out.type = Value::Type::Array;
        skipWs();
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            return true;
        }
        while (true) {
            Value item;
            if (!parseValue(item))
                return false;
            out.array.push_back(std::move(item));
            skipWs();
            if (pos >= text.size())
                return fail("unterminated array");
            if (text[pos] == ']') {
                ++pos;
                return true;
            }
            if (text[pos] != ',')
                return fail("expected ',' or ']'");
            ++pos;
        }
    }

    [[nodiscard]] bool parseObject(Value& out) {
        ++pos; // '{'
        out.type = Value::Type::Object;
        skipWs();
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        while (true) {
            skipWs();
            if (pos >= text.size() || text[pos] != '"')
                return fail("expected object key");
            std::string key;
            if (!parseString(key))
                return false;
            skipWs();
            if (pos >= text.size() || text[pos] != ':')
                return fail("expected ':'");
            ++pos;
            Value item;
            if (!parseValue(item))
                return false;
            out.object.emplace_back(std::move(key),
                                    std::make_unique<Value>(std::move(item)));
            skipWs();
            if (pos >= text.size())
                return fail("unterminated object");
            if (text[pos] == '}') {
                ++pos;
                return true;
            }
            if (text[pos] != ',')
                return fail("expected ',' or '}'");
            ++pos;
        }
    }
};

std::optional<Value> parse(const std::string& text, std::string* errorOut) {
    Parser parser{.text = text, .pos = 0, .errorOut = errorOut};
    Value root;
    if (!parser.parseValue(root))
        return std::nullopt;
    parser.skipWs();
    if (parser.pos != text.size()) {
        if (errorOut)
            *errorOut = "trailing content at offset " + std::to_string(parser.pos);
        return std::nullopt;
    }
    return root;
}

} // namespace json

// numpy hanning(M) 等价 (对称 Hann, M-1 分母; M==1 特例返回 [1.0])
void hannSymmetric(int m, std::vector<double>& out) {
    out.resize(static_cast<std::size_t>(m));
    if (m <= 1) {
        if (m == 1)
            out[0] = 1.0;
        return;
    }
    for (int k = 0; k < m; ++k)
        out[static_cast<std::size_t>(k)] =
            0.5 * (1.0 - std::cos(2.0 * M_PI * k / (m - 1)));
}

} // namespace

std::optional<MdxNetSpec> mdxSpecFromJson(const std::string& jsonText,
                                          const std::string& modelMd5) {
    std::string parseError;
    const std::optional<json::Value> root = json::parse(jsonText, &parseError);
    if (!root || root->type != json::Value::Type::Object)
        return std::nullopt;
    const json::Value* entry = root->find(modelMd5);
    if (!entry || entry->type != json::Value::Type::Object)
        return std::nullopt;

    MdxNetSpec spec;
    const auto readNumber = [entry](const char* key, double* out) {
        if (const json::Value* v = entry->find(key);
            v && v->type == json::Value::Type::Number) {
            *out = v->number;
            return true;
        }
        return false;
    };
    double value = 0.0;
    if (readNumber("mdx_n_fft_scale_set", &value))
        spec.nFft = static_cast<int>(value);
    if (readNumber("mdx_dim_f_set", &value))
        spec.dimF = static_cast<int>(value);
    if (readNumber("mdx_dim_t_set", &value))
        spec.dimT = 1 << static_cast<int>(value); // UVR 表存指数: dim_t = 2^set
    if (readNumber("compensate", &value))
        spec.compensate = value;
    if (const json::Value* v = entry->find("primary_stem");
        v && v->type == json::Value::Type::String)
        spec.primaryStem = v->string;
    return spec;
}

std::optional<MdxNetSpec> mdxSpecForModelFile(const std::string& fileName) {
    // UVR 经典 MDX-Net 模型 (UVR 仓库 model_data.json 数值, 引擎参数 hop=1024/segment=256)
    if (fileName == "UVR_MDXNET_Main.onnx")
        return MdxNetSpec{.nFft = 7680,    .dimF = 3072, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.75,  .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    if (fileName == "UVR_MDXNET_1_9703.onnx")
        return MdxNetSpec{.nFft = 6144,    .dimF = 2048, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    if (fileName == "UVR_MDXNET_2_9682.onnx")
        return MdxNetSpec{.nFft = 6144,    .dimF = 2048, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    if (fileName == "UVR_MDXNET_3_9662.onnx")
        return MdxNetSpec{.nFft = 6144,    .dimF = 2048, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    if (fileName == "UVR-MDX-NET-Inst_Main.onnx")
        return MdxNetSpec{.nFft = 7680,    .dimF = 3072, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Instrumental"};
    // Kim Vocal 1/2: 与 Main 相同架构 (66.7MB, [1,4,3072,256])
    if (fileName == "Kim_Vocal_1.onnx" || fileName == "Kim_Vocal_2.onnx")
        return MdxNetSpec{.nFft = 7680,    .dimF = 3072, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    // kuielab a/b vocals: 与 UVR-MDX-NET 1 相同架构 (29.7MB, [1,4,2048,256])
    if (fileName == "kuielab_a_vocals.onnx" || fileName == "kuielab_b_vocals.onnx")
        return MdxNetSpec{.nFft = 6144,    .dimF = 2048, .dimT = 256, .hop = 1024,
                          .segmentSize = 256, .compensate = 1.035, .sampleRate = 44100,
                          .primaryStem = "Vocals"};
    return std::nullopt;
}

bool demixChunks(const std::vector<dsp::Vec>& mix, int nFft, int hop, int segmentSize,
                 const ChunkInferFn& modelFn, std::vector<dsp::Vec>& vocal,
                 const std::function<void(int current, int total)>& progress,
                 const std::stop_token& stopToken, std::string* errorOut) {
    const int chunkSize = hop * (segmentSize - 1);
    const int trim = nFft / 2;
    const int genSize = chunkSize - 2 * trim;
    const int step = chunkSize - nFft; // UVR overlap == "Default" => step = chunk - n_fft
    const int total = static_cast<int>(mix[0].size());
    if (total <= 0) {
        vocal.assign(2, {});
        return true;
    }
    const int pad = genSize + trim - (total % genSize);
    const int mixtureLen = trim + total + pad;

    std::vector<dsp::Vec> result(2, dsp::Vec(static_cast<std::size_t>(mixtureLen), 0.0));
    std::vector<dsp::Vec> divider(2, dsp::Vec(static_cast<std::size_t>(mixtureLen), 0.0));
    std::vector<float> inBuf(2 * static_cast<std::size_t>(chunkSize), 0.0f);
    std::vector<float> outBuf(2 * static_cast<std::size_t>(chunkSize), 0.0f);

    const int totalChunks = (mixtureLen + step - 1) / step;
    int chunkIndex = 0;
    for (int start = 0; start < mixtureLen; start += step, ++chunkIndex) {
        if (stopToken.stop_requested()) {
            if (errorOut)
                *errorOut = "cancelled";
            return false;
        }
        const int end = std::min(start + chunkSize, mixtureLen);
        const int actual = end - start;

        std::vector<double> window;
        hannSymmetric(actual, window);

        for (int c = 0; c < 2; ++c) {
            float* dst = inBuf.data() + static_cast<std::size_t>(c) * chunkSize;
            for (int k = 0; k < actual; ++k) {
                // mixture = [trim 零 | mix | pad 零]: 超出 mix 范围的样本取 0
                const int p = start + k;
                dst[k] = (p >= trim && p < trim + total)
                             ? static_cast<float>(
                                   mix[static_cast<std::size_t>(c)]
                                      [static_cast<std::size_t>(p - trim)])
                             : 0.0f;
            }
        }
        if (!modelFn(inBuf.data(), outBuf.data(), chunkSize, errorOut))
            return false;

        for (int c = 0; c < 2; ++c) {
            const std::size_t channel = static_cast<std::size_t>(c);
            const float* src = outBuf.data() + channel * static_cast<std::size_t>(chunkSize);
            dsp::Vec& res = result[channel];
            dsp::Vec& div = divider[channel];
            for (int k = 0; k < actual; ++k) {
                const std::size_t pos = static_cast<std::size_t>(start) + static_cast<std::size_t>(k);
                const double w = window[static_cast<std::size_t>(k)];
                res[pos] += w * src[k];
                div[pos] += w;
            }
        }
        if (progress)
            progress(chunkIndex + 1, totalChunks);
    }

    vocal.assign(2, dsp::Vec(static_cast<std::size_t>(total), 0.0));
    for (int c = 0; c < 2; ++c) {
        const std::size_t channel = static_cast<std::size_t>(c);
        const dsp::Vec& res = result[channel];
        const dsp::Vec& div = divider[channel];
        dsp::Vec& v = vocal[channel];
        for (int k = 0; k < total; ++k) {
            const std::size_t pos = static_cast<std::size_t>(trim) + static_cast<std::size_t>(k);
            v[static_cast<std::size_t>(k)] = res[pos] / div[pos];
        }
    }
    return true;
}

bool MdxNet::load(const std::string& onnxPath, const MdxNetSpec& spec, std::string* errorOut) {
    m_spec = spec;
    const int chunkSize = m_spec.hop * (m_spec.segmentSize - 1);
    m_chunkSize = chunkSize;
    m_trim = m_spec.nFft / 2;
    m_nBins = m_spec.nFft / 2 + 1;
    if (chunkSize % m_spec.hop != 0 || chunkSize / m_spec.hop + 1 != m_spec.dimT) {
        if (errorOut)
            *errorOut = "spec inconsistent: n_fft/hop/segment do not yield dimT frames";
        return false;
    }
    if (!m_session.load(onnxPath, errorOut))
        return false;
    // inputShape() = {channels, freq, frames}; 要求 4 通道且频率/帧数与超参一致
    const std::vector<int64_t> actual = m_session.inputShape();
    if (actual.size() != 3 || actual[0] != 4 || actual[1] != m_spec.dimF ||
        actual[2] != m_spec.dimT) {
        if (errorOut) {
            *errorOut = "model input shape mismatch: expected [1,4," +
                        std::to_string(m_spec.dimF) + "," + std::to_string(m_spec.dimT) +
                        "], got [1,4," + std::to_string(actual[1]) + "," +
                        std::to_string(actual[2]) + "]";
        }
        m_session = OnnxSession();
        return false;
    }
    return true;
}

bool MdxNet::isLoaded() const noexcept {
    return m_session.isLoaded();
}

bool MdxNet::separate(const std::vector<dsp::Vec>& mix, std::vector<dsp::Vec>& vocal,
                      const std::function<void(int current, int total)>& progress,
                      const std::stop_token& stopToken, std::string* errorOut) {
    if (!isLoaded()) {
        if (errorOut)
            *errorOut = "model not loaded";
        return false;
    }
    if (mix.size() < 2 || mix[0].empty()) {
        if (errorOut)
            *errorOut = "mix must contain two non-empty channels";
        return false;
    }
    if (mix[0].size() != mix[1].size()) {
        if (errorOut)
            *errorOut = "mix channels differ in length";
        return false;
    }
    const auto infer = [this](const float* in, float* out, int chunkSamples,
                              std::string* err) { return inferChunk(in, out, chunkSamples, err); };
    if (!demixChunks(mix, m_spec.nFft, m_spec.hop, m_spec.segmentSize, infer, vocal, progress,
                     stopToken, errorOut))
        return false;
    if (m_spec.compensate != 1.0) {
        for (dsp::Vec& channel : vocal) {
            for (double& x : channel)
                x *= m_spec.compensate;
        }
    }
    return true;
}

bool MdxNet::inferChunk(const float* in, float* out, int chunkSamples, std::string* errorOut) {
    const int nBins = m_nBins;
    const int dimF = m_spec.dimF;
    const int hop = m_spec.hop;
    const int nFft = m_spec.nFft;

    const dsp::Vec ch0(in, in + chunkSamples);
    const dsp::Vec ch1(in + static_cast<std::ptrdiff_t>(chunkSamples),
                       in + 2 * static_cast<std::ptrdiff_t>(chunkSamples));
    const std::vector<dsp::CVec> frames0 = dsp::stft(ch0, nFft, hop);
    const std::vector<dsp::CVec> frames1 = dsp::stft(ch1, nFft, hop);
    const int frames = static_cast<int>(frames0.size());

    // [4][dimF][frames]: 0=c0r, 1=c0i, 2=c1r, 3=c1i (UVR STFT 通道交织布局)
    const std::ptrdiff_t freqStride = frames;
    const std::ptrdiff_t channelStride = static_cast<std::ptrdiff_t>(dimF) * freqStride;
    const std::size_t specSize = static_cast<std::size_t>(4 * channelStride);
    std::vector<float> specIn(specSize);
    std::vector<float> specOut(specSize);
    const auto interleave = [&](const std::vector<dsp::CVec>& f, int channel) {
        for (int t = 0; t < frames; ++t) {
            for (int k = 0; k < dimF; ++k) {
                const std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(channel) * channelStride +
                                           static_cast<std::ptrdiff_t>(k) * freqStride + t;
                specIn[static_cast<std::size_t>(idx)] =
                    static_cast<float>(f[static_cast<std::size_t>(t)]
                                           [static_cast<std::size_t>(k)]
                                               .real());
            }
        }
    };
    const auto interleaveImag = [&](const std::vector<dsp::CVec>& f, int channel) {
        for (int t = 0; t < frames; ++t) {
            for (int k = 0; k < dimF; ++k) {
                const std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(channel) * channelStride +
                                           static_cast<std::ptrdiff_t>(k) * freqStride + t;
                specIn[static_cast<std::size_t>(idx)] =
                    static_cast<float>(f[static_cast<std::size_t>(t)]
                                           [static_cast<std::size_t>(k)]
                                               .imag());
            }
        }
    };
    interleave(frames0, 0);
    interleaveImag(frames0, 1);
    interleave(frames1, 2);
    interleaveImag(frames1, 3);

    // UVR: spek[:, :, :3, :] *= 0 (零化前 3 个频点)
    for (int ch = 0; ch < 4; ++ch) {
        for (int k = 0; k < 3 && k < dimF; ++k) {
            const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(ch) * channelStride +
                                          static_cast<std::ptrdiff_t>(k) * freqStride;
            std::fill(specIn.begin() + offset, specIn.begin() + offset + freqStride, 0.0f);
        }
    }

    if (!m_session.run(specIn.data(), specIn.size(), specOut.data(), specOut.size(), errorOut))
        return false;

    // 反交织 + 频域补零 (k >= dimF 保持 0)
    std::vector<dsp::CVec> outF0(static_cast<std::size_t>(frames),
                                 dsp::CVec(static_cast<std::size_t>(nBins)));
    std::vector<dsp::CVec> outF1(static_cast<std::size_t>(frames),
                                 dsp::CVec(static_cast<std::size_t>(nBins)));
    for (int t = 0; t < frames; ++t) {
        for (int k = 0; k < dimF; ++k) {
            const std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(k) * freqStride + t;
            outF0[static_cast<std::size_t>(t)][static_cast<std::size_t>(k)] = std::complex<double>(
                specOut[static_cast<std::size_t>(idx)],
                specOut[static_cast<std::size_t>(channelStride + idx)]);
            outF1[static_cast<std::size_t>(t)][static_cast<std::size_t>(k)] = std::complex<double>(
                specOut[static_cast<std::size_t>(2 * channelStride + idx)],
                specOut[static_cast<std::size_t>(3 * channelStride + idx)]);
        }
    }

    const dsp::Vec w0 = dsp::istft(outF0, hop);
    const dsp::Vec w1 = dsp::istft(outF1, hop);
    if (w0.size() < static_cast<std::size_t>(chunkSamples) ||
        w1.size() < static_cast<std::size_t>(chunkSamples)) {
        if (errorOut)
            *errorOut = "istft output shorter than chunk";
        return false;
    }
    for (int k = 0; k < chunkSamples; ++k) {
        out[k] = static_cast<float>(w0[static_cast<std::size_t>(k)]);
        out[chunkSamples + k] = static_cast<float>(w1[static_cast<std::size_t>(k)]);
    }
    return true;
}

} // namespace neural
