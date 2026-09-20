# 作曲家简介查询

信息页保留曲库中的原始姓名（包括 IMSLP 拼写）。外部简介查询使用
`ComposerTable::biographyName()` 识别的标准名字，支持已收录的转写别名、
重音符号、大小写、姓名倒序及缩写。例如 `Aleksandr Borodin`、
`Alexandre Borodine` 会查询 `Alexander Borodin`。未识别的名字保留原文查询，
不单凭相同姓氏猜测人物。

Wikipedia 首先使用标题和重定向 API 获取正式页面及简介。缺页、消歧义页、
空简介或请求失败时，回退到既有搜索及 Last.fm 流程。Last.fm 的
`This is mistagged...` 等更正标签提示不作为人物简介，也不送去翻译。

已识别别名的简介缓存共用标准姓名键；显示名称和曲库文件标签不变。
旧的别名缓存保留，标准姓名缓存中的更正标签提示会被跳过并重新查询。
翻译继续使用现有设置；网络或资料源不可用时不能保证取得新简介。

已识别的作曲家图片优先通过标准姓名查询 Wikipedia 页面肖像，核对返回页面
确为同一作曲家后才下载；消歧义页、缺图或下载失败时回退到原有
Last.fm、MusicBrainz、Discogs 等图片流程。外部姓名查询也使用标准姓名，
图片缓存和界面仍保留曲库原始姓名。此路径不依赖 MusicBrainz 条目包含图片链接。

验证：

```sh
cmake --build build-tests-rebase --target artistlookup_test composertable_test
ctest --test-dir build-tests-rebase -R '^(artistlookup|composertable)_test$' --output-on-failure
# 构建 App 后，用生产 WikipediaEngine 联网验证不同拼写及实际简介：
python3 scripts/run-macos-model-probe.py tests/artistlookup_probe.cpp
# 实际图片下载、解码及缓存验证，使用独立的 CantataArtistImageProbe 配置：
python3 scripts/run-macos-model-probe.py tests/artistimage_probe.cpp
```

联网探针不写入用户简介缓存，结果保留在 `build-macos/probe-artistlookup_probe-result.txt`。
