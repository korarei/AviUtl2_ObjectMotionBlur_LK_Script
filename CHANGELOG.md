# Changelog

## v2.0.1

- `ObjectMotionBlur_LK` へ UI 非表示ルールを追加
- `ObjectMotionBlur_LK` の計算結果で得るサンプル数下限を 2 に変更
- `SceneMotionBlur_K` のフレームキャッシュ更新処理を修正

## v2.0.0

> [!CAUTION]
> v1.x.x との互換性はない．

- `ObjectMotionBlur_LK` をフィルタプラグイン化
- `SceneMotionBlur_K` を追加

以下は `ObjectMotionBlur_LK` の変更点

- グループ制御に対応
- 時間制御に仮対応
- キャッシュ ID を Object ID から Effect ID に変更
- `Geo Cache` を常に有効として項目を削除
- `Cache Purge` を本体のキャッシュ破棄と連動させ項目を削除
- `Shutter::Phase` を追加
- `Shutter::Falloff` を追加
- `Tint` を追加
- `Compositing::Alpha Mode` を追加
- `Extrapolation` の挙動を調整
- `Mix` の挙動を調整
- 拡大動作時ブラーがかからない問題の修正
- 0 フレーム目の Geometry データを 1 つしか PF へ保存していなかった問題を修正 (個別オブジェクト 2500 個まで対応)
- UI を整理

## v1.1.1

- UI のグルーピング (beta22 以降必要)
- `Cache Purge` が `Auto` のときの条件分岐を修正

## v1.1.0

- `.mod2` 化
- 縦横比変形に対応
- 外挿計算結果をプロジェクトファイルに埋め込むようにした
- `Cache Control` を `Cache Purge` に変更 (破壊的)

## v1.0.0

- `Object ID` をスクリプト側で入手できるように変更
- `Print Information` で表示される `Required Samples` が 1 少なかった問題の解決

## v0.2.2

- lua の `require` から呼び出せるように変更
- リサイズ計算の精度向上
- `PI` 項目名の間違いを修正

## v0.2.1

- 平均計算ミスの修正
- リサイズ計算で中心座標に対してブラー量を考慮していなかった問題の修正
- Geometry データの保存クラスに修飾子追加

## v0.2.0

- 中心座標をスクリプト側で入手できるように変更
- サンプル数計算をリサイズ量に基づいて計算するように変更
- `Print Information` で表示される必要サンプル数が描画時のサンプル数であった問題を修正
- Geometry データの保存方法を変更

## v0.1.0

- Release
