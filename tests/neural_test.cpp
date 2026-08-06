// Neural 层单元测试: UVR model_data.json 解析, 内置模型表, demix 重叠相加骨架。
// 不依赖真实 onnx 权重: 模型回调用恒等函数 (identity), 验证分块/补零/裁剪/OLA 数学。

#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <stop_token>
#include <string>

#include "mdxnet.h"
#include "modelcatalog.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_CLOSE(a, b, tol)                                                                     \
    do {                                                                                           \
        const double av = (a);                                                                     \
        const double bv = (b);                                                                     \
        if (std::abs(av - bv) > (tol)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s == %g != %s == %g\n", __FILE__, __LINE__, #a, av, \
                         #b, bv);                                                                  \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// 恒等模型: 输出 = 输入 (人声 = 混音)。用于验证 demix 骨架的数学正确性。
bool identityModel(const float* in, float* out, int chunkSamples, std::string*) {
    for (int i = 0; i < 2 * chunkSamples; ++i)
        out[i] = in[i];
    return true;
}

void testSpecFromJson() {
    // UVR model_data.json 中 UVR-MDX-NET Main 的参数条目 (7680/3072/2^8/compensate 1.75)
    const std::string json = R"({
        "398580b6d5d973af3120df54cee6759d": {
            "compensate": 1.75,
            "mdx_dim_f_set": 3072,
            "mdx_dim_t_set": 8,
            "mdx_n_fft_scale_set": 7680,
            "primary_stem": "Vocals"
        },
        "c3b29bdce8c4fa17ec609e16220330ab": {
            "config_yaml": "model_2_stem_061321.yaml"
        }
    })";
    const auto spec = neural::mdxSpecFromJson(json, "398580b6d5d973af3120df54cee6759d");
    CHECK(spec.has_value());
    if (spec) {
        CHECK(spec->nFft == 7680);
        CHECK(spec->dimF == 3072);
        CHECK(spec->dimT == 256); // 2^8
        CHECK(spec->compensate == 1.75);
        CHECK(spec->primaryStem == "Vocals");
    }
    // 未知 md5 -> nullopt
    CHECK(!neural::mdxSpecFromJson(json, "00000000000000000000000000000000").has_value());
    // 非 JSON -> nullopt
    CHECK(!neural::mdxSpecFromJson("not json", "398580b6d5d973af3120df54cee6759d").has_value());
    // config_yaml 条目无超参 -> 返回默认 spec (mdx23 模型不受支持)
    const auto yamlSpec = neural::mdxSpecFromJson(json, "c3b29bdce8c4fa17ec609e16220330ab");
    CHECK(yamlSpec.has_value());
}

void testSpecForModelFile() {
    const auto main = neural::mdxSpecForModelFile("UVR_MDXNET_Main.onnx");
    CHECK(main.has_value());
    if (main) {
        CHECK(main->nFft == 7680);
        CHECK(main->dimF == 3072);
        CHECK(main->dimT == 256);
        CHECK(main->hop == 1024);
        CHECK(main->segmentSize == 256);
        CHECK(main->sampleRate == 44100);
    }
    CHECK(!neural::mdxSpecForModelFile("unknown.onnx").has_value());
}

void testDemixIdentity() {
    constexpr int kNFft = 7680;
    constexpr int kHop = 1024;
    constexpr int kSegment = 256;
    const int chunkSize = kHop * (kSegment - 1); // 261120
    const int genSize = chunkSize - kNFft;       // 253440

    // 长度覆盖多个 chunk 且余数非零
    const int total = genSize * 3 + 12345;
    std::vector<dsp::Vec> mix(2, dsp::Vec(static_cast<std::size_t>(total)));
    for (int c = 0; c < 2; ++c) {
        for (int k = 0; k < total; ++k)
            mix[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)] =
                0.6 * std::sin(2.0 * M_PI * 440.0 * k / 44100.0 + c) +
                0.4 * std::cos(2.0 * M_PI * 997.0 * k / 44100.0);
    }

    std::vector<dsp::Vec> vocal;
    std::string error;
    int progressCalls = 0;
    const auto progress = [&](int, int) { ++progressCalls; };
    const bool ok = neural::demixChunks(mix, kNFft, kHop, kSegment, identityModel, vocal, progress,
                                        std::stop_token{}, &error);
    CHECK(ok);
    CHECK(vocal.size() == 2);
    CHECK(vocal[0].size() == static_cast<std::size_t>(total));
    CHECK(progressCalls > 0);
    for (int c = 0; c < 2; ++c) {
        double maxErr = 0.0;
        for (int k = 0; k < total; ++k) {
            const double err =
                std::abs(vocal[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)] -
                         mix[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)]);
            maxErr = std::max(maxErr, err);
        }
        CHECK(maxErr < 1e-4); // float 往返舍入
    }
}

void testDemixExactChunk() {
    // total == genSize (整除情形: pad 恰好拼满一个完整 chunk)
    constexpr int kNFft = 7680;
    constexpr int kHop = 1024;
    constexpr int kSegment = 256;
    const int genSize = kHop * (kSegment - 1) - kNFft;
    const int total = genSize;
    std::vector<dsp::Vec> mix(2, dsp::Vec(static_cast<std::size_t>(total), 0.5));
    std::vector<dsp::Vec> vocal;
    std::string error;
    const bool ok = neural::demixChunks(mix, kNFft, kHop, kSegment, identityModel, vocal, nullptr,
                                        std::stop_token{}, &error);
    CHECK(ok);
    if (ok) {
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < total; ++k) {
                CHECK_CLOSE(vocal[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)], 0.5,
                            1e-4);
            }
        }
    }
}

void testDemixCancellation() {
    constexpr int kNFft = 7680;
    constexpr int kHop = 1024;
    constexpr int kSegment = 256;
    const int genSize = kHop * (kSegment - 1) - kNFft;
    const int total = genSize * 4;
    std::vector<dsp::Vec> mix(2, dsp::Vec(static_cast<std::size_t>(total), 0.25));

    // 第 3 个 chunk 触发取消
    std::stop_source source;
    int chunk = 0;
    const auto cancelAtThird = [&](const float* in, float* out, int n, std::string*) {
        (void)in;
        (void)out;
        (void)n;
        ++chunk;
        if (chunk == 3)
            source.request_stop();
        return true;
    };
    std::vector<dsp::Vec> vocal;
    std::string error;
    const bool ok = neural::demixChunks(mix, kNFft, kHop, kSegment, cancelAtThird, vocal, nullptr,
                                        source.get_token(), &error);
    CHECK(!ok);
    CHECK(error == "cancelled");

    // 已停止的 token: 立即失败
    std::stop_source stopped;
    stopped.request_stop();
    const bool ok2 = neural::demixChunks(mix, kNFft, kHop, kSegment, identityModel, vocal, nullptr,
                                         stopped.get_token(), &error);
    CHECK(!ok2);
}

void testDemixModelFailure() {
    constexpr int kNFft = 7680;
    constexpr int kHop = 1024;
    constexpr int kSegment = 256;
    const int genSize = kHop * (kSegment - 1) - kNFft;
    std::vector<dsp::Vec> mix(2, dsp::Vec(static_cast<std::size_t>(genSize), 0.1));
    const auto failing = [](const float*, float*, int, std::string* err) {
        if (err)
            *err = "boom";
        return false;
    };
    std::vector<dsp::Vec> vocal;
    std::string error;
    const bool ok = neural::demixChunks(mix, kNFft, kHop, kSegment, failing, vocal, nullptr,
                                        std::stop_token{}, &error);
    CHECK(!ok);
    CHECK(error == "boom");
}

void testCatalog() {
    const auto& catalog = neural::modelCatalog();
    CHECK(!catalog.empty());
    CHECK(neural::defaultModel() != nullptr);
    std::set<std::string> ids;
    std::set<std::string> files;
    bool hasDefault = false;
    for (const neural::ModelEntry& entry : catalog) {
        CHECK(!entry.id.empty());
        CHECK(!entry.fileName.empty());
        CHECK(!entry.url.empty());
        CHECK(entry.sizeBytes > 0);
        CHECK(ids.insert(entry.id).second);   // id 唯一
        CHECK(files.insert(entry.fileName).second); // 文件名唯一
        hasDefault = hasDefault || entry.defaultPick;
        // 目录内每个条目都能按 id / 文件名反查
        CHECK(neural::modelById(entry.id) == &entry);
        CHECK(neural::modelByFileName(entry.fileName) == &entry);
    }
    CHECK(hasDefault); // 至少一个默认 (系列最强)
    CHECK(neural::modelById("no_such_model") == nullptr);
    CHECK(neural::modelByFileName("no_such_file.onnx") == nullptr);
    // 目录条目的 spec 均能在内置文件名表解析 (引擎可加载)
    for (const neural::ModelEntry& entry : catalog) {
        const auto spec = neural::mdxSpecForModelFile(entry.fileName);
        CHECK(spec.has_value());
    }
}

} // namespace

int main(int argc, char** argv) {
    testSpecFromJson();
    testSpecForModelFile();
    testCatalog();
    testDemixIdentity();
    testDemixExactChunk();
    testDemixCancellation();
    testDemixModelFailure();

    // 若提供真实 model_data.json, 验证完整文件可解析且包含已知 md5 条目
    if (argc > 1) {
        const std::string text = readFile(argv[1]);
        if (!text.empty()) {
            const auto spec =
                neural::mdxSpecFromJson(text, "398580b6d5d973af3120df54cee6759d");
            CHECK(spec.has_value());
            if (spec) {
                CHECK(spec->nFft == 7680);
                CHECK(spec->dimF == 3072);
                CHECK(spec->dimT == 256);
            }
        }
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("neural: all checks passed\n");
    return 0;
}
