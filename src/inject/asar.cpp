#include "asar.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include "../util/strutil.h"

namespace inject {
namespace {

constexpr uint64_t kBlockSize = 4 * 1024 * 1024;
constexpr ULONG kMaxUlong = 0xffffffffUL;

bool NtSuccess(NTSTATUS status) { return status >= 0; }

using OpenAlgorithm = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG);
using CreateHash = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, PUCHAR, ULONG,
                                     PUCHAR, ULONG, ULONG);
using HashData = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
using FinishHash = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
using CloseAlgorithm = NTSTATUS(WINAPI*)(BCRYPT_ALG_HANDLE, ULONG);
using DestroyHash = NTSTATUS(WINAPI*)(BCRYPT_HASH_HANDLE);

struct HashHandleDeleter {
    DestroyHash destroy = nullptr;
    void operator()(BCRYPT_HASH_HANDLE handle) const {
        if (handle && destroy) destroy(handle);
    }
};
using UniqueHash = std::unique_ptr<std::remove_pointer_t<BCRYPT_HASH_HANDLE>, HashHandleDeleter>;

class Sha256Api {
public:
    bool Load() {
        module_.reset(LoadLibraryW(L"bcrypt.dll"));
        if (!module_) return false;
        open_ = Resolve<OpenAlgorithm>("BCryptOpenAlgorithmProvider");
        create_ = Resolve<CreateHash>("BCryptCreateHash");
        data_ = Resolve<HashData>("BCryptHashData");
        finish_ = Resolve<FinishHash>("BCryptFinishHash");
        close_ = Resolve<CloseAlgorithm>("BCryptCloseAlgorithmProvider");
        destroy_ = Resolve<DestroyHash>("BCryptDestroyHash");
        return open_ && create_ && data_ && finish_ && close_ && destroy_;
    }

    ~Sha256Api() {
        if (algorithm_) close_(algorithm_, 0);
    }

    bool Open() {
        return open_ && NtSuccess(open_(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr,
                                         BCRYPT_HASH_REUSABLE_FLAG));
    }

    bool Hash(const uint8_t* bytes, size_t size, std::array<uint8_t, 32>& digest,
              std::wstring& err) const {
        BCRYPT_HASH_HANDLE raw = nullptr;
        if (!NtSuccess(create_(algorithm_, &raw, nullptr, 0, nullptr, 0,
                                BCRYPT_HASH_REUSABLE_FLAG))) {
            err = L"BCryptCreateHash 失败";
            return false;
        }
        UniqueHash hash(raw, HashHandleDeleter{destroy_});
        while (size > 0) {
            const ULONG chunk = static_cast<ULONG>(size > kMaxUlong ? kMaxUlong : size);
            PUCHAR input = reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(bytes));
            if (!NtSuccess(data_(raw, input, chunk, 0))) {
                err = L"BCryptHashData 失败";
                return false;
            }
            bytes += chunk;
            size -= chunk;
        }
        if (!NtSuccess(finish_(raw, reinterpret_cast<PUCHAR>(digest.data()),
                                static_cast<ULONG>(digest.size()), 0))) {
            err = L"BCryptFinishHash 失败";
            return false;
        }
        return true;
    }

private:
    struct ModuleDeleter {
        void operator()(HMODULE module) const { if (module) FreeLibrary(module); }
    };

    template <typename T>
    T Resolve(const char* name) const {
        return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module_.get(), name)));
    }

    std::unique_ptr<std::remove_pointer_t<HMODULE>, ModuleDeleter> module_;
    OpenAlgorithm open_ = nullptr;
    CreateHash create_ = nullptr;
    HashData data_ = nullptr;
    FinishHash finish_ = nullptr;
    CloseAlgorithm close_ = nullptr;
    DestroyHash destroy_ = nullptr;
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
};

std::string LowerHex(const std::array<uint8_t, 32>& digest) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (uint8_t byte : digest) {
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0f];
    }
    return hex;
}

bool Sha256(const Sha256Api& api, const std::string& bytes, size_t offset, size_t count,
            std::string& hex, std::wstring& err) {
    std::array<uint8_t, 32> digest{};
    if (!api.Hash(reinterpret_cast<const uint8_t*>(bytes.data()) + offset, count, digest, err))
        return false;
    hex = LowerHex(digest);
    return true;
}

struct Node {
    bool isFile = false;
    std::string fileJson;
    // map 提供确定性的键顺序；实际文件体偏移始终按输入文件顺序计算。
    std::map<std::string, Node> files;
};

std::string EscapeJsonString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

void AppendNode(std::string& json, const Node& node) {
    if (node.isFile) {
        json += node.fileJson;
        return;
    }
    json += "{\"files\":{";
    bool first = true;
    for (const auto& entry : node.files) {
        if (!first) json += ',';
        first = false;
        json += EscapeJsonString(entry.first);
        json += ':';
        AppendNode(json, entry.second);
    }
    json += "}}";
}

void AppendU32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xff));
}

} // namespace

std::string AsarPack(const std::vector<AsarFile>& files, std::wstring& err) {
    err.clear();
    Sha256Api api;
    if (!api.Load() || !api.Open()) {
        err = L"无法动态加载 bcrypt.dll 的 SHA256 接口";
        return {};
    }

    try {
        Node root;
        std::string body;
        for (const auto& file : files) {
            if (file.path.empty() || file.path.front() == '/' || file.path.back() == '/') {
                err = L"无效的 ASAR 路径: " + su::Utf8ToW(file.path);
                return {};
            }

            Node* current = &root;
            size_t begin = 0;
            while (true) {
                const size_t slash = file.path.find('/', begin);
                const std::string part = file.path.substr(begin, slash == std::string::npos
                                                                      ? std::string::npos : slash - begin);
                if (part.empty() || part == "." || part == "..") {
                    err = L"无效的 ASAR 路径: " + su::Utf8ToW(file.path);
                    return {};
                }
                const bool last = slash == std::string::npos;
                if (current->isFile) {
                    err = L"ASAR 路径冲突: " + su::Utf8ToW(file.path);
                    return {};
                }
                auto& child = current->files[part];
                if (last) {
                    if (child.isFile || !child.files.empty()) {
                        err = L"ASAR 重复文件或路径冲突: " + su::Utf8ToW(file.path);
                        return {};
                    }
                    child.isFile = true;
                    std::string wholeHash;
                    if (!Sha256(api, file.bytes, 0, file.bytes.size(), wholeHash, err)) return {};

                    std::string blocks = "[";
                    bool firstBlock = true;
                    for (size_t offset = 0; offset < file.bytes.size();) {
                        const size_t count = static_cast<size_t>(
                            (file.bytes.size() - offset > kBlockSize) ? kBlockSize : file.bytes.size() - offset);
                        std::string blockHash;
                        if (!Sha256(api, file.bytes, offset, count, blockHash, err)) return {};
                        if (!firstBlock) blocks += ',';
                        firstBlock = false;
                        blocks += '"' + blockHash + '"';
                        offset += count;
                    }
                    blocks += ']';
                    child.fileJson = "{\"size\":" + std::to_string(file.bytes.size()) +
                        ",\"offset\":\"" + std::to_string(body.size()) +
                        "\",\"integrity\":{\"algorithm\":\"SHA256\",\"hash\":\"" + wholeHash +
                        "\",\"blockSize\":" + std::to_string(kBlockSize) +
                        ",\"blocks\":" + blocks + "}}";
                    body.append(file.bytes);
                    break;
                }
                current = &child;
                begin = slash + 1;
            }
        }

        std::string json = "{\"files\":{";
        bool first = true;
        for (const auto& entry : root.files) {
            if (!first) json += ',';
            first = false;
            json += EscapeJsonString(entry.first);
            json += ':';
            AppendNode(json, entry.second);
        }
        json += "}}";

        const size_t padding = (4 - (json.size() % 4)) % 4;
        const size_t payloadSize = 4 + json.size() + padding;
        const size_t headerPickleLen = 4 + payloadSize;
        const size_t archiveSize = 8 + headerPickleLen + body.size();
        if (json.size() > 0xffffffffULL ||
            payloadSize > 0xffffffffULL ||
            headerPickleLen > 0xffffffffULL) {
            err = L"ASAR 头过大";
            return {};
        }

        std::string archive;
        archive.reserve(archiveSize);
        AppendU32(archive, 4);
        AppendU32(archive, static_cast<uint32_t>(headerPickleLen));
        AppendU32(archive, static_cast<uint32_t>(payloadSize));
        AppendU32(archive, static_cast<uint32_t>(json.size()));
        archive += json;
        archive.append(padding, '\0');
        archive += body;
        return archive;
    } catch (const std::bad_alloc&) {
        err = L"内存不足，无法生成 ASAR";
        return {};
    } catch (const std::exception& ex) {
        err = L"生成 ASAR 失败: " + su::Utf8ToW(ex.what());
        return {};
    }
}

} // namespace inject
