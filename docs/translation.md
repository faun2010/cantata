# LLM 中文翻译

Cantata 会把当前艺术家简介和音乐元素的详情提示异步翻译成简体中文。原文会立即显示；模型返回后，界面只在歌曲、艺术家、原文和控件仍匹配时更新，避免切歌或移动鼠标后显示过期结果。翻译失败、超时或队列溢出时继续显示原文。

悬停翻译覆盖歌曲详情、播放队列、艺术家、专辑、按流派分组的音乐库，以及当前播放的曲名和艺术家文字。原始音乐标签不改写。艺术家简介的译文保留原有链接目标，链接文字可以翻译；模型丢失链接标记时，会在译文末尾保留原链接。右键菜单提供“显示原文”。

音乐详情保留英文原有的双列表格：字段名加粗、右对齐，字段值左对齐。底部文件目录保留原文和斜体，不发送给模型；已有缓存中的断行会在显示时合并，旧目录译文会被原始目录替换，无需重新翻译已有结果。

音乐库、播放队列和搜索页支持中文搜索。输入中文时，通过同一个 LLM 配置异步生成英语、法语等语言的等价关键词，例如“动物”可匹配 `Le carnaval des animaux`。原中文关键词始终参与匹配，模型返回后自动补充结果；首次查询可能需要等待模型响应，重复搜索及重启后的搜索复用持久缓存，无需预先翻译整座音乐库。音乐库与播放队列保留空格分词的 AND 关系和 `#年份` 筛选。搜索页的文件、日期和修改时间类别不做翻译扩展；LLM 不可用时仍可按原词搜索。

首次使用时会自动创建 `translation.ini`，权限在 Unix 和 macOS 上限制为当前用户读写。macOS 默认位置是：

```text
~/Library/Preferences/Cantata/Cantata/translation.ini
```

示例见 [`config/translation.ini.example`](../config/translation.ini.example)。修改配置后重启 Cantata 生效。默认配置连接本机 Ollama：

```ini
[Translation]
enabled=true
provider=ollama
url=http://127.0.0.1:11434
model=qwen3.8:27b
targetLanguage=Simplified Chinese
promptVersion=2
timeoutMs=180000
cooldownSeconds=30
maxMemoryEntries=512
maxConcurrentRequests=1
maxQueuedRequests=16
apiKey=
```

`provider=ollama` 时，`url` 可以是服务根地址、以 `/api` 结尾的地址或完整的 `/api/generate` 地址。其他 provider 值按 OpenAI-compatible Chat Completions 接口处理：根地址会自动追加 `/v1/chat/completions`，也可以直接填写完整 endpoint；需要认证时把令牌写入 `apiKey`。

默认只允许一个模型请求同时执行，最多保留 16 个等待请求。同一配置、用途和原文的重复请求会合并。队列满时淘汰最早的等待项，使近期鼠标悬停内容优先得到翻译。

翻译结果先保存在内存中，再按 SHA-256 内容键写入磁盘。缓存键包含 provider、完整 endpoint、model、目标语言、提示词版本、用途和原文；切换服务器或模型不会误用旧译文。macOS 默认缓存目录是：

```text
~/Library/Caches/Cantata/Cantata/translations
```

`--no-network` 会禁止新的翻译请求，但已有内存和磁盘缓存仍可读取。失败项在 `cooldownSeconds` 内不重复请求。模型输出按纯文本进行 HTML 转义后才显示。
