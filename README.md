# ObjectMotionBlur_LK

![GitHub License](https://img.shields.io/github/license/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Last commit](https://img.shields.io/github/last-commit/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Downloads](https://img.shields.io/github/downloads/korarei/AviUtl2_ObjectMotionBlur_LK_Script/total)
![GitHub Release](https://img.shields.io/github/v/release/korarei/AviUtl2_ObjectMotionBlur_LK_Script)

オブジェクトにモーションブラーをかけるスクリプト．

[ダウンロードはこちらから](https://github.com/korarei/AviUtl2_ObjectMotionBlur_LK_Script/releases)

## 動作確認

- [AviUtl ExEdit2 beta21](https://spring-fragrance.mints.ne.jp/aviutl/)

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

プレビュー時の描画制度．`0`以外の値にすることで，編集時に描画制度を下げて軽量にすることができる．出力時は`Sample Limit`の値になる．

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

#### Mix

元画像を元の位置に描画する．(アルファブレンド)

かつての標準モーションブラーエフェクトの`残像`のようなもの．

初期値は`0.0`

#### Print Information

コンソールに情報を表示する．

表示される情報は以下のとおり

- Object ID (所謂`obj.id`．キャッシュはObject IDごとに保存される．)
- Index (所謂`obj.index`．個別オブジェクトのインデックス．)
- Required Samples (必要なサンプル数．これを目安に`Sample Limit`を設定してほしい．)

初期値は`OFF`

#### PI

パラメータインジェクション．

```lua
{
  shutter_angle = 180.0, -- 360.0を超える値も指定可能 (ただ伸ばすだけ)
  render_sample_limit = 256,
  preview_sample_limit = 0,
  extrapolation = 2,
  resize = true, -- booleanも可
  geo_cache = 0,
  cache_purge = 0,
  mix = 0.0,
  print_info = false, -- booleanも可
}
```

`{}`は既に挿入済みであるため，PI項目では中身のみ記載する．

## スクリプトモジュール

### version 関数

スクリプトモジュールのバージョンを返す．

#### 戻り値

1. `version` (number) : バージョン情報 

### compute_motion 関数

座標データから同次変換行列等を求める．

> [!CAUTION]
> `obj.num`が1以上の場合にのみ使用可能．

#### 引数

1. `params` (table) : 設定値
1. `context` (table) : オブジェクトの情報等
1. `xform_curr` (table) : 現在の描画基準座標
1. `xform_prev` (table) : 過去の描画基準座標
1. `geo_curr` (table) : 現在のオブジェクト設定値 (座標)
1. `data` (userdata, option) : 汎用データ (64バイト)
1. `size` (number, option) : 汎用データサイズ

> [!IMPORTANT]
> `data`を渡すときは必ず`size`を渡す必要がある．

#### 戻り値

1. `maegin` (table) : 領域拡張量
1. `samples` (number) : サンプリング数 (必要サンプル数より1小さい)
1. `xform_matrix` (table) : 1つ目のサンプリング地点までの同次変換行列の逆行列
1. `scaling_matrix` (table) : 1つ目のサンプリング地点でのスケーリング行列の逆行列
1. `drift_vector` (tavle) : 1つ目のサンプリング地点での中心座標ずれの逆ベクトル

> [!NOTE]
> 行列，ベクトルは列優先で一次元配列である．
>
> 行列は3次元正方行列，ベクトルは3次元ベクトルである．
>
> `samples`が0のとき，単位行列，0ベクトルとなる．

> [!TIP]
> `xform_matrix`，`scaling_matrix`，`drift_vector`について．
>
> 現在位置 $\Sigma_0$ から1つ目のサンプリング位置 $\Sigma_1$ への変換を ${}^0T_1$ とし， $\Sigma_1$ からターゲット座標 $\Sigma_t$ への変換を ${}^1S_t$ とする．
>
> 中心 $\boldsymbol{V}$ とズレ量 $\boldsymbol{d}$ を用いると， $\Sigma_t$ での中心座標 ${}^t\boldsymbol{V}$ は以下のように表現できる．
>
> $$
> {}^t\boldsymbol{V} = \boldsymbol{V} + \boldsymbol{d}
> $$
>
> このとき， $\Sigma_0$ から見た ${}^t\boldsymbol{V}$ である ${}^0\boldsymbol{V}$ は以下のように表現できる．
>
> $$
> {}^0\boldsymbol{V} = {}^0T_1 {}^1S_t {}^t\boldsymbol{V}
> $$
>
> 以上よりシェーダーではこの逆変換を行えばよいので，
>
> $$
> {}^t\boldsymbol{V} = {}^1S_t^{-1} {}^0T_1^{-1} {}^0\boldsymbol{V} = {}^tS_1 {}^1T_0 {}^0\boldsymbol{V}
> $$
>
> ここで， ${}^1T_0$ は`xform_matrix`， ${}^tS_1$ は`scaling_matrix`， $-\boldsymbol{d}$ は`drift_vector`である．

#### テーブル

```lua
local params = {
  amt = 1.0, -- shutter_angle / 360.0
  smp_lim = 256,
  ext = 2,
  geo_cache = 0,
  cache_purge = 0,
  print_info = false
}

local context = {
  name = "SCRIPT_NAME",
  w = obj.w,
  h = obj.h,
  cx = obj.getvalue("cx") + obj.cx,
  cy = obj.getvalue("cy") + obj.cy,
  id = obj.id,
  idx = obj.index,
  num = obj.num,
  frame = obj.frame,
  range = obj.totalframe
}

local xform = {
  cx = obj.getvalue("cx"),
  cy = obj.getvalue("cy"),
  x = obj.getvalue("x"),
  y = obj.getvalue("y"),
  rz = obj.getvalue("rz"),
  sx = obj.getvalue("sx"),
  sy = obj.getvalue("sy")
}

local geo = {
  cx = obj.cx,
  cy = obj.cy,
  ox = obj.ox,
  oy = obj.oy,
  rz = obj.rz,
  sx = obj.sx,
  sy = obj.sy
}

local margin = {
  left = 0,
  top = 0,
  right = 0,
  bottom = 0
}
```

##  ビルド方法

`.github/workflows`内の`releaser.yml`に記載．

## License
LICENSEファイルに記載．

## Credits

### AviUtl ExEdit2 Plugin SDK

https://spring-fragrance.mints.ne.jp/aviutl/

---

The MIT License

Copyright (c) 2025 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Change Log
- **v1.1.0**
  - `.mod2`化．
  - 縦横比変形に対応．
  - 外挿計算結果をプロジェクトファイルに埋め込むようにした．
  - `Cache Control`を`Cache Purge`に変更．(破壊的)

- **v1.0.0**
  - `Object ID`をスクリプト側で入手できるように変更．
  - `Print Information`で表示される`Required Samples`が1少なかった問題の解決．

- **v0.2.2**
  - luaの`require`から呼び出せるように変更．
  - リサイズ計算の精度向上．
  - `PI`項目名の間違いを修正．

- **v0.2.1**
  - 平均計算ミスの修正．
  - リサイズ計算で中心座標に対してブラー量を考慮していなかった問題の修正．
  - Geometryデータの保存クラスに修飾子追加．

- **v0.2.0**
  - 中心座標をスクリプト側で入手できるように変更．
  - サンプル数計算をリサイズ量に基づいて計算するように変更．
  - `Print Information`で表示される必要サンプル数が描画時のサンプル数であった問題を修正．
  - Geometryデータの保存方法を変更．

- **v0.1.0**
  - Release
