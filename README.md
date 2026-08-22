# ObjectMotionBlur_LK

![GitHub License](https://img.shields.io/github/license/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Last commit](https://img.shields.io/github/last-commit/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Downloads](https://img.shields.io/github/downloads/korarei/AviUtl2_ObjectMotionBlur_LK_Script/total)
[![GitHub Release][releases-badge]][releases-url]
[![AviUtl2 Catalog][catalog-badge]][catalog-url]

AviUtl ExEdit2 向け軽量モーションブラエフェクト．

以下の機能が追加される．

- ぼかし\\ObjectMotionBlur_LK: オブジェクトに 2D モーションブラーをかける

## 動作確認

- [AviUtl ExEdit2 v2.1.6](https://spring-fragrance.mints.ne.jp/aviutl/)

> [!CAUTION]
> v2.1.6 以降必須．

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
> 連続したレンダリングが必要である．非連続なフレーム取得や出力対象のシーンとは異なるシーンでの使用は正しく動作しない．

### ObjectMotionBlur_LK

初期ラベル: `ぼかし`

トラックバーによる二次元座標変換に関して線形的にフレーム補間したモーションブラーをかける．

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

- <details>
  <summary>Shutter</summary>

  - Shutter::Angle: ブラーの範囲．360度で1フレーム移動量と等しい．
  - Shutter::Phase: ブラーの位置． `Shutter::Angle` * -0.5 で中央．

  </details>

- <details>
  <summary>Sampling</summary>

  - Sampling::Viewport::Sample Limit: プレビューでの描画精度．サンプル数の上限値を設定する．
  - Sampling::Render::Sample Limit: 出力時の描画精度．サンプル数の上限値を設定する．

  </details>

- <details>
  <summary>Compositing</summary>

  - Compositing::Mix: オリジナル画像とブラーを合成する． `50.00` で標準モーションブラーの `残像` に近い挙動になる．
  - Compositing::Falloff: ブラーウェイトの勾配． `100.00` にすると標準モーションブラーエフェクトに近い挙動になる．

  </details>

- <details>
  <summary>Additional Options</summary>

  - Extrapolation: 0 フレームより前を仮想的に計算する．
    - None (計算しない)
    - Linear (1次補間)
    - Quadratic (2次補間)
  - Resize: ブラーが見切れないように画像サイズを変更する．
  - Diagnostics: コンソールに情報を表示する．

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
