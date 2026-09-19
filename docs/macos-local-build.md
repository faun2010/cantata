# macOS 本机构建

`scripts/build-macos-local.sh` 一键编译并生成 Apple Silicon 版 `dist/Cantata.app` 和 `dist/Cantata-3.5.0-arm64.dmg`（文件名随版本变化），部署目标为 macOS 15 或更新版本。打开 DMG，把 Cantata 拖到 Applications 即可安装。Qt 运行库、插件和 TagLib 会随 App 打包，运行时无需安装 Homebrew 或 Qt。

本次构建使用现有 Xcode、CMake、Ninja 和 TagLib 2.0.2；Qt 6.10.3 SDK 只解压到项目 `build-deps` 目录，没有安装新的系统软件包。TagLib 的默认查找位置是 `/opt/homebrew/opt/taglib`，也可以用 `CANTATA_TAGLIB_DIR` 指定私有安装目录。

准备 Qt SDK 时，仅需 qtbase、qtsvg、qttools、qttranslations、qtimageformats 和 qtmultimedia。将 Qt 官方包解压到任意私有目录，或用虚拟环境中的 aqtinstall 下载：

```sh
python -m aqt install-qt mac desktop 6.10.3 clang_64 \
  -O build-deps/qt -m qtimageformats qtmultimedia \
  --archives qtbase qtsvg qttools qttranslations
```

在已有私有 Qt SDK 的项目中，一键构建、验证并打包 DMG：

```sh
./scripts/build-macos-local.sh
```

脚本只在项目内构建、打包，检查每个 Mach-O 文件的动态库依赖与代码签名，再用 macOS 自带的 `hdiutil` 创建并验证压缩 DMG，无需安装 `create-dmg` 或 fish。DMG 包含 `Cantata.app` 和 Applications 快捷入口。打包失败时保留上一次 DMG，成功替换时将旧 DMG 移到构建目录留存。

默认 Qt 目录为 `build-deps/qt/6.10.3/macos`。可用 `CANTATA_QT_DIR`、`CANTATA_BUILD_DIR`、`CANTATA_OUTPUT_DIR`、`CANTATA_XCODE_DIR`、`CANTATA_TAGLIB_DIR` 和 `CANTATA_BUILD_JOBS` 覆盖路径或并行数。部署使用临时本机签名；这是本机使用的构建。

只生成 App，或用已有 App 单独重新打包：

```sh
./scripts/build-macos-local.sh --app-only
./scripts/package-macos-dmg.sh dist/Cantata.app
# 也可指定输出文件：
./scripts/package-macos-dmg.sh dist/Cantata.app dist/Cantata-custom.dmg
```

保留 MPD 客户端、标签编辑、HTTP 流播放等功能。本机构建关闭 Avahi 自动发现、外部设备支持和 ReplayGain 计算；连接已有 MPD 不受影响。

独立回归测试位于 `tests/`，配置时指定同一 Qt SDK 与 Xcode 编译器，运行 `ctest --output-on-failure`。`ollama_translation_smoke` 另外使用本机 `qwen3.8:27b` 验证实际译文、内存命中和重新创建服务后的磁盘命中；测试使用临时配置，不覆盖正常 App 配置。

构建主程序后，可运行 `python3 scripts/run-macos-model-probe.py tests/mpdsearchmodel_probe.cpp`，用实际生产对象验证搜索结果增量更新、持久索引与动画状态。清理工具测试命令为 `python3 -B -m unittest discover -s tests -p test_macos_cleanup.py`，删除操作仅在测试创建的临时仓库中执行。

构建与打包完成后不自动删除文件。安装并试用确认后，可手工清理：

```sh
# 预览清理目录和大小，不删除：
python3 scripts/clean-macos-local.py
# 清理编译、测试、DMG 暂存目录及其中的旧版备份，保留 Qt SDK：
python3 scripts/clean-macos-local.py --apply
# 如不再需要编译，可连同私有 SDK 一起清理：
python3 scripts/clean-macos-local.py --apply --include-sdk
```

清理范围固定为本项目的 `build-macos/`、`build-delivery/`、`build-tests-*/` 和 `aqtinstall.log`；加 `--include-sdk` 才删除 `build-deps/`。自定义构建目录需自行处理。`dist/` 中的 App、DMG 和验收资料，已安装的 App、Git 历史、其他任务的 `tmp/` 与 `output/` 均保留。删除 SDK 后，下次构建须按上文重新准备私有 Qt。

App 的翻译配置和持久缓存位于用户目录，清理工具不会改动，详见 [翻译说明](translation.md)。
