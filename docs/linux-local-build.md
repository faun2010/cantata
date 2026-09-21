# Linux 本机构建

在项目目录内构建并直接运行 `build/cantata`，不安装到系统，也不修改系统软件包。以下命令在 Ubuntu 24.04（Qt 6.4.2，无 KF6，无 Qt Multimedia 开发包）上验证过。

一键构建（自动识别 Ubuntu，等价于下面的完整命令）：

```sh
cd cantata
./mybuild.sh            # 配置 + 编译，并用 --version 做无界面自检
./mybuild.sh --run      # 构建成功后直接启动 cantata
```

构建目录可用 `CANTATA_BUILD_DIR` 覆盖，并行度可用 `CANTATA_BUILD_JOBS` 覆盖。

## 构建与运行

```sh
cd cantata

# 首次构建，或修改 CMake 选项后执行
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_HTTP_STREAM_PLAYBACK=OFF \
  -DBUNDLED_KCATEGORIZEDVIEW=ON \
  -DBUNDLED_KARCHIVE=ON

# 编译；改代码后只需重复这一步
cmake --build build -j"$(nproc)"

# 直接运行，无需安装
./build/cantata
```

选项说明：

| 选项 | 原因 |
|---|---|
| `-DENABLE_HTTP_STREAM_PLAYBACK=OFF` | 系统没有 Qt Multimedia 开发包；关闭后仍可连接 MPD 播放，只是不能在 Cantata 内播放 MPD 的 HTTP 输出流。 |
| `-DBUNDLED_KCATEGORIZEDVIEW=ON`、`-DBUNDLED_KARCHIVE=ON` | 系统没有 KF6，使用源码树内自带的副本。 |
| `-G Ninja` | 可省略，省略时使用 Makefile，编译命令不变。 |

其他选项见 [`INSTALL`](../INSTALL)。确实需要安装时再执行 `sudo cmake --install build`，默认前缀是 `/usr/local`。

## 回归测试

`tests/` 是独立的 CMake 工程，不随主程序构建：

```sh
cmake -S tests -B build-tests -G Ninja
cmake --build build-tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build-tests --output-on-failure
```

`QT_QPA_PLATFORM=offscreen` 让测试在没有图形会话时也能运行。

## 常见问题

- **配置时报 “Qt6 6.5.0 required”**：当前分支缺少提交 “build: allow building against Qt 6.4”，先合入该提交。
- **同时装有系统版 Cantata**：先退出 `/usr/bin/cantata` 再运行 `./build/cantata`。两者共用 `~/.config/Cantata` 配置，避免分不清在测试哪一个。
- **连接本机 MPD 或翻译服务失败，而系统版正常**：检查“设置 → Proxy”和 `http_proxy` 环境变量。回环地址始终直连，局域网地址会走代理，详见 [翻译说明](translation.md)。
