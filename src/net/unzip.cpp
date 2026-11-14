// unzip.cpp — GitHub zip 解压：中央目录解析 + 自实现 RFC1951 inflate（puff 思路）。
// 零外部依赖。支持 store(0) 与 deflate(8)。防路径穿越。
#include "unzip.h"
#include "../util/strutil.h"
#include "../util/logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace net {

// ================= puff：RFC1951 inflate =================
// 参考公开领域实现思路（zlib 的 puff.c），重写为紧凑版本。
namespace puff {

struct Bits {
    const unsigned char* in;
    size_t inlen;
    size_t incnt = 0;
    int bitbuf = 0;
    int bitcnt = 0;

    int BitsPull(int need)
    {
        long val = bitbuf;
        while (bitcnt < need) {
            if (incnt == inlen) return -1;
            val |= (long)(in[incnt++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = (int)(val >> need);
        bitcnt -= need;
        return (int)(val & ((1L << need) - 1));
    }

    void BitsUndo(int unusedBits)
    {
        bitbuf <<= unusedBits;
        bitcnt += unusedBits;
    }

    // Huffman 码表：count[0..15] 为各长度码数，symbol 为按码长排序的符号
    struct Huffman {
        short* count;
        short* symbol;
    };

    int Decode(const Huffman& h)
    {
        int len = 1, code = 0, first = 0, index = 0;
        while (len <= 15) {
            int b = BitsPull(1);
            if (b < 0) return -1;
            code |= b;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            ++len;
        }
        return -10;
    }

    int Construct(Huffman& h, const short* length, int n)
    {
        int symbol, len, left;
        short offs[16];
        for (len = 0; len <= 15; ++len) h.count[len] = 0;
        for (symbol = 0; symbol < n; ++symbol) h.count[length[symbol]]++;
        if (h.count[0] == n) return 0;
        left = 1;
        for (len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        offs[1] = 0;
        for (len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
        for (symbol = 0; symbol < n; ++symbol)
            if (length[symbol]) h.symbol[offs[length[symbol]]++] = (short)symbol;
        return left;
    }

    const short kLengthBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    const short kLengthExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    const short kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    const short kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

    int Codes(const Huffman& lencode, const Huffman& distcode,
              unsigned char* out, size_t outlen, size_t& outcnt)
    {
        int symbol;
        for (;;) {
            symbol = Decode(lencode);
            if (symbol < 0) return symbol;
            if (symbol < 256) {
                if (outcnt == outlen) return -22;
                out[outcnt++] = (unsigned char)symbol;
            } else if (symbol > 256) {
                symbol -= 257;
                if (symbol >= 29) return -10;
                int len = kLengthBase[symbol] + BitsPull(kLengthExtra[symbol]);
                symbol = Decode(distcode);
                if (symbol < 0) return symbol;
                if (symbol >= 30) return -10;
                int dist = kDistBase[symbol] + BitsPull(kDistExtra[symbol]);
                if (outcnt - dist < 0) return -11;
                if (outcnt + (size_t)len > outlen) return -22;
                for (int i = 0; i < len; ++i) {
                    out[outcnt] = out[outcnt - dist];
                    ++outcnt;
                }
            } else break; // 256 = 块结束
        }
        return 0;
    }

    int Stored(unsigned char* out, size_t outlen, size_t& outcnt)
    {
        BitsUndo(bitcnt & 7);
        bitbuf = 0; bitcnt = 0;
        if (outcnt + 4 > outlen) return -22;
        if (incnt + 4 > inlen) return -3;
        unsigned len = in[incnt] | (in[incnt + 1] << 8);
        unsigned nlen = in[incnt + 2] | (in[incnt + 3] << 8);
        incnt += 4;
        if ((len ^ 0xFFFF) != nlen) return -4;
        if (outcnt + len > outlen) return -22;
        if (incnt + len > inlen) return -3;
        memcpy(out + outcnt, in + incnt, len);
        outcnt += len;
        incnt += len;
        return 0;
    }

    int Fixed(Huffman& lh, Huffman& dh, unsigned char* out, size_t outlen, size_t& outcnt)
    {
        short lengths[288];
        int symbol;
        for (symbol = 0; symbol < 144; ++symbol) lengths[symbol] = 8;
        for (; symbol < 256; ++symbol) lengths[symbol] = 9;
        for (; symbol < 280; ++symbol) lengths[symbol] = 7;
        for (; symbol < 288; ++symbol) lengths[symbol] = 8;
        Construct(lh, lengths, 288);
        for (symbol = 0; symbol < 30; ++symbol) lengths[symbol] = 5;
        Construct(dh, lengths, 30);
        return Codes(lh, dh, out, outlen, outcnt);
    }

    int Dynamic(Huffman& lh, Huffman& dh, unsigned char* out, size_t outlen, size_t& outcnt)
    {
        static const short kOrder[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
        short lengths[288 + 32];
        int nlen = BitsPull(5) + 257;
        int ndist = BitsPull(5) + 1;
        int ncode = BitsPull(4) + 4;
        if (nlen > 286 || ndist > 30) return -3;
        int index = 0;
        for (; index < ncode; ++index) lengths[kOrder[index]] = (short)BitsPull(3);
        for (; index < 19; ++index) lengths[kOrder[index]] = 0;
        short tcount[16]{}, tsym[19]{};
        Huffman lencode{ tcount, tsym };
        int err = Construct(lencode, lengths, 19);
        if (err) return -5;
        index = 0;
        while (index < nlen + ndist) {
            int symbol = Decode(lencode);
            if (symbol < 0) return symbol;
            if (symbol < 16) {
                lengths[index++] = (short)symbol;
            } else {
                int len = 0, rep;
                if (symbol == 16) {
                    if (index == 0) return -5;
                    len = lengths[index - 1];
                    rep = 3 + BitsPull(2);
                } else if (symbol == 17) {
                    rep = 3 + BitsPull(3);
                } else {
                    rep = 11 + BitsPull(7);
                }
                if (index + rep > nlen + ndist) return -5;
                while (rep--) lengths[index++] = (short)len;
            }
        }
        // 检查 288/289 处的结束码
        err = Construct(lh, lengths, nlen);
        if (err && (err < 0 || nlen != lh.count[0] + lh.count[1])) return -7;
        err = Construct(dh, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != dh.count[0] + dh.count[1])) return -8;
        return Codes(lh, dh, out, outlen, outcnt);
    }
};

// 返回解压后字节数；出错返回负数
long PuffInflate(const unsigned char* in, size_t inlen, unsigned char* out, size_t outlen)
{
    Bits ctx{ in, inlen };
    Bits& s = ctx;
    short lcount[16]{}, lsym[288]{};
    short dcount[16]{}, dsym[30]{};
    Bits::Huffman lencode{ lcount, lsym };
    Bits::Huffman distcode{ dcount, dsym };
    size_t outcnt = 0;
    int last, err = 0;
    do {
        last = s.BitsPull(1);
        int type = s.BitsPull(2);
        if (last < 0 || type < 0) return -2;
        err = type == 0 ? s.Stored(out, outlen, outcnt)
            : type == 1 ? s.Fixed(lencode, distcode, out, outlen, outcnt)
            : type == 2 ? s.Dynamic(lencode, distcode, out, outlen, outcnt)
            : -9;
        if (err) break;
    } while (!last);
    return err ? err : (long)outcnt;
}

} // namespace puff

// ================= zip 解析 =================
namespace {

unsigned Rd16(const std::string& b, size_t o) { return (unsigned)(unsigned char)b[o] | ((unsigned)(unsigned char)b[o + 1] << 8); }
unsigned Rd32(const std::string& b, size_t o) { return Rd16(b, o) | ((unsigned)Rd16(b, o + 2) << 16); }

} // anonymous namespace

std::vector<ZipEntry> ZipList(const std::string& bytes)
{
    std::vector<ZipEntry> out;
    // 从尾部找 EOCD
    if (bytes.size() < 22) return out;
    long eocd = -1;
    for (long i = (long)bytes.size() - 22; i >= 0 && i >= (long)bytes.size() - 22 - 65557; --i) {
        if (Rd32(bytes, (size_t)i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) return out;
    unsigned count = Rd16(bytes, (size_t)eocd + 10);
    unsigned cdOffset = Rd32(bytes, (size_t)eocd + 16);
    for (unsigned i = 0; i < count; ++i) {
        size_t p = cdOffset;
        if (p + 46 > bytes.size() || Rd32(bytes, p) != 0x02014b50) break;
        unsigned method = Rd16(bytes, p + 10);
        unsigned csize = Rd32(bytes, p + 20);
        unsigned nameLen = Rd16(bytes, p + 28);
        unsigned extraLen = Rd16(bytes, p + 30);
        unsigned commentLen = Rd16(bytes, p + 32);
        unsigned localOffset = Rd32(bytes, p + 42);
        ZipEntry e;
        e.name = bytes.substr(p + 46, nameLen);
        e.method = method;
        e.offset = localOffset;
        e.compressedSize = csize;
        out.push_back(e);
        p += 46 + nameLen + extraLen + commentLen;
        cdOffset = p; // 继续下一条
    }
    return out;
}

bool ZipExtract(const std::string& bytes, const std::wstring& destDir, std::wstring& err)
{
    auto entries = ZipList(bytes);
    if (entries.empty()) { err = L"无效 zip 或无条目"; return false; }

    CreateDirectoryW(destDir.c_str(), nullptr);

    for (auto& e : entries) {
        // 防路径穿越：绝对路径、含 .. 段、含反斜杠的一律跳过
        if (e.name.empty() || e.name[0] == '/') continue;
        if (e.name.find("..") != std::string::npos) continue;
        if (e.name.find('\\') != std::string::npos) continue;

        bool isDir = !e.name.empty() && e.name.back() == '/';
        std::wstring wname = su::Utf8ToW(e.name);
        std::wstring target = destDir + L"\\" + wname;

        if (isDir) {
            // 逐级创建
            std::wstring cur;
            for (size_t i = 0; i < target.size(); ++i) {
                cur += target[i];
                if (target[i] == L'\\' || i + 1 == target.size()) {
                    if (!cur.empty() && cur.back() != L':') CreateDirectoryW(cur.c_str(), nullptr);
                }
            }
            continue;
        }

        // 读本地头
        size_t p = e.offset;
        if (p + 30 > bytes.size() || Rd32(bytes, p) != 0x04034b50) { err = L"本地头损坏"; return false; }
        unsigned nameLen = Rd16(bytes, p + 26);
        unsigned extraLen = Rd16(bytes, p + 28);
        size_t dataOff = p + 30 + nameLen + extraLen;
        if (dataOff + e.compressedSize > bytes.size()) { err = L"数据越界"; return false; }

        // 逐级创建父目录
        size_t slash = target.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            std::wstring parent = target.substr(0, slash);
            std::wstring cur;
            for (size_t i = 0; i < parent.size(); ++i) {
                cur += parent[i];
                if (parent[i] == L'\\') {
                    if (!cur.empty() && cur.back() != L':') CreateDirectoryW(cur.c_str(), nullptr);
                }
            }
        }

        HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { err = L"无法创建: " + target; return false; }

        if (e.method == 0) {
            DWORD written = 0;
            if (!WriteFile(h, bytes.data() + dataOff, e.compressedSize, &written, nullptr) ||
                written != e.compressedSize) {
                CloseHandle(h);
                err = L"写入不完整: " + target;
                return false;
            }
        } else if (e.method == 8) {
            // 先分配输出缓冲（zip 无 size 时用 csize*4 + 64KB 估算，GitHub zip 中央目录有原始大小——
            // 但 ZipEntry 未存原始大小，这里用增量扩容策略）
            std::string out;
            out.resize(e.compressedSize * 6 + 65536);
            for (int attempt = 0; attempt < 3; ++attempt) {
                long r = puff::PuffInflate(
                    (const unsigned char*)bytes.data() + dataOff, e.compressedSize,
                    (unsigned char*)out.data(), out.size());
                if (r >= 0) {
                    DWORD written = 0;
                    WriteFile(h, out.data(), (DWORD)r, &written, nullptr);
                    break;
                }
                if (r == -22) { out.resize(out.size() * 2); continue; } // 输出不够，扩容重试
                CloseHandle(h);
                err = L"inflate 失败: " + std::to_wstring(r);
                return false;
            }
        } else {
            CloseHandle(h);
            err = L"不支持的压缩方法: " + std::to_wstring(e.method);
            return false;
        }
        CloseHandle(h);
    }
    return true;
}

} // namespace net
