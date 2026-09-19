# LLM 中文翻译

Cantata 会把当前艺术家简介和音乐元素的详情提示异步翻译成简体中文。原文会立即显示；模型返回后，界面只在歌曲、艺术家、原文和控件仍匹配时更新，避免切歌或移动鼠标后显示过期结果。翻译失败、超时或队列溢出时继续显示原文。

悬停翻译覆盖歌曲详情、播放队列、艺术家、专辑、按流派分组的音乐库，以及当前播放的曲名和艺术家文字。原始音乐标签不改写。艺术家简介的译文保留原有链接目标，链接文字可以翻译；模型丢失链接标记时，会在译文末尾保留原链接。右键菜单提供“显示原文”。

音乐详情保留英文原有的双列表格：字段名加粗、右对齐，字段值左对齐。底部文件目录保留原文和斜体，不发送给模型；已有缓存中的断行会在显示时合并，旧目录译文会被原始目录替换，无需重新翻译已有结果。

音乐库、播放队列和搜索页支持中文搜索。输入中文时，通过同一个 LLM 配置异步生成英语、法语等语言的等价关键词，例如“动物”可匹配 `Le carnaval des animaux`。原中文关键词始终参与匹配，模型返回后自动补充结果；首次查询可能需要等待模型响应，重复搜索及重启后的搜索复用持久缓存，无需预先翻译整座音乐库。输入使用现有防抖；改词或清空后，取消没有其他视图使用的旧扩展请求。搜索页增量追加去重后的结果，保留选中项，扩展和已提交查询全部结束后才停止动画。音乐库与播放队列保留空格分词的 AND 关系和 `#年份` 筛选，SQLite 查询不依赖 FTS 括号扩展。搜索页的文件、日期和修改时间类别不做翻译扩展；LLM 不可用时仍可按原词搜索。

## 配置

推荐在“设置 → Translation”页面修改：勾选“Enable translation”，填写 provider、URL、模型等，点击“确定”后立即生效，无需重启。“Test”按钮会先保存当前设置，再发送一条 `Hello, world` 验证服务是否可用。

设置页和手工编辑的是同一个 `translation.ini`。首次使用时自动创建，权限在 Unix 和 macOS 上限制为当前用户读写。默认位置：

| 平台 | 配置文件 | 持久缓存目录 |
|---|---|---|
| Linux | `~/.config/Cantata/Cantata/translation.ini` | `~/.cache/Cantata/Cantata/translations/` |
| macOS | `~/Library/Preferences/Cantata/Cantata/translation.ini` | `~/Library/Caches/Cantata/Cantata/translations/` |

手工编辑配置文件后必须重启 Cantata；运行中的程序只在设置页保存时重新读取。示例见 [`config/translation.ini.example`](../config/translation.ini.example)。

新建的配置默认**关闭**翻译（`enabled=false`），避免在用户确认 provider 和 URL 之前把悬停内容、搜索词和艺术家简介发给任何服务。其余默认值连接本机 Ollama：

```ini
[Translation]
enabled=false
provider=ollama
url=http://127.0.0.1:11434
model=qwen3.8:27b
targetLanguage=Simplified Chinese
promptVersion=2
timeoutMs=180000
searchTimeoutMs=30000
cooldownSeconds=30
maxMemoryEntries=512
maxConcurrentRequests=1
maxQueuedRequests=16
disableThinking=true
apiKey=
```

| 键 | 说明 |
|---|---|
| `enabled` | `true` 才会发起翻译请求；为 `false` 时直接显示原文，也不读缓存。 |
| `provider` | `ollama`，或其他任意值（如 `openai`）表示 OpenAI-compatible Chat Completions 接口。 |
| `url` | 见下文 endpoint 规则。 |
| `model` | 服务端的模型名。 |
| `targetLanguage` | 写入提示词的目标语言。 |
| `promptVersion` | 提示词版本，参与缓存键；修改后旧缓存不再命中。 |
| `timeoutMs` | 单个请求超时，100–900000 毫秒。 |
| `cooldownSeconds` | 某条原文翻译失败后，在该秒数内不再重试这一条。 |
| `maxMemoryEntries` | 内存缓存条数上限。 |
| `maxConcurrentRequests` | 同时进行的请求数，1–8。 |
| `maxQueuedRequests` | 等待队列长度，0–1024；满时淘汰最早的等待项。 |
| `disableThinking` | 默认 `true`，要求推理模型跳过思考阶段。见“请求很慢”。设置页不提供此项，需手工编辑。 |
| `apiKey` | 非空时以 `Authorization: Bearer` 发送。 |

`provider=ollama` 时，`url` 可以是服务根地址、以 `/api` 结尾的地址或完整的 `/api/generate` 地址。其他 provider 值按 OpenAI-compatible Chat Completions 接口处理：根地址会自动追加 `/v1/chat/completions`，以 `/v1` 结尾时追加 `/chat/completions`，也可以直接填写完整 endpoint；需要认证时把令牌写入 `apiKey`。

翻译请求走 Cantata 共享的网络管理器，遵循“设置 → Proxy”（默认使用系统代理，即 `http_proxy` 等环境变量）。`localhost` 和回环地址（`127.0.0.1`、`::1`）始终直连；局域网地址（如 `192.168.x.x`）会走代理，代理无法访问该地址时，把它加入 `no_proxy` 或在 Proxy 页选择“No proxy”。

## 何时发起请求

启用后，只有以下操作会请求模型，其余界面文字不翻译：

- 在音乐库、播放队列或搜索页输入**含中文**的搜索词（不超过 160 个字符，且不含 `/` 或 `\`）。纯英文搜索不请求。
- 悬停列表中的**音乐详情提示**（带加粗字段名的表格型提示，上下文 `music-details`）；普通操作提示不翻译。
- 悬停“正在播放”区域的曲名和艺术家。
- 信息页显示艺术家简介。
- 设置页的“Test”按钮。

已有缓存的内容直接显示，不再请求。

默认只允许一个模型请求同时执行，最多保留 16 个等待请求。同一配置、用途和原文的重复请求会合并。新的中文搜索优先进入等待队列；队列满时优先淘汰旧的普通请求。搜索请求使用 `timeoutMs` 与 `searchTimeoutMs` 两者中较短的超时，其余翻译使用 `timeoutMs`。连接被拒绝或主机无法解析时，整个端点按 `cooldownSeconds` 冷却，避免未运行 Ollama 时因不同曲目而连续重试。

翻译结果先保存在内存中，再按 SHA-256 内容键写入上表的缓存目录，每条一个 `.txt` 文件。缓存键包含 provider、完整 endpoint、model、目标语言、提示词版本、用途和原文；切换服务器或模型不会误用旧译文。想强制重新翻译时，删除缓存目录即可。

`--no-network` 会禁止新的翻译请求，但已有内存和磁盘缓存仍可读取。模型输出按纯文本进行 HTML 转义后才显示。

## 故障排查

### 没有发起翻译请求

按顺序检查：

1. `translation.ini` 中 `enabled=true`。设置页修改后要点“确定”，直接关闭对话框不会保存；手工编辑后要重启。
2. 触发的操作在“何时发起请求”列表中，例如搜索词确实含中文。
3. 最近是否连接失败：连接被拒绝、主机不可达、超时或代理错误后，所有翻译在 60 秒内直接显示原文；单条失败的原文在 `cooldownSeconds` 内不重试。
4. 队列是否被占满：`maxConcurrentRequests=1` 时，一个慢请求会让后续请求排队，超出 `maxQueuedRequests` 的早期请求被丢弃。
5. 未使用 `--no-network` 启动。
6. 用 `curl --noproxy '*'` 直接访问 endpoint，确认服务和模型名可用；若 curl 直连成功而 Cantata 失败，检查上文的代理说明。

### 请求很慢

推理模型（如带思考阶段的 Qwen、DeepSeek 等）会先生成大量推理内容再输出译文。实测一条音乐详情提示：不关闭思考时 30 秒、约 3800 个输出 token；关闭后 0.9 秒、约 100 个 token。

`disableThinking=true`（默认）时：

- Ollama 请求发送 `think: false`。
- OpenAI-compatible 请求发送 `chat_template_kwargs: {"enable_thinking": false, "thinking": false}`，vLLM、SGLang 等服务会据此关闭思考。

若服务端拒绝未知字段（例如官方 OpenAI API 报 400），设置 `disableThinking=false`。服务不支持上述开关时，改用非推理模型，或在服务端配置默认关闭思考。模型较慢时，首次悬停和搜索会等待模型响应，之后命中缓存立即显示。
