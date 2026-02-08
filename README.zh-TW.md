# NVFD — NVIDIA 風扇控制守護程式

[English](README.md)

NVFD 是一款開源的 Linux NVIDIA GPU 風扇控制守護程式。透過 NVML API 直接控制風扇，支援 **X11、Wayland 及無頭伺服器**，不需要 `nvidia-settings`。

## 功能特色

- **互動式 TUI 儀表板** — 執行 `nvfd` 即可啟動即時 GPU 監控與控制介面
- **互動式曲線編輯器** — ncurses 視覺化風扇曲線編輯器，支援滑鼠操作
- 自訂風扇曲線（線性插值），即時追蹤溫度調整轉速
- 固定轉速模式
- 自動模式（將控制權交還 NVIDIA 驅動程式）
- 多 GPU 支援，單卡或全卡控制，自適應全顯/分頁顯示
- 即時溫度、使用率、記憶體、功耗監控
- Systemd 服務，關機時自動重設風扇
- 透過 SIGHUP 熱載入設定
- 自動提權為 root（無需手動輸入 sudo）

## TUI 儀表板

```
 NVFD v1.0 ─ GPU Fan Control                                    [q] Quit
─────────────────────────────────────────────────────────────────────────
 GPU 0: NVIDIA GeForce RTX 4090
   Temp      45°C   [##############·················]
   GPU Use   78%    [########################·······]
   Memory    12.3 / 24.0 GB
   Power     285 / 450 W

   Fan 0     52%    [################···············]
   Fan 1     53%    [################···············]

   Mode:   Auto   Manual   Curve
─────────────────────────────────────────────────────────────────────────
 [Tab] GPU  [m] Mode  [M] All  [↑↓] Speed ±5  [e] Edit Curve  [q] Quit
```

> 多 GPU：當終端機視窗夠大時，所有 GPU 會同時顯示。視窗較小時，以分頁方式切換 GPU。

## 免責聲明

使用 NVFD 的風險由使用者自行承擔。不當的風扇轉速設定可能會損壞 GPU 或其他硬體。Infinirc 不對因使用本軟體造成的任何損害負責。

建議：
1. 避免將風扇轉速設定過低或過高。
2. 定期監控 GPU 溫度。
3. 如有異常，執行 `nvfd auto` 將風扇控制權交還驅動程式。

## 系統需求

- NVIDIA GPU 搭配官方 NVIDIA 驅動程式（非開源 nouveau）
- Linux 作業系統
- `libjansson-dev` — JSON 函式庫
- `libncursesw5-dev` — ncurses 寬字元支援
- NVML 標頭檔（包含在 CUDA toolkit 或 `nvidia-cuda-toolkit` 套件中）

## 安裝

### 從原始碼安裝（建議）

```bash
git clone https://github.com/Infinirc/nvfd.git
cd nvfd
sudo scripts/install.sh
```

安裝腳本會自動：
- 偵測作業系統並安裝編譯所需依賴
- 編譯程式
- 安裝至 `/usr/local/bin/nvfd`
- 設定 systemd 服務
- 自動遷移 v1.x 舊設定

### 手動編譯

```bash
make
sudo make install
sudo systemctl enable --now nvfd.service
```

## 解除安裝

```bash
sudo scripts/uninstall.sh
```

或手動執行：
```bash
sudo systemctl stop nvfd.service
sudo systemctl disable nvfd.service
sudo make uninstall
```

設定檔 `/etc/nvfd/` 會被保留，如需移除請手動刪除。

## 使用方式

```
nvfd                       互動式 TUI 儀表板（需在終端機執行）
nvfd auto                  將風扇控制權交還 NVIDIA 驅動程式
nvfd curve                 啟用自訂風扇曲線
nvfd curve <溫度> <轉速>   編輯風扇曲線控制點（例：nvfd curve 60 70）
nvfd curve show            顯示目前風扇曲線
nvfd curve edit            互動式曲線編輯器（ncurses）
nvfd curve reset           重設風扇曲線為預設值
nvfd <轉速>                設定所有 GPU 固定轉速（30-100）
nvfd <GPU編號> <轉速>      設定指定 GPU 固定轉速
nvfd list                  列出所有 GPU
nvfd status                顯示目前狀態
nvfd -h                    顯示說明
```

在終端機中不帶參數執行 `nvfd` 會啟動互動式 TUI 儀表板。
透過 systemd 啟動時（非 TTY）會自動進入守護程式模式。

### TUI 儀表板快捷鍵

| 按鍵 | 功能 |
|------|------|
| `Tab` / `Shift-Tab` | 切換 GPU（多 GPU 時）|
| `m` | 循環切換選取 GPU 模式：Auto → Manual → Curve → Auto |
| `M` | 一次切換所有 GPU 模式 |
| `↑` / `↓` | 調整轉速 ±5%（手動模式）|
| `PgUp` / `PgDn` | 調整轉速 ±10%（手動模式）|
| `e` | 開啟曲線編輯器（曲線模式）|
| `q` | 退出（有修改時會詢問是否儲存）|

### 曲線編輯器快捷鍵

| 按鍵 | 功能 |
|------|------|
| `←` / `→` | 調整溫度 ±5°C |
| `↑` / `↓` | 調整轉速 ±5% |
| `t` | 直接輸入溫度數值 |
| `f` | 直接輸入轉速數值 |
| `a` | 新增控制點 |
| `d` | 刪除選取的控制點 |
| `Tab` | 選取下一個控制點 |
| `s` | 儲存並退出 |
| `r` | 重設為預設曲線 |
| `q` | 退出（有修改時會詢問是否儲存）|

### 模式說明

| 模式 | 說明 |
|------|------|
| `auto` | 將風扇控制權交還 NVIDIA 驅動程式，完全由驅動管理。|
| `curve` | 使用自訂溫度對轉速曲線控制風扇。|
| `manual` | 將風扇鎖定在固定百分比（透過 `nvfd <轉速>` 設定）。|

### 使用範例

```bash
# 啟動互動式 TUI 儀表板
nvfd

# 設定所有風扇為 80%
nvfd 80

# 設定 GPU 0 為 60%
nvfd 0 60

# 所有風扇交還驅動程式控制
nvfd auto

# 使用自訂風扇曲線
nvfd curve
nvfd curve show
nvfd curve 50 60    # 50°C 時風扇轉速 60%
nvfd curve edit     # 互動式曲線編輯器
nvfd curve reset    # 還原預設曲線

# 查看狀態
nvfd status
nvfd list
```

## 設定檔

設定檔儲存在 `/etc/nvfd/`：

| 檔案 | 用途 |
|------|------|
| `config.json` | 每張 GPU 的模式設定（auto / manual / curve）|
| `curve.json` | 風扇曲線控制點（溫度 → 轉速 %）|

### 風扇曲線格式

```json
{
    "30": 30,
    "40": 40,
    "50": 55,
    "60": 65,
    "70": 85,
    "80": 100
}
```

## Systemd 服務

```bash
sudo systemctl start nvfd      # 啟動風扇控制守護程式
sudo systemctl stop nvfd       # 停止（風扇重設為自動）
sudo systemctl restart nvfd    # 重新啟動
sudo systemctl reload nvfd     # 重新載入設定（SIGHUP）
sudo systemctl status nvfd     # 查看狀態
```

守護程式關閉時會自動將所有風扇重設為驅動程式控制的自動模式。

## 從 v1.x 遷移

NVFD 會自動遷移舊版設定：

| 舊版 | 新版 |
|------|------|
| `/usr/local/bin/infinirc_gpu_fan_control` | `/usr/local/bin/nvfd` |
| `/etc/infinirc_gpu_fan_control.conf` | `/etc/nvfd/config.json` |
| `/etc/infinirc_gpu_fan_curve.json` | `/etc/nvfd/curve.json` |
| `infinirc-gpu-fan-control.service` | `nvfd.service` |

## 支援

如遇到任何問題，請[建立 Issue](https://github.com/Infinirc/nvfd/issues)。

## 授權條款

MIT License — 詳見 [LICENSE](LICENSE)。
