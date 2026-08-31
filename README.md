# MotionBlur_K

![GitHub License](https://img.shields.io/github/license/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Last commit](https://img.shields.io/github/last-commit/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Downloads](https://img.shields.io/github/downloads/korarei/AviUtl2_ObjectMotionBlur_LK_Script/total)
[![GitHub Release][releases-badge]][releases-url]
[![AviUtl2 Catalog][catalog-badge]][catalog-url]

AviUtl ExEdit2 向け軽量モーションブラエフェクト．

以下の機能が追加される．

- ぼかし\ObjectMotionBlur_LK: オブジェクトに 2D モーションブラーをかける
- ぼかし\SceneMotionBlur_K: オプティカルフローに基づき画面全体にモーションブラーをかける

## 動作確認

- [AviUtl ExEdit2 v2.1.7a](https://spring-fragrance.mints.ne.jp/aviutl/)

> [!CAUTION]
> - v2.1.7a 以降必須．
> - `SceneMotionBlur_K` の利用には Turing 世代以降の NVIDIA GPU (GeForce RTX シリーズ / GTX 166x シリーズ等) が必要．

## 導入・更新・削除

### パッケージファイルからインストール

#### 導入・更新

[こちら][releases-url]からダウンロードした `*.au2pkg.zip` をAviUtl2にD&D．

#### 削除

パッケージ情報からアンインストールする．

### [AviUtl2 カタログ](https://github.com/Neosku/aviutl2-catalog)からインストール

[こちら][catalog-url]から導入，更新，削除を行う．

## 使い方

> [!WARNING]
> 連続したレンダリングが必要である．

### ObjectMotionBlur_LK

初期ラベル: `ぼかし`

トラックバーによる二次元座標変換に関して線形的にフレーム補間したモーションブラーをかける．

> [!NOTE]
> 非連続なフレームが処理された場合は全体キャッシュからデータを取得する．データが存在しない場合は内挿計算によって推定する．

#### 対象項目

出力項目 (標準描画等) の設定値は以下の 7 項目が対象．

拡大率と縦横比は X 軸方向と Y 軸方向の拡大率として取得している．

- X
- Y
- 中心X
- 中心Y
- Z軸回転
- 拡大率
- 縦横比

オブジェクトの設定値 (フィルタ効果などのエフェクトによるもの) は以下の 7 項目が対象．

- obj.ox
- obj.oy
- obj.cx
- obj.cy
- obj.rz
- obj.sx
- obj.sy

グループ制御の設定値は以下の 4 項目が対象．

- X
- Y
- Z軸回転
- 拡大率

#### パラメータ

<details>
<summary><b>Shutter</b></summary>

- Shutter::Angle: ブラーの範囲．360度で1フレーム移動量と等しい．
- Shutter::Phase: ブラーの位置． `Shutter::Angle` * -0.5 で中央．
- Shutter::Falloff::Edge: 減衰させる位置．
  - Trailing: 移動元側を減衰．
  - Leading: 移動先側を減衰．
  - Symmetric: 両端を均等に減衰．
- Shutter::Falloff::Amount: 減衰の割合．

</details>

<details>
<summary><b>Sampling</b></summary>

- Sampling::Viewport::Sample Limit: プレビューでの描画精度．サンプル数の上限値を設定する．
- Sampling::Render::Sample Limit: 出力時の描画精度．サンプル数の上限値を設定する．

</details>

<details>
<summary><b>Tint</b></summary>

- Tint::Source: グラデーションマップの参照元．
  - Image: 画像ファイル．
  - Layer: レイヤー．
- Tint::Image: グラデーションマップ画像のパス．
- Tint::Layer: グラデーションマップレイヤーの番号．

> **Note**
>
> グラデーションマップの仕様:
> - 横軸: 輝度
> - 縦軸: ブラー進捗

</details>

<details>
<summary><b>Compositing</b></summary>

- Compositing::Mix: オリジナル画像とブラーを合成する．
- Compositing::Alpha Mode: 出力のアルファ情報．
  - Alpha Blending: アルファブレンド．
  - Alpha Hashed: アルファハッシュ．

</details>

<details>
<summary><b>Additional Options</b></summary>

- Extrapolation: 0 フレームより前を仮想的に計算する．
  - None (計算しない)
  - Linear (1次補間)
  - Quadratic (2次補間)
- Layer Reference: レイヤーの参照方法．
  - Absolute: 絶対参照．
  - Relative: 相対参照．
- Resize: ブラーが見切れないように画像サイズを変更する．
- Diagnostics: コンソールに情報を表示する．

</details>

### SceneMotionBlur_K

初期ラベル: `ぼかし`

連続フレーム間のオプティカルフロー (ピクセル単位の動きベクトル) を推定し，映像全体にモーションブラーをかける．

> [!TIP]
> オブジェクトを分割するか中間点を追加することでシーンチェンジを明示すること．

> [!IMPORTANT]
> 出力シーンとこのエフェクトが存在するシーンを同じものにすること．(AviUtl2 の仕様上，必ずフレームジャンプが発生するため．)

#### パラメータ

<details>
<summary><b>Shutter</b></summary>

- Shutter::Angle: ブラーの範囲．360度で1フレーム移動量と等しい．
- Shutter::Falloff::Edge: 減衰させる位置．
  - Trailing: 移動元側を減衰．
  - Leading: 移動先側を減衰．
  - Symmetric: 両端を均等に減衰．
- Shutter::Falloff::Amount: 減衰の割合．

</details>

<details>
<summary><b>Sampling</b></summary>

- Sampling::Viewport::Sample Limit: プレビューでの描画精度．サンプル数の上限値を設定する．
- Sampling::Render::Sample Limit: 出力時の描画精度．サンプル数の上限値を設定する．

</details>

<details>
<summary><b>Compositing</b></summary>

- Compositing::Mix: オリジナル画像とブラーを合成する．

</details>

<details>
<summary><b>Depth</b></summary>

- Depth::Layer: 深度マップとして参照するレイヤーの番号．

</details>

<details>
<summary><b>Additional Options</b></summary>

- Preset: NVIDIA Optical Flow のプリセット．
  - Slow: 高品質．
  - Medium: 標準．
  - Fast: 高速．
- Layer Reference: レイヤーの参照方法．
  - Absolute: 絶対参照．
  - Relative: 相対参照．
- View: 描画・デバッグ用表示モード．
  - Processed: ブラー処理結果．
  - Flow: 推定されたオプティカルフロー．
  - Nearest Propagated Flow: 最近傍伝播フロー．
  - Distinct Propagated Flow: 異なるモーションの伝播フロー．

</details>

## ビルド方法

[リリース用ワークフロー](./.github/workflows/releaser.yml)を参照されたい．

## ライセンス

本プログラムのライセンスは [LICENSE](./LICENSE) を参照されたい．

また，本プログラムが利用するサードパーティ製ライブラリ等のライセンス情報は [THIRD_PARTY_LICENSES](./THIRD_PARTY_LICENSES.md) に記載している．

## 更新履歴

[CHANGELOG](./CHANGELOG.md) を参照されたい．

<!-- links -->

[releases-url]: https://github.com/korarei/AviUtl2_ObjectMotionBlur_LK_Script/releases
[releases-badge]: https://img.shields.io/github/v/release/korarei/AviUtl2_ObjectMotionBlur_LK_Script
[catalog-url]: https://aviutl2-catalog-badge.sevenc7c.workers.dev/package/korarei.ObjectMotionBlur_LK
[catalog-badge]: https://aviutl2-catalog-badge.sevenc7c.workers.dev/badge/v/korarei.ObjectMotionBlur_LK
