/**
 * @file      textrender.cc
 * @author    Hikaru Terazono (3c1u) <3c1u@vulpesgames.tokyo>
 * @brief     Drop-in replacement for TextRender.dll.
 * @date      2020-08-16
 *
 * @copyright Copyright (c) 2020 Hikaru Terazono. All rights reserved.
 *
 */

/* from TextRender.tjs:
 * ・フェースは１回の描画中での変更は無しとしてあります。変更すると結果が保証されません
 * ・ルビは指定領域から上にはみ出た位置に配置されます。最大サイズの本文テキスト上端が0位置です
 *
 * 入力用特殊テキスト書式
 * \n      改行
 * \t      タブ文字
 * \i      インデント開始(次行から)
 * \r      インデント解除(次行から)
 * \w      空白相当分表示位置を進める
 * \k      キー入力待ち情報を生成
 * \x      nul文字相当
 * \文字   エスケープ指定。特殊機能が無効
 * [xxxx]  ルビ指定。次の文字にかかる
 * [xxxx,文字数]  ルビ指定。次の指定した個数の複数の文字にかかる
 *
 * フォント指定
 * %f名前; フォントフェイス指定
 * %bX     ボールド指定    0:off 1:on その他:デフォルト
 * %iX     イタリック指定  0:off 1:on その他:デフォルト
 * %sX     影指定          0:off 1:on その他:デフォルト
 * %eX     エッジ指定      0:off 1:on その他:デフォルト
 * %数値;  フォントサイズ指定(デフォルトに対するパーセント)
 * %B      大サイズフォント
 * %S      小サイズフォント
 * #xxxxxx; 色指定(xxxは色を16進指定)
 * %r      フォントリセット
 *
 * スタイル指定
 * %C      センタリング (align=0)
 * %R      右よせ (align=1)
 * %L      左寄せ (align=-1)
 * %p数値; ピッチ
 *
 * 特殊指定
 * %d数値;  文字あたり表示時間指定(標準に対するパーセント指定 100で標準速度)
 * %w数値;  時間待ち(1文字表示の時間に対するパーセント指定100で1文字分)
 * %D数値;  時間同期指定 指定した時間で表示同期指定 単位:ms
 * %D$xxx;  時間同期指定 同期指定・ラベル指定(xxxはラベル名)
 *
 * $xxx;   埋め込み指定(xxxは変数名) ※onEval で処理されます
 * &xxx;   グラフィック文字指定 (xxxは画像名)
 */

#include "ncbind/ncbind.hpp"

#include <optional>

// use Kirikiri-Z rasterizer for layouting
#include "FontRasterizer.h"
FontRasterizer *GetCurrentRasterizer();

using RgbColor = uint32_t;

struct TextRenderState {
  bool     bold        = false;    // 太字
  int      fontSize    = 24;       // フォントサイズ
  RgbColor chColor     = 0xffffff; // 文字色
  int      rubySize    = 10;       // ルビの大きさ
  int      rubyOffset  = -2;       // ルビのオフセット
  bool     shadow      = true;     // 影
  RgbColor shadowColor = 0x000000; // 影の色
  bool     edge        = false;    // 縁取り
  RgbColor edgeColor   = 0x0080ff; // 縁の色
  int      lineSpacing = 6;        // 行間
  int      pitch       = 0;        // 行間
  int      lineSize    = 0;        // ラインの高さ
};

struct TextRenderOptions {
  tjs_string following = TJS_W(
      "%),:;]}｡｣ﾞﾟ。，、．：；゛゜ヽヾゝゞ々’”）〕］｝〉》」』】°′″℃￠％‰　!.?"
      "､･ｧｨｩｪｫｬｭｮｯｰ・？！ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");
  tjs_string leading = TJS_W("\\$([{｢‘“（〔［｛〈《「『【￥＄￡");
  tjs_string begin = TJS_W("「『（‘“〔［｛〈《");
  tjs_string end   = TJS_W("」』）’”〕］｝〉》");
};

// [LEADING] [NORMAL] [FOLLOWING] の形になるように文字をセグメンテーションする．

struct CharacterInfo {
  bool                      bold     = false; // 太字
  bool                      italic   = false; // 斜体
  bool                      graph    = false; // グラフィック文字
  bool                      vertical = false; // 縦書き
  std::optional<tjs_string> face = TJS_W(""); // フォントフェイス名？

  int x    = 0; // X座標
  int y    = 0; // Y座標
  int cw   = 0; // 文字幅
  int size = 0; // フォントサイズ？

  RgbColor                color  = 0xffffff;     // 文字色
  std::optional<RgbColor> edge   = std::nullopt; // 縁の色
  std::optional<RgbColor> shadow = std::nullopt; // 影の色
};

/**
 * @brief The base of the TextRender class. This only performs the text
 * layouting and the line breaking (禁則処理)．
 */
class TextRenderBase {
public:
  TextRenderBase();
  virtual ~TextRenderBase();
  bool        render(tTJSString text, int autoIndent, int diff, int all,
                     bool _reserved);
  void        setRenderSize(int width, int height);
  void        setDefault(tTJSVariant defaultSettings);
  void        setOption(tTJSVariant options);
  tTJSVariant getCharacters(int start, int end);
  void        clear();
  void        done();

private:
  int m_boxWidth  = 0;
  int m_boxHeight = 0;

  int m_x = 0;
  int m_y = 0;

  TextRenderOptions m_options;
  TextRenderState   m_default;
  TextRenderState   m_state;

  std::vector<CharacterInfo> m_characters;
};

constexpr size_t kTextRenderMaxSegmentLength = 2;

enum TextRenderAlignment {
  kTextRenderAlignmentLeft   = -1,
  kTextRenderAlignmentCenter = 0,
  kTextRenderAlignmentRight  = 1,
};

TextRenderBase::TextRenderBase() {}

TextRenderBase::~TextRenderBase() {}

static bool readchar(tTJSString const &str, size_t &i, tjs_char &c) {
  auto const len = str.GetLen();

  if (++i >= len) {
    return false;
  }

  c = str[i];
  return true;
}

bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all,
                            bool same) {
  // ラスタライザの取得
  auto rasterizer = GetCurrentRasterizer();

  auto const len = text.GetLen();

  for (size_t i = 0; i < len; ++i) {
    auto ch = text[i];

    switch (ch) {
    // 制御文字のパース
    case '%':
      if (!readchar(text, i, ch)) {
        TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() failed to "
                                       "parse: expected character, found EOF"));
      }

      switch (ch) {
      default:
        TVPThrowExceptionMessage(
            TJS_W("TextRenderBase::render() failed to "
                  "parse: expected any of 'fbiseBSrCRLpdwD0123456789', found "
                  "'%1'"),
            ch);
        break;
      }
      break;
    case '\\':
      if (!readchar(text, i, ch)) {
        TVPThrowExceptionMessage(TJS_W("TextRenderBase::render() failed to "
                                       "parse: expected character, found EOF"));
      }

      switch (ch) {
      case 'n':
        //　改行
        break;
      case 't':
        // タブ
        break;
      case 'i':
        // インデント開始
        break;
      case 'r':
        // インデント解除
        break;
      case 'w':
        // 空白相当分表示位置を進める
        break;
      case 'k':
        // キー待ち
        break;
      case 'x':
        break;
      default:
        goto __draw_normal; // chをふつうの文字列として描画する
        break;
      }
      break;
    case '[':
      // [ .* ]
      // [ .*, [0-9]+ ]
      break;
    case '#':
      // '#' [0-9a-fA-F]+ ';'
      break;
    case '&':
      // '&' .+ ';'
      break;
    case '$':
      // '$' .+ ';'

      // onEvalで評価？
      break;
    default:
    __draw_normal:
      break;
    }
  }

  return true;
}

void TextRenderBase::setRenderSize(int width, int height) {
  m_boxWidth  = width;
  m_boxHeight = height;

  clear();
}

void TextRenderBase::setDefault(tTJSVariant defaultSettings) {
  // TODO:
}

void TextRenderBase::setOption(tTJSVariant options) {
  // TODO:
}

tTJSVariant TextRenderBase::getCharacters(int start, int end) {
  // TODO:
  auto array = TJSCreateArrayObject();
  return tTJSVariant(array, array);
}

void TextRenderBase::clear() {
  // TODO:
}

void TextRenderBase::done() {
  // TODO:
}

// register the class
NCB_REGISTER_CLASS(TextRenderBase) {
  Constructor();

  NCB_METHOD(render);
  NCB_METHOD(setRenderSize);
  NCB_METHOD(setDefault);
  NCB_METHOD(setOption);
  NCB_METHOD(getCharacters);
  NCB_METHOD(clear);
  NCB_METHOD(done);
};
