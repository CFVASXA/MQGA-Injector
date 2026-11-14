#include "templates.h"

#include "../util/strutil.h"

namespace inject {

std::vector<AsarFile> BuildShadowFiles(const std::wstring& hostRoot,
                                       const std::wstring& qqVersion,
                                       const std::wstring& resources) {
    const std::wstring hostEntry = hostRoot + L"\\src\\main.js";
    const std::wstring logFile = hostRoot + L"\\logs\\inject.log";
    const std::wstring background = resources + L"\\app\\application.asar\\background.js";

    std::string quotedVersion = su::JsonStr(qqVersion);
    if (quotedVersion.size() >= 2)
        quotedVersion = quotedVersion.substr(1, quotedVersion.size() - 2);

    const std::string packageJson =
        "{\n"
        "  \"name\": \"qq-chat\",\n"
        "  \"version\": \"" + quotedVersion + "\",\n"
        "  \"private\": true,\n"
        "  \"productName\": \"QQ\",\n"
        "  \"main\": \"app_launcher/index.js\"\n"
        "}\n";

    const std::string index =
        "\"use strict\";\n"
        "const path = require(\"path\");\n"
        "const fs = require(\"fs\");\n"
        "const Module = require(\"module\");\n"
        "const originalEntry = path.join(process.resourcesPath, \"app\", \"application.asar\", \"app_launcher\", \"index.js\");\n"
        "const hostEntry = " + su::JsonStr(hostEntry) + ";\n"
        "const logFile = " + su::JsonStr(logFile) + ";\n"
        "let started = false;\n"
        "function startOriginal() {\n"
        "    if (started) return;\n"
        "    started = true;\n"
        "    require(originalEntry);\n"
        "}\n"
        "function log(message) {\n"
        "    try { fs.appendFileSync(logFile, \"[\" + new Date().toISOString() + \"] \" + message + \"\\n\"); } catch (_) {}\n"
        "}\n"
        "if (process.env.LQ_INJECT_DISABLE === \"1\") {\n"
        "    log(\"disabled: native QQ entry\");\n"
        "    startOriginal();\n"
        "} else {\n"
        "    try {\n"
        "        require(hostEntry);\n"
        "        if (!Module._cache[originalEntry]) startOriginal();\n"
        "        if (global.launcher?.installPathPkgJson) global.launcher.installPathPkgJson.main = \"./application.asar/app_launcher/index.js\";\n"
        "        log(\"host loaded: \" + hostEntry);\n"
        "    } catch (error) {\n"
        "        log(\"host load error: \" + (error && error.stack || error));\n"
        "        try { startOriginal(); } catch (e) { console.error(e); }\n"
        "    }\n"
        "}\n";

    const std::string backgroundJs =
        "// QQ resolves its native main-process body from app.getAppPath().\n"
        "module.exports = require(" + su::JsonStr(background) + ");\n";

    return {
        {"package.json", packageJson},
        {"app_launcher/index.js", index},
        {"application/background.js", backgroundJs}
    };
}

} // namespace inject
