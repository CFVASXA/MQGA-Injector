# MQGA 注入器（C++ 版）

![cover](cover.png)

**640KB 单文件 GUI 注入器，把 LiteLoaderQQNT 兼容宿主注入 QQNT，无需修改 QQ 签名文件。**

![技术栈](https://img.shields.io/badge/C%2B%2B-Win32%2BWebView2-blue) ![体积](https://img.shields.io/badge/exe-640KB-green)

## 功能

- **双宿主**：MakeQQGreatAgain / LiteLoaderQQNT 卡片切换
- **一键注入**：Electron appSearchPaths 规则下三文件 `app.asar` 顶替应用根（不动签名文件）
- **禁用 / 恢复 / 卸载**：`.disabled` 互转，随时回退原生
- **GitHub 下载宿主**：内置镜像链（gh-proxy → ghproxy.net → 直连），自动解压
- **浏览目录**：原生目录对话框（每个路径输入框）
- **实时校验**：400ms 防抖检查 `QQ.exe` / `src\main.js`，✓/✗ 徽标
- **QQ 更新跟随**：自动检测未注入新版本，一键重装
- **三层日志**：操作入口 / JS 闭环 / `[OK][FAIL]`（`data\install.log`）
- **`--selftest` 自检**：一键全链路验证并留证

## 使用

1. 下载 Release 的 `MQGAInjector.exe`（或 `build_main.bat` 自己编译）
2. 放任意目录双击运行
3. 退出 QQ →「宿主管理」下载宿主 → 确认路径 →「一键安装注入」→ 启动 QQ

## 构建

```bat
build_main.bat
```

需要本地 MSVC 14.29 + Win11 SDK + WebView2 Runtime（Win10/11 自带）。

## 测试

| 套件 | 命令 | 覆盖 |
|---|---|---|
| util | `build_util_test.bat` | paths/JSON/config/logger/fsutil 24 项 |
| net | `build_net_test.bat` | 真实下载 + zip 解压（自实现 RFC1951 inflate） |
| inject | `build_inject_test.bat` | ASAR + 生命周期 24 项（需关闭 QQ） |
| selftest | `MQGAInjector.exe --selftest` | UI 消息桥端到端 |

## 架构

```
src/
├── util/   paths · strutil · logger · config · fsutil
├── qq/     定位 · 版本列表
├── net/    WinHTTP · 镜像链下载 · zip inflate
├── inject/ ASAR Pickle · 模板 · 安装服务
└── ui/     Win32+WebView2 · UI 线程消息队列 · 目录对话框
```

全部 WinHTTP/shell32 动态加载（离线 SDK 无对应 lib）；zip 解压自实现 RFC1951；ASAR 4MB 分块 SHA256；COM 跨线程经 UI 队列投递。

## 宣传视频

`MQGAInjector-promo.mp4`（33 秒，1080p）：片头品牌 + 操作演示 + 功能清单。

## 问题排查记录

开发中实际修复过的深层 bug（都有测试或日志佐证）：
- JS 对已解析对象重复 `JSON.parse` 致监听器崩溃（按钮锁死根因）
- C++ `g_busy` 占锁后从未复位（第二个操作起全被静默丢弃）
- WebView2 COM 非 free-threaded 跨线程调用静默失败
- `CreateCoreWebView2EnvironmentWithOptions` 4 参签名误写 3 参致栈错位
- 宿主侧 5 处性能地雷致 QQ 未响应（高频同步日志 + 常驻 DOM 定时器）
