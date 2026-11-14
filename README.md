# AI 作業ランプ

Claude Code に作業を頼んでいる間の状態を、USB でつないだランプの光で知らせる。Claude Code のプラグインと、ランプのファームウェア（Arduino）。

| 状態 | 光り方 | いつ |
|---|---|---|
| 作業中 | 青い光がくるくる回る | 指示を送ったとき・ツールを使うたび |
| 完了 | 緑で点きっぱなし | 返事が終わったとき |
| 許可待ち | 黄色で点滅 | 許可を求めて止まっているとき |
| 消える | | Claude Code を終了したとき・ランプのボタンを短く押したとき |

ボタンを長押しすると明るさが 1 段暗くなる（100% → 60% → 35% → 20% → 10% → 100%）。明るさは覚えている。

## 使い方

ランプを USB で PC に挿して、Claude Code で:

```
/plugin marketplace add robohase/ai-lamp
/plugin install ai-lamp@robohase
```

入れたら Claude Code を起動し直す。

Linux（Ubuntu など）は最初に 1 回だけ、USB シリアルに書き込めるようにする:

```bash
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="c550", MODE="0666"' | sudo tee /etc/udev/rules.d/99-ai-lamp.rules
sudo udevadm control --reload && sudo udevadm trigger
```

Mac はそのまま使える（未確認）。Windows は Git for Windows（Claude Code でもすすめられているもの）が入っていれば使える（未確認）。

Claude Code を同時にいくつも動かしているときは、どれかが許可待ちなら黄色、どれかが作業中なら青、全部終わったら緑になる。

## ほかのツールから光らせる

ランプは USB シリアルで `work` / `done` / `wait` / `off` を 1 行送るとその状態になる（返事はしない）。`?` を送ると今の状態と明るさが返ってくる（例: `work 60%`）。

```bash
echo work > /dev/serial/by-id/usb-Deqing_CH55xduino_*
```

## 光り方を変える

`firmware/ai_lamp/ai_lamp.ino` がファームウェアのソース（[ch55xduino](https://github.com/DeqingSun/ch55xduino)）。

1. Arduino IDE の「基本設定」→「追加のボードマネージャの URL」に `https://raw.githubusercontent.com/DeqingSun/ch55xduino/ch55xduino/package_ch55xduino_mcs51_index.json` を足し、ボードマネージャで「CH55xDuino MCS51」を入れる
2. ボード「CH552 Board」、Clock Source「24 MHz (internal), 5V」、USB Settings「Default CDC」を選ぶ
3. ランプを挿したまま「書き込み」

元に戻すときは、このリポジトリの `ai_lamp.ino` をそのまま書き込む。書き込めなくなったときは、ボタンを押しながら USB を挿すと書き込みモードになる。

## ライセンス

MIT
