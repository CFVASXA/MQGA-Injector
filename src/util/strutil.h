// strutil.h — 字符串与编码转换、最小 JSON 工具。全局公共依赖。
#pragma once
#include <string>
#include <map>
#include <vector>

namespace su {
    std::wstring Utf8ToW(const std::string& s);
    std::string  WToUtf8(const std::wstring& s);

    // 把 JSON 文本解析成扁平 kv：只取顶层对象的 string/number/bool 值，
    // 以及指定数组的字符串元素（key 形如 "versions[]"，多个值用 \x1f 分隔存进同一 key）。
    // 足够本程序 UI 协议使用，不追求完整 JSON。
    std::map<std::wstring, std::wstring> JsonParseFlat(const std::string& utf8);

    // 把 value 转成可放进 JSON 字符串字面量的带引号形式（含转义）
    std::string JsonStr(const std::wstring& v);

    // 数组值拆分（对应 JsonParseFlat 的 \x1f 约定）
    std::vector<std::wstring> SplitArr(const std::wstring& joined);
}
