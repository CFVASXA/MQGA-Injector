// strutil.cpp — 编码转换与最小 JSON。
#include "strutil.h"
#include <windows.h>
#include <vector>

namespace su {

std::wstring Utf8ToW(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string WToUtf8(const std::wstring& s)
{
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), u.data(), n, nullptr, nullptr);
    return u;
}

// ---- 最小 JSON 解析（顶层扁平） ----
// 状态机：跳过空白，读取 "key": value 对。value 为 string/number/bool。
// 嵌套对象/数组：只识别顶层 "config":{...}（展开一层进 map，前缀 "config."）
// 与 "versions":[...]（元素以 \x1f 连接存 "versions[]"）。
namespace {

void SkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

std::string ParseString(const std::string& s, size_t& i)
{
    // s[i] == '"'
    ++i;
    std::string out;
    while (i < s.size()) {
        char c = s[i];
        if (c == '"') { ++i; break; }
        if (c == '\\') {
            ++i;
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 <= s.size()) {
                        auto hex = [&](char ch) -> int {
                            if (ch >= '0' && ch <= '9') return ch - '0';
                            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                            return -1;
                        };
                        int cp = 0;
                        bool ok = true;
                        for (int k = 0; k < 4; ++k) {
                            int d = hex(s[i + k]);
                            if (d < 0) { ok = false; break; }
                            cp = cp * 16 + d;
                        }
                        if (ok) {
                            i += 4;
                            // UTF-16 → UTF-8（含代理对）
                            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() &&
                                s[i] == '\\' && s[i + 1] == 'u') {
                                int lo = 0; bool ok2 = true;
                                for (int k = 0; k < 4; ++k) {
                                    int d = hex(s[i + 2 + k]);
                                    if (d < 0) { ok2 = false; break; }
                                    lo = lo * 16 + d;
                                }
                                if (ok2 && lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                    i += 6;
                                }
                            }
                            wchar_t wc[2] = { (wchar_t)cp, 0 };
                            out += WToUtf8(std::wstring(wc));
                        }
                    }
                    break;
                }
                default:
                    // 非法转义（如 \L）：保留反斜杠 + 原字符，不静默吞字符
                    out += '\\';
                    out += e;
                    break;
            }
        } else {
            out += c; ++i;
        }
    }
    return out;
}

} // anonymous namespace

std::map<std::wstring, std::wstring> JsonParseFlat(const std::string& utf8)
{
    std::map<std::wstring, std::wstring> m;
    const std::string& s = utf8;
    size_t i = 0;
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return m;
    ++i;
    while (i < s.size()) {
        SkipWs(s, i);
        if (i >= s.size()) break;
        if (s[i] == '}') break;
        if (s[i] == ',') { ++i; continue; }
        if (s[i] != '"') break;
        std::string key = ParseString(s, i);
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') break;
        ++i;
        SkipWs(s, i);
        if (i >= s.size()) break;
        char v = s[i];
        std::wstring wkey = Utf8ToW(key);
        if (v == '"') {
            std::string val = ParseString(s, i);
            m[wkey] = Utf8ToW(val);
        } else if (v == '{') {
            // 嵌套对象：展开一层，前缀 "key."
            ++i;
            std::wstring prefix = wkey + L".";
            while (i < s.size()) {
                SkipWs(s, i);
                if (i >= s.size()) break;
                if (s[i] == '}') { ++i; break; }
                if (s[i] == ',') { ++i; continue; }
                if (s[i] != '"') break;
                std::string k2 = ParseString(s, i);
                SkipWs(s, i);
                if (i >= s.size() || s[i] != ':') break;
                ++i; SkipWs(s, i);
                if (i < s.size() && s[i] == '"') {
                    m[prefix + Utf8ToW(k2)] = Utf8ToW(ParseString(s, i));
                } else {
                    // 非 string 值：跳过到 , 或 }
                    while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
                }
            }
        } else if (v == '[') {
            // 数组：元素以 \x1f 连接
            ++i;
            std::wstring joined;
            while (i < s.size()) {
                SkipWs(s, i);
                if (i >= s.size()) break;
                if (s[i] == ']') { ++i; break; }
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == '"') {
                    if (!joined.empty()) joined += L'\x1f';
                    joined += Utf8ToW(ParseString(s, i));
                } else {
                    while (i < s.size() && s[i] != ',' && s[i] != ']') ++i;
                }
            }
            m[wkey + L"[]"] = joined;
        } else {
            // number / true / false / null：取到 , 或 }
            size_t st = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
            m[wkey] = Utf8ToW(s.substr(st, i - st));
        }
    }
    return m;
}

std::string JsonStr(const std::wstring& v)
{
    std::string out = "\"";
    std::string u = WToUtf8(v);
    for (char c : u) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    out += "\"";
    return out;
}

std::vector<std::wstring> SplitArr(const std::wstring& joined)
{
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : joined) {
        if (c == L'\x1f') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty() || !out.empty()) out.push_back(cur);
    return out;
}

} // namespace su
