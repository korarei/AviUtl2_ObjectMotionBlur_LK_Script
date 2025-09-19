# ObjectMotionBlur_LK

![GitHub License](https://img.shields.io/github/license/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Last commit](https://img.shields.io/github/last-commit/korarei/AviUtl2_ObjectMotionBlur_LK_Script)
![GitHub Downloads](https://img.shields.io/github/downloads/korarei/AviUtl2_ObjectMotionBlur_LK_Script/total)
![GitHub Release](https://img.shields.io/github/v/release/korarei/AviUtl2_ObjectMotionBlur_LK_Script)

オブジェクトにモーションブラーをかけるスクリプト．

[ダウンロードはこちらから](https://github.com/korarei/AviUtl2_ObjectMotionBlur_LK_Script/releases)

## 動作確認

- [AviUtl ExEdit2 beta11a](https://spring-fragrance.mints.ne.jp/aviutl/)

## 導入・削除・更新

初期配置場所は`ぼかし`である．

`オブジェクト追加メニューの設定`から`ラベル`を変更することで任意の場所へ移動可能．

### 導入

1.  同梱の`*.anm2`と`*.dll`を`%ProgramData%`内の`aviutl2\\Script`フォルダまたはその子フォルダ (英語) に入れる．

`beta4`以降では`aviutl2.exe`と同じ階層内の`data\\Script`フォルダ内でも可．

### 削除

1.  導入したものを削除する．

### 更新

1.  導入したものを上書きする．

## `ObjectMotionBlur@MotionBlur_K`との相違点

このスクリプトは[ObjectMotionBlur@MotionBlur_K](https://github.com/korarei/AviUtl_MotionBlur_K_Script)の劣化移植版である．いくつかの機能が削除，制限，追加されている．

### 削除項目

計算が複雑になる上にそこまで正確でない項目を削除した．

- Shutter Angle (`[360.0, 720]`): 非現実的な数値を認めない．

- Shutter Phase: `-shutter_angle`で固定．

### 制限項目

`beta11a`で提供されているライブラリのみを使用したため制限された．

- Object ID (オブジェクト固有な値)

- Center

### 追加項目

新たに追加した機能．

- Extrapolation (旧`Calc -1F & -2F`): かつては2次補間だけだったが1次補間できるようにした．

- Mix (旧`Mix Original Image`): アルファ値にした．

- 中心座標の移動に対してブラーをかけるようにした．

基本的な使い方は同じだが，これらの機能差に注意してほしい．

## 使い方

オブジェクトのX，Y，拡大率，Z軸回転，中心X，中心Yのトラックバーによる移動に関して線形的モーションブラーをかける．ただしbeta11a現在，中心座標を入手する方法がないので，中心X，中心Yはスクリプト内トラックバーにて再度設定する必要がある．

また，Geometryのうち，ox，oy，zoom，rz，cx，cyに関してはデータを保持することにより，計算で扱うことが可能である． (基本効果系エフェクトやDelayMove，全自動リリックモーションなど)

- Shutter Angle

  ブラー幅 (360度で1フレーム移動量と等しい)．

  初期値は`180.0`で一般的な値を採用している．

- Sample Limit

  描画精度．このスクリプトは移動量に応じた可変サンプル数を採用している．ここでは，そのサンプル数の上限値を設定する．PCの重さに関わるため適切に設定してほしい．
  
  必要サンプル数はダイアログ内の`Print Information`を有効にするとコンソールで確認できる．

  最小必要サンプル数は`2`．

  初期値は`256`とやや小さい値にしている．

- Preview Limit

  プレビュー時の描画制度．`0`以外の値にすることで，編集時に描画制度を下げて軽量にすることができる．出力時は`Sample Limit`の値になる．

  初期値は`0`でこの機能を無効にしている．

- Extrapolation

  0フレームより前を仮想的に計算する．計算方法として以下の3つある．

    - None (計算しない)

    - Linear (1次補間)

    - Quadratic (2次補間)

  初期値は`Quadratic`

- Resize

  サイズを変更．`ON`でブラーが見切れないようにする．

  初期値は`ON`

- Geo Cache

  スクリプトによる座標変化を計算に入れるかどうかを指定する．保存方法は以下の3つ．

  - None (保存しない)

  - Full (全フレーム保存する)

  - Minimal (必要最低限だけ保存する)

  初期値は`None`

- Cache Control

  ジオメトリデータの扱いを指定する．コントロール方法は以下の4つ．

  - Off (特に何も行わない)

  - Auto (オブジェクトの最終フレームに現在オブジェクトのデータのみ削除する)

  - All (次スクリプトが読み込まれた時，すべて削除する)

  - Current (次スクリプトが読み込まれた時，現在オブジェクトのデータのみ削除する)

  初期値は`Off`

- Object ID

  オブジェクト固有の値を入力する．この数字ごとにジオメトリデータを保存する．

  初期値は`0`

- Mix

  元画像を元の位置に描画する．(アルファブレンド)

  かての標準モーションブラーエフェクトの`残像`のようなもの．

  初期値は`0.0`

- Print Information

  コンソールに情報を表示する．

  表示される情報は以下のとおり

  - Object ID (所謂`Object Index`．GeometryはObject IDごとに保存される．)

  - Index (所謂`obj.index`．個別オブジェクトのインデックス．)

  - Required Samples (必要なサンプル数．これを目安に`Sample Limit`を設定してほしい．)

  初期値は`OFF`

- Center

  中心X，中心Yの値と同じ値を入力する．

- PI

  パラメータインジェクション．

  ```lua
  {
    shutter_angle = 180.0,
    render_sample_limit = 256,
    preview_sample_limit = 0,
    extrapolation = 2,
    resize = true, -- booleanも可
    geo_cache = 0,
    geo_ctrl = 0,
    object_id = 0,
    mix = 0.0,
    print_info = false, -- booleanも可
    cx = 0.0,
    cy = 0.0
  }
  ```

  `{}`は既に挿入済みであるため，PI項目では中身のみ記載する．

##  ビルド方法

`.github\\workflows`内の`releaser.yml`に記載．

## License
LICENSEファイルに記載．

## Change Log 
- **v0.1.0**
  - Release
