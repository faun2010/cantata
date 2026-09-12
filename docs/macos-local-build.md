# macOS 本机构建

`scripts/build-macos-local.sh` 生成 Apple Silicon 版 `dist/Cantata.app`，部署目标为 macOS 15 或更新版本。Qt 运行库、插件和 TagLib 会随 App 打包，运行时无需安装 Homebrew 或 Qt。

本次构建使用现有 Xcode、CMake、Ninja 和 TagLib 2.0.2；Qt 6.10.3 SDK 只解压到项目 `build-deps` 目录，没有安装新的系统软件包。TagLib 的默认查找位置是 `/opt/homebrew/opt/taglib`，也可以用 `CANTATA_TAGLIB_DIR` 指定私有安装目录。

准备 Qt SDK 时，仅需 qtbase、qtsvg、qttools、qttranslations、qtimageformats 和 qtmultimedia。将 Qt 官方包解压到任意私有目录，或用虚拟环境中的 aqtinstall 下载：

```sh
python -m aqt install-qt mac desktop 6.10.3 clang_64 \
  -O build-deps/qt -m qtimageformats qtmultimedia \
  --archives qtbase qtsvg qttools qttranslations
```

构建和验证：

```sh
CANTATA_QT_DIR="$PWD/build-deps/qt/6.10.3/macos" scripts/build-macos-local.sh
```

脚本只在项目内构建、打包，最后检查每个 Mach-O 文件的动态库依赖与代码签名。可用 `CANTATA_BUILD_DIR`、`CANTATA_OUTPUT_DIR` 和 `CANTATA_XCODE_DIR` 覆盖目录。部署使用临时本机签名；这是本机使用的构建。

保留 MPD 客户端、标签编辑、HTTP 流播放等功能。本机构建关闭 Avahi 自动发现、外部设备支持和 ReplayGain 计算；连接已有 MPD 不受影响。

独立回归测试位于 `tests/`，配置时指定同一 Qt SDK 与 Xcode 编译器，运行 `ctest --output-on-failure`。`ollama_translation_smoke` 另外使用本机 `qwen3.8:27b` 验证实际译文、内存命中和重新创建服务后的磁盘命中；测试使用临时配置，不覆盖正常 App 配置。

构建完成后保留 `build-deps/`、`build-macos/`、`build-tests-*/` 和验收文件，供用户手工验证和试用；只有用户明确要求清理时才删除。App 的翻译配置和持久缓存位于用户目录，详见 [翻译说明](translation.md)。
