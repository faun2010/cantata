# 推荐录音封面

Recommended Recordings 中的条目可显示 96 像素宽的唱片封面，点击图片打开
MusicBrainz 对应发行版。每次打开或切换专辑，AlbumView 都把当前参考录音
清单传给同一套批量脚本下载器；图片异步加载，找到后自动刷新。切换到另一张
专辑会终止上一份清单，旧任务不会刷新新页面。

优先以唱片目录号和厂牌检索；没有目录号、查不到对应发行版或该版无封面时，
按当前作品、作曲家和演奏者检索。候选必须满足作品标题、作曲家及全部已提供
演奏者、厂牌的核对；不同录音组之间不能消除歧义时不显示封面。年份用于优先
选择有日期依据的结果；无目录号时，图片可能属于该录音的再版，可点击核对版本。

封面来自 Cover Art Archive。下载内容须成功解码为图片，缓存缩略图及来源链接。
缓存键包含作品、演奏者、厂牌、目录号和年份，避免同一演奏者的不同作品串图。
新缓存目录为 `recording-covers-v2`；查无结果缓存一天，网络或图片响应异常缓存
五分钟，旧缓存保留。无匹配或无图的条目保持文字显示。

本地 LLM 只用于现有的参考录音建议或 work-dossier；它不决定封面。脚本只在
MusicBrainz 的发行版同时核对作品、作曲家、演奏者和厂牌后下载，以免 LLM 的
模糊建议导致串图。

可手工预热当前或一批参考录音封面。清单最多 50 项，每项必须有 `composer`、
`work`、`performers`；`label`、`catalogue`、`year` 可选。输出为每项一行 JSON，
包含 `downloaded`、`cached`、`missing`、`retry`、`deferred` 或 `timeout` 状态。

```sh
cat > /tmp/reference-recordings.json <<'JSON'
{"recordings":[{
  "composer":"Carl Maria von Weber",
  "work":"Der Freischütz",
  "performers":"Joseph Keilberth",
  "label":"EMI",
  "year":"1958"
}]}
JSON
scripts/fetch-recording-covers.sh --input /tmp/reference-recordings.json
# 只检查缓存；或重新尝试此前的未匹配项：
scripts/fetch-recording-covers.sh --input /tmp/reference-recordings.json --no-network
scripts/fetch-recording-covers.sh --input /tmp/reference-recordings.json --retry-missing
```

验证：

```sh
cmake --build build-tests-rebase --target recordingcovers_test recommendedrecordings_test
ctest --test-dir build-tests-rebase -R '^(recordingcovers|recommendedrecordings)_test$' --output-on-failure
# 构建主程序后进行真实下载与磁盘缓存验收：
python3 scripts/run-macos-model-probe.py tests/recordingcovers_probe.cpp
# 验证已打包的脚本入口：
scripts/fetch-recording-covers.sh --input build-macos/recording-cover-worker-acceptance/manifest.json
```

联网探针使用项目内独立缓存，保留日志和 `build-macos/recording-cover-preview.png`。
