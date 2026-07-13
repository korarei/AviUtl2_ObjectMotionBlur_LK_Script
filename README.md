# ObjectMotionBlur_LK

![GitHub License](https://img.shields.io/github/license/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Last commit](https://img.shields.io/github/last-commit/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Downloads](https://img.shields.io/github/downloads/korarei/AviUtl2_ObjectMotionBlur_LK_Script/total)
![GitHub Release](https://img.shields.io/github/v/release/korarei/AviUtl2_ObjectMotionBlur_LK_Script)

オブジェクトにモーションブラーをかけるスクリプト．

[ダウンロードはこちらから](https://github.com/korarei/AviUtl2_ObjectMotionBlur_LK_Script/releases)

## 動作確認

- [AviUtl ExEdit2 beta22](https://spring-fragrance.mints.ne.jp/aviutl/)

> [!CAUTION]
> beta20以降必須．

## 導入・削除・更新

初期配置場所は`ぼかし`である．

`オブジェクト追加メニューの設定`から`ラベル`を変更することで任意の場所へ移動可能．

### 導入

1.  同梱の`*.anm2`と`*.mod2`を`%ProgramData%`内の`aviutl2\Script`フォルダまたはその子フォルダに入れる．

`beta4`以降では`aviutl2.exe`と同じ階層内の`data\Script`フォルダ内でも可．

### 削除

1.  導入したものを削除する．

### 更新

1.  導入したものを上書きする．

## 使い方

オブジェクトにこのスクリプトを追加することで，トラックバーによる移動に関して線形的にフレーム補間したモーションブラーをかける．

また，追加エフェクト，スクリプトによる座標変化はデータを保持することにより計算で扱うことが可能である．

### 対象項目

出力項目 (標準描画等) の設定値は以下の7項目が対象．

拡大率と縦横比はX軸方向とY軸方向の拡大率として取得している．

- X
- Y
- 中心X
- 中心Y
- Z軸回転
- 拡大率
- 縦横比

オブジェクトの設定値 (基本効果やスクリプトなどの追加エフェクトによるもの) は以下の7項目が対象．

これらの項目は`Geo Cache`が`Full`か`Minimal`の時に有効．

- obj.ox
- obj.oy
- obj.cx
- obj.cy
- obj.rz
- obj.sx
- obj.sy

### パラメータ

#### Shutter Angle

ブラー幅 (360度で1フレーム移動量と等しい)．

初期値は`180.0`で一般的な値を採用している．

#### Sample Limit

描画精度．サンプル数の上限値を設定する．

上げると描画が綺麗になる代わりに重くなる．一方，下げると描画が粗くなる代わりに軽くなる．

必要サンプル数はダイアログ内の`Print Information`を有効にするとコンソールで確認できる．

初期値は`256`とやや小さい値にしている．

> [!NOTE]
> `2`未満のときブラーは表示されない．

#### Preview Limit

プレビュー時の描画精度．`0`以外の値にすることで，編集時に描画制度を下げて軽量にすることができる．出力時は`Sample Limit`の値になる．

初期値は`0`でこの機能を無効にしている．

#### Extrapolation

0フレームより前を仮想的に計算する．計算方法として以下の3つある．

- None (計算しない)
- Linear (1次補間)
- Quadratic (2次補間)

初期値は`Quadratic`

#### Resize

サイズを変更．`ON`でブラーが見切れないようにする．

初期値は`ON`

#### Mix

元画像を元の位置に描画する．(アルファブレンド)

かつての標準モーションブラーエフェクトの`残像`のようなもの．

初期値は`0.0`

#### Geo Cache

エフェクトによる座標変化を計算に入れるかどうかを指定する．保存方法は以下の3つ．

- None (保存しない)
- Full (全フレーム保存する)
- Minimal (必要最低限だけ保存する)

初期値は`None`

#### Cache Purge

キャッシュ削除に関して以下の4つから設定する．

- None (特に何も行わない)
- Auto (このオブジェクトの最終フレームでこのオブジェクトのデータのみ削除する)
- All (スクリプトが読み込まれた時，すべて削除する)
- Active (スクリプトが読み込まれた時，このオブジェクトのデータのみ削除する)

初期値は`None`

#### Print Information

コンソールに情報を表示する．

表示される情報は以下のとおり

- Object ID (所謂`obj.id`．キャッシュはObject IDごとに保存される．)
- Index (所謂`obj.index`．個別オブジェクトのインデックス．)
- Required Samples (必要なサンプル数．これを目安に`Sample Limit`を設定してほしい．)

初期値は`OFF`

## ビルド方法

[リリース用ワークフロー](./.github/workflows/releaser.yml)を参照されたい．

## ライセンス

本プログラムのライセンスは [LICENSE](./LICENSE) を参照されたい．

また，本プログラムが利用するサードパーティ製ライブラリ等のライセンス情報は [THIRD_PARTY_LICENSES](./THIRD_PARTY_LICENSES.md) に記載している．

## 更新履歴

[CHANGELOG](./CHANGELOG.md) を参照されたい．
