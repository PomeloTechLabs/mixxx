> 本文由 AI Agent 自动生成，供用户检查。

# 旧柚Mixxx迁移助手

Windows 便携包解压后双击 `PomeloMixxxMigration.exe`，无需安装 Python。

1. 关闭 Windows Mixxx，点击“读取本机 Mixxx 配置”，直接读取自动识别的配置目录（通常为 `%LOCALAPPDATA%/Mixxx`）；自定义路径用“选择其他配置目录”。无需先压缩为 RAR/ZIP，“从备份文件读取”仅用于已有备份。
2. 为每个旧音乐根选择现在对应的目录。保留原相对子目录结构可批量关联；大小发生变化的文件需要手动确认。
3. 核对缺失歌曲。工具不会按同名文件猜测关联；可以为选中歌曲指定实际音频。
4. 默认携带音乐；波形缓存可选。生成 ZIP64 包，完成后会校验内容。取消不会覆盖已有输出。
5. 将包复制到手机/Pad 的 `Download/com.pomelo.mixxx`，在原 Mixxx 的“选项 → 导入 Windows 迁移包”中选择。
6. 导入建立独立档案。重新启动后加载新档案；“配置档案／还原”可切回原档案，音乐不删除。

各档案的设置和数据库独立保存，公共 Music 目录共用。启动扫描也可能发现其它导入批次的音频；切换档案不会移走那些文件。导入结果报告在 `Download/com.pomelo.mixxx/logs/import-<档案 ID>.json`。

备份里没有音乐时，只能迁移配置和曲库记录。未携带歌曲仍保留 CUE、歌单及数据库 ID，目标端显示缺失。工具不修改来源配置、数据库或音乐。

首版支持单个完整或部分 ZIP64 包、多根映射、同名歌曲独立保存、封面、采样器、效果和可选分析缓存。所选歌单导出、多卷、补充包、合并现有曲库、自定义皮肤资源迁移尚未实现。控制器映射可携带，不代表鸿蒙支持对应硬件。路径映射保存在本机 LocalAppData/PomeloMixxxMigration。

RAR 读取使用随包分发的 7-Zip。其版权和许可见 `_internal/7zip/License.txt`，包含 RAR 解压限制。迁移包输出使用 ZIP，无需 RAR 组件。

开发测试：`python -m unittest discover -s tools/ohos-migration -v`。构建：在已安装 PyInstaller 的 Python 环境执行 `build.ps1 -Python <python.exe>`，会复制本机 7-Zip 组件和许可。

> 本文由 AI Agent 自动生成，结束。
