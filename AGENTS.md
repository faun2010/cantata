# Cantata 本机构建与验收

- 新增构建依赖放在项目私有目录，不安装到主机全局环境。
- 修改源码后，重新构建并更新实际交付的 `dist/Cantata.app`，核对运行路径并重启验证。
- 保留私有 Qt SDK、构建目录、调试与验收文件，供用户手工验证和试用。只有用户明确要求清理时才删除，不能在编译或自动验证通过后自行清理。
- 构建方法见 `docs/macos-local-build.md` 和 `scripts/build-macos-local.sh`。
- 一键构建默认交付 App 和 DMG；打包完成不自动清理。用户可运行 `scripts/clean-macos-local.py` 预览，再加 `--apply` 手工清理默认构建目录；`--include-sdk` 可同时清理私有 SDK。保留 `dist/`、用户配置和翻译缓存，不清理其他任务的 `tmp/`、`output/`。
