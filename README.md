# SimDistanceUnlock

Minecraft Bedrock の演算距離(simulation distance)を、自由に設定できるようにする mod です。
**1.26.60.4(arm64)専用**。別のビルドでは何も書き換えず、ロードが失敗として終わります。

## 何を変えるか(バイナリ解析で確認した3か所)
0. 端末ごとの上限値を返す関数(`AppPlatform_android` の仮想関数) … 元は端末の種別・メモリ量で 4〜12 を返す(多くのスマホで 8)
   設定画面もワールドに入る処理もこの値を参照するため、ここが「ワールドに入ると 8 に戻る」原因
   → 常に `kMaxDistance` を返すように置き換え
1. 設定画面の選択肢の一覧 … 元は「4, 6, 8, … 上限まで(2刻み)」
   → 開始値・刻みを置き換え(既定: 1〜64を1刻み)
2. ワールド(level.dat)読み込み時の `serverChunkTickRange` の補正 … 元は 4〜12 に収める
   → 補正の下限・上限を置き換え

## 範囲の変え方
`src/main.cpp` 冒頭の3つの定数を編集します。
```cpp
constexpr uint32_t kMinDistance = 1;  // 最小値
constexpr uint32_t kMaxDistance = 64; // 最大値(4以上)
constexpr uint32_t kStep = 1;         // 一覧の刻み
```

## 安全設計
- 書き換え位置は署名で探し、元の命令であることを確認してから書きます。
- 全ての箇所が一致したときだけ書き込みます(1つでも不一致なら何も書きません)。

## 注意
- 大きい値ほどメモリとCPUを使います。メモリ不足で落ちる端末では、上げすぎないでください。
- 他にも制限(サーバー側・Realms など)がある可能性があります。画面に選択肢が出ても、実際に反映されるかは実機で確認が必要です。

## ビルド
GitHub に push → Actions の build → Artifacts `SimDistanceUnlock-levipack` を取得 → 中の `SimDistanceUnlock.levipack` をインポート。
ログ: logcat タグ `SimDistanceUnlock`
