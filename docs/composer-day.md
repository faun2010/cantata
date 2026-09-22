# 今日音乐纪念

播放列表页的“今日音乐纪念”始终只有两个动态分组：**今日诞辰**和**今日忌日**。分组下仅显示曲库有匹配作品的音乐家，每人最多 10 部代表作、每部最多 2 个录音版本，选中分组即可加入整个分组的曲目；重复文件会去重。程序不会自动创建 MPD 的永久播放列表，也不会积累 365 × 2 个列表。需要永久保存时仍可手动保存选择。

名单来自 On This Day 当天的音乐生日、忌日页面，例如：
https://www.onthisday.com/music/deaths/september/22 。日期跟随电脑本地日期，跨午夜自动更新，重新显示页面也会检查日期。作曲家、演奏家和歌手均参与匹配。

每天只缓存两份网页，放在应用缓存目录的 `music-day/birthdays.html`、`music-day/deaths.html`，新日期覆盖旧日期。网络失败或页面结构无法识别时明确提示，回退到内置作曲家日历；不会拿昨天的人物当作今天的名单。内置日历仍可用配置目录的 `composercalendar.json` 覆盖。

## 曲库和录音选择

- 查询 MPD 的 Composer、Artist、AlbumArtist 标签，再验证完整人物姓名、网站提供的别名或已知作曲家规范名，不以单姓直接认定人物。
- 配置了 LLM 时，请求该音乐家的代表作；作曲家查自身作品，演奏家查其知名演奏作品。只保留 MPD 库中实际匹配的录音。
- 优先选择 `recommendedrecordings.json` 推荐资料中能匹配作品、完整演奏者姓名及已知年份的版本。资料默认来自内置资源，配置目录同名文件可覆盖。
- 推荐资料无法确认时，标记“曲库录音（未核实推荐版本）”。没有可播放的推荐代表作时，从曲库按录音版本数选取最多十部作品；不再用整张合集兜底。
- 每部作品最多选择两个不同录音，推荐版本优先，各版本内按碟号和音轨号排序。没有匹配作品的音乐家不显示，加载过程中也不展示空占位行。

姓名或年份标签缺失会导致保守漏选；LLM 代表作和推荐资料的覆盖范围也有限。界面标注区分已匹配的推荐版本与普通库内录音。

## 验证

`composerday_test` 覆盖日期、网站结构、完整姓名和作品匹配；`dayrecordings_test` 覆盖推荐录音优先、年份及人物不匹配回退。构建：

```sh
cmake -S tests -B build-tests
cmake --build build-tests --target composerday_test dayrecordings_test
ctest --test-dir build-tests -R '^(composerday|dayrecordings)_test$' --output-on-failure
```
