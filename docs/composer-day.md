# 今日作曲家（Composer of the Day）

播放列表页多了一个子页 **Composer of the Day**：只显示"今天"这一天出生或去世的著名作曲家，
每位作曲家列出最著名的 10 部作品，每部作品从你的曲库里挑 1 个录音。
它是动态的——换一天打开，列表就换成那天的纪念日；库里永远只存今天这几条，不会把 365 天都铺出来。

## 数据从哪来

1. **纪念日日历**：`playlists/composercalendar.json`（编译进资源，约 150 位作曲家的生卒日期）。
   想加人或改日期，把同样格式的文件放到配置目录下即可覆盖内置的那份：
   - Linux：`~/.config/cantata/composercalendar.json`
   - macOS：`~/Library/Preferences/cantata/composercalendar.json`

   ```json
   {
     "version": 1,
     "composers": [
       {"name": "Jean Sibelius", "zh": "西贝柳斯", "born": "1865-12-08", "died": "1957-09-20"}
     ]
   }
   ```
   `name` 用作曲家标签里常见的西文本名（与 `context/composertable.cpp` 的规范名一致最稳妥），
   `zh` 可选、中文界面下显示，在世作曲家不写 `died`。2 月 29 日出生的（罗西尼）在平年顺延到 3 月 1 日显示。

2. **十大名作**：由已配置的 LLM 给出（prompt 上下文 `composer-works-v1`，见 `network/translationservice.cpp`），
   结果按作曲家缓存，所以同一位作曲家只问一次。
   **没配 LLM 或请求失败时**不会空着：改用曲库自己推导——把该作曲家的专辑按作品归并，
   按"你拥有几个录音版本"排序取前 10 个。

3. **每部作品 1 个录音**：从 MPD 按作曲家姓氏搜索，用 `ComposerTable::resolve()` 把标签归一
   （这样 J.S. 巴赫不会混进 J.C. 巴赫的曲子），再按作品匹配专辑：
   作品号（Op./BWV/K. 等）匹配优先于标题匹配，整张专辑就是这部作品时取整张，
   合辑里则只取属于这部作品的那几轨，并按碟号/音轨号排好。库里没有的作品照样列出来，
   标注"No recording of this work in your library"，但不可选中、不会进播放队列。

选中作曲家那一行加入播放队列 = 把他今天这 10 部作品的录音按顺序全部加进去；
选中单部作品则只加那一个录音。

## 相关代码

| 文件 | 作用 |
| --- | --- |
| `playlists/composerday.{h,cpp}` | 纯逻辑：日历解析、纪念日筛选、作品解析、作品↔曲库匹配（无网络/GUI，有单测） |
| `playlists/composerdaymodel.{h,cpp}` | 两级模型：作曲家 → 作品 |
| `playlists/composerdaypage.{h,cpp}` | 页面：MPD 搜索 + LLM 调用的编排 |
| `playlists/composercalendar.json` | 内置纪念日日历 |
| `tests/composerday_test.cpp` | 单元测试：`cmake -S tests -B build-tests && cmake --build build-tests && ./build-tests/composerday_test` |
