# UEG 数字生命卡 · 固件与源码发布

> 为 [FoloToy AI Passport](https://ai-passport.folotoy.cn/)（ESP32-C3, 8 MB Flash）打造的随身「数字生命卡」固件。
> 翻页即可查看各 AI 平台余额、今日消费、月度用量热力图，还有 F1 下一站倒计时与积分榜。
> 官方玩法页：https://ai-passport.folotoy.cn/plays/342/

**版本**：`WE-TOKEN v1.0.0` ｜ **构建日期**：2026-09-13 ｜ **目标芯片**：ESP32-C3（8 MB Flash）
**构建校验**：`tools/validate.sh` 全绿（仓库检查 / 宿主测试 / 固件布局 / 合并镜像）

---

## 这是什么

把 AI Passport 变成一块随身的「数字生命卡」：

- **身份卡** — 你的 UEG 数字生命卡主页面
- **余额看板** — 各 AI 平台还剩多少额度、今天花了多少
- **用量热力** — 最近一个月的用量热力图
- **F1 看板** — 下一站倒计时、车手/车队积分榜、F1 新闻（北京时间显示）
- **连接设置** — Wi-Fi 与平台 API Key 配置

用上下键翻页，共五页。在连接设置页按 OK 即可打开配置热点。

> 完整源码就在本仓库 `main` 分支，可直接浏览；Release 里附的 `ueg-token-src-*.tar.gz` 是同一份源码的打包快照。

## 仓库结构

| 目录 | 说明 |
|---|---|
| `main/` | 应用主代码（五页 UI、网络、配置热点） |
| `components/` `bootloader_components/` | 自定义组件与引导组件 |
| `assets/` `docs/` | 品牌素材与文档 |
| `tools/` `tests/` | 校验脚本与测试 |

## 包里有什么（Release 资产）

| 文件 | 说明 |
|---|---|
| `FoloToy-AI-Passport-full.bin` | **整包镜像**（2 866 528 B），从 `0x0` 烧写，含 bootloader + 分区表 + 应用。日常刷机用这个。 |
| `FoloToy-AI-Passport-app-only.bin` | **仅应用**（2 800 928 B），从 `0x10000` 烧写，保留现有引导与分区表，更快。 |
| `ueg-token-src-20260913.tar.gz` | 完整源码树（含 `tests/`、`tools/`、`docs/`），不含 `build/`、`managed_components/`、`.git/`，可由 `idf.py build` 重建。 |
| `SHA256SUMS.txt` | 上述文件的 SHA256 校验和。 |
| `flash.sh` | 一键烧写脚本（自动校验 SHA256 并调用 esptool，自动挑整包/仅应用）。 |

## 烧写

```bash
# 方式一：附带的脚本（自动校验 SHA256 并调用 esptool）
./flash.sh /dev/ttyACM0            # 默认整包
./flash.sh /dev/ttyACM0 app        # 只更新应用

# 方式二：手动整包
esptool.py --chip esp32c3 --baud 921600 \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  write_flash 0x0 FoloToy-AI-Passport-full.bin

# 方式三：手动仅应用
esptool.py --chip esp32c3 --baud 921600 \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  write_flash 0x10000 FoloToy-AI-Passport-app-only.bin
```

### ⚠️ 烧写前须知

- **整包会覆盖 NVS 分区**（`0x9000`）→ 设备里已保存的 Wi-Fi 与平台 Key 会被清空，需要重新配网。想保留配置就用「仅应用」方式。
- **受保护区域不受影响**：整包镜像只写到 `0x2bbd60`，而 `cardid` 分区在 `0x356000`、`recovery` 在 `0x700000`，均在写入范围之外，**不会被覆盖**。校验脚本 `tools/verify_firmware.py` 会检查这一点。

## 首次开机配置

设备未配网时会自动开热点：

1. 手机连热点 **`UEG-TOKEN-XXXX`**（后 4 位取自设备 MAC）
2. 密码：`ordinarylife`
3. 浏览器打开 `http://192.168.4.1`
   - 建议用无痕窗口，避免旧缓存
   - 保存按钮固定在页面顶部，不用往下滑
4. 填 Wi-Fi 与平台 API Key → 保存 → 设备自动重启并联网刷新

联网后翻到第 4 页即可看到 F1 下一站、积分榜与新闻。

## 从源码重建

```bash
tar xzf ueg-token-src-20260913.tar.gz
cd ueg-token

# 需要 ESP-IDF v5.5（managed_components 自动拉取，版本由 dependencies.lock 锁定，可复现）
idf.py build

# 跑完整校验门（仓库检查 + 宿主测试 + 固件布局）
./tools/validate.sh

# 生成整包镜像
idf.py merge-bin -o "$(pwd)/build/FoloToy-AI-Passport-full.bin"
```

> 注意：`idf.py merge-bin` 的 `-o` 是相对 **build 目录**解析的，所以要用绝对路径，写相对路径会报 `FileNotFoundError`。

## F1 数据来源

| 数据 | 端点 | 说明 |
|---|---|---|
| 下一站 | `api.jolpi.ca/ergast/f1/current/next.json` | 只有 765 B；整季赛历 14 KB 且要先扫过 60% 才轮到下一站。 |
| 车手榜 | `.../driverstandings.json?limit=5` | 加 `limit` 后 2.4 KB（不加是 10 KB）。 |
| 车队榜 | `.../constructorstandings.json?limit=5` | 1.3 KB。 |
| 新闻 | `formula1.com/en/latest/all.xml` | BBC 的 RSS 在开发网络不可达，未采用。 |

全部换算为北京时间显示。

## License

固件与源码按「现状」提供，用于 FoloToy AI Passport 社区玩法交流。