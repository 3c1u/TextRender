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

#include "DebugIntf.h"

#include <optional>

#if 0
#define dbg_print TVPAddLog
#else
#define dbg_print(...)
#endif

// use Kirikiri-Z rasterizer for layouting
#include "FontRasterizer.h"
FontRasterizer *GetCurrentRasterizer();

using RgbColor = uint32_t;

#define setprop_t(d, p, ty)                                                    \
  {                                                                            \
    tTJSVariant v(ty(p));                                                      \
    d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d);                   \
  }

#define setprop_opt_t(d, p, ty)                                                \
  if (p != std::nullopt) {                                                     \
    tTJSVariant v(ty(*p));                                                     \
    d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d);                   \
  } else {                                                                     \
    tTJSVariant v;                                                             \
    d->PropSet(TJS_MEMBERENSURE, TJS_W(#p), nullptr, &v, d);                   \
  }

#define setprop(d, p) setprop_t(d, p, )

#define setprop_opt(d, p) setprop_opt_t(d, p, )

#define getprop_t(d, p, ty)                                                    \
  {                                                                            \
    tTJSVariant v;                                                             \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) &&             \
        v.Type() != tvtVoid) {                                                 \
      p = ty(v);                                                               \
    }                                                                          \
  }

#define getprop_opt_t(d, p, ty)                                                \
  {                                                                            \
    tTJSVariant v;                                                             \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) &&             \
        v.Type() != tvtVoid) {                                                 \
      p = ty(v);                                                               \
    } else {                                                                   \
      p = std::nullopt;                                                        \
    }                                                                          \
  }

#define getprop_ensure_opt_deref(d, p, e)                                      \
  {                                                                            \
    tTJSVariant v;                                                             \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) &&             \
        v.Type() != tvtVoid) {                                                 \
      auto ens = v.e;                                                          \
      if (ens)                                                                 \
        p = *ens;                                                              \
      else                                                                     \
        p = std::nullopt;                                                      \
    } else {                                                                   \
      p = std::nullopt;                                                        \
    }                                                                          \
  }

#define getprop(d, p) getprop_t(d, p, )

#define getprop_ensure_deref(d, p, e)                                          \
  {                                                                            \
    tTJSVariant v;                                                             \
    if (TJS_SUCCEEDED(d->PropGet(0, TJS_W(#p), nullptr, &v, d)) &&             \
        v.Type() != tvtVoid) {                                                 \
      auto ens = v.e;                                                          \
      if (ens)                                                                 \
        p = *ens;                                                              \
    }                                                                          \
  }

struct TextRenderState {
  bool       bold        = false;         // 太字
  bool       italic      = false;         // 斜体
  tjs_string face        = TJS_W("user"); // フォントフェイス
  int        fontSize    = 24;            // フォントサイズ
  RgbColor   chColor     = 0xffffff;      // 文字色
  int        rubySize    = 10;            // ルビの大きさ
  int        rubyOffset  = -2;            // ルビのオフセット
  bool       shadow      = true;          // 影
  RgbColor   shadowColor = 0x000000;      // 影の色
  bool       edge        = false;         // 縁取り
  RgbColor   edgeColor   = 0x0080ff;      // 縁の色
  int        lineSpacing = 6;             // 行間
  int        pitch       = 0;             // 字間
  int        lineSize    = 0;             // ラインの高さ

  // -------------------------------------------------------------- //

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();

    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, fontSize);
    setprop(dict, face);
    setprop_t(dict, chColor, static_cast<tjs_int>);
    setprop(dict, rubySize);
    setprop(dict, rubyOffset);
    setprop(dict, shadow);
    setprop_t(dict, shadowColor, static_cast<tjs_int>);
    setprop(dict, edge);
    setprop_t(dict, edgeColor, static_cast<tjs_int>);
    setprop(dict, lineSpacing);
    setprop(dict, pitch);
    setprop(dict, lineSize);

    auto res = tTJSVariant(dict, dict);
    dict->Release();

    return res;
  }

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) {
      return;
    }

    getprop(dict, bold);
    getprop(dict, italic);
    getprop(dict, fontSize);
    getprop_ensure_deref(dict, face, AsStringNoAddRef());
    getprop_t(dict, chColor, static_cast<tjs_int>);
    getprop(dict, rubySize);
    getprop(dict, rubyOffset);
    getprop(dict, shadow);
    getprop_t(dict, shadowColor, static_cast<tjs_int>);
    getprop(dict, edge);
    getprop_t(dict, edgeColor, static_cast<tjs_int>);
    getprop(dict, lineSpacing);
    getprop(dict, pitch);
    getprop(dict, lineSize);
  }

  static TextRenderState from(tTJSVariant t) {
    TextRenderState state{};
    state.deserialize(t);
    return state;
  }
};

struct TextRenderOptions {
  tjs_string following = TJS_W(
      "%),:;]}｡｣ﾞﾟ。，、．：；゛゜ヽヾゝゞ々’”）〕］｝〉》」』】°′″℃￠％‰　!.?"
      "､･ｧｨｩｪｫｬｭｮｯｰ・？！ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ");
  tjs_string leading = TJS_W("\\$([{｢‘“（〔［｛〈《「『【￥＄￡");
  tjs_string begin = TJS_W("「『（‘“〔［｛〈《");
  tjs_string end   = TJS_W("」』）’”〕］｝〉》");

  // -------------------------------------------------------------- //

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();

    setprop(dict, following);
    setprop(dict, leading);
    setprop(dict, begin);
    setprop(dict, end);

    auto res = tTJSVariant(dict, dict);
    dict->Release();

    return res;
  }

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) {
      return;
    }

    getprop_ensure_deref(dict, following, AsStringNoAddRef());
    getprop_ensure_deref(dict, leading, AsStringNoAddRef());
    getprop_ensure_deref(dict, begin, AsStringNoAddRef());
    getprop_ensure_deref(dict, end, AsStringNoAddRef());
  }

  static TextRenderState from(tTJSVariant t) {
    TextRenderState state{};
    state.deserialize(t);
    return state;
  }
};

struct CharacterInfo {
  bool       bold     = false;         // 太字
  bool       italic   = false;         // 斜体
  bool       graph    = false;         // グラフィック文字
  bool       vertical = false;         // 縦書き
  tjs_string face     = TJS_W("user"); // フォントフェイス名？

  int x    = 0; // X座標
  int y    = 0; // Y座標
  int cw   = 0; // 文字幅
  int size = 0; // フォントサイズ？

  RgbColor                color  = 0xffffff;     // 文字色
  std::optional<RgbColor> edge   = std::nullopt; // 縁の色
  std::optional<RgbColor> shadow = std::nullopt; // 影の色

  tjs_string text = TJS_W(""); // 文字

  // -------------------------------------------------------------- //

  tTJSVariant serialize() const {
    auto dict = TJSCreateDictionaryObject();

    setprop(dict, bold);
    setprop(dict, italic);
    setprop(dict, graph);
    setprop(dict, vertical);
    setprop(dict, x);
    setprop(dict, y);
    setprop(dict, cw);
    setprop(dict, size);
    setprop(dict, face);

    setprop_t(dict, color, static_cast<tjs_int>);
    setprop_opt_t(dict, edge, static_cast<tjs_int>);
    setprop_opt_t(dict, shadow, static_cast<tjs_int>);

    setprop(dict, text);

    auto res = tTJSVariant(dict, dict);
    dict->Release();

    return res;
  }

  void deserialize(tTJSVariant t) {
    auto dict = t.AsObjectNoAddRef();
    if (!dict) {
      return;
    }

    getprop(dict, bold);
    getprop(dict, italic);
    getprop(dict, graph);
    getprop(dict, vertical);
    getprop(dict, x);
    getprop(dict, y);
    getprop(dict, cw);
    getprop(dict, size);
    getprop_ensure_deref(dict, face, AsStringNoAddRef());

    getprop_t(dict, color, static_cast<tjs_int>);
    getprop_opt_t(dict, edge, static_cast<tjs_int>);
    getprop_opt_t(dict, shadow, static_cast<tjs_int>);

    getprop_ensure_deref(dict, text, AsStringNoAddRef());
  }

  static TextRenderState from(tTJSVariant t) {
    TextRenderState state{};
    state.deserialize(t);
    return state;
  }
};

#define property_accessor(name, type, storage)                                 \
  type get_##name() const { return storage; }                                  \
  void set_##name(type v) { storage = v; }

#define property_accessor_cast(name, type, cast, storage)                      \
  cast get_##name() const { return cast(storage); }                            \
  void set_##name(cast v) { storage = type(v); }

#define property_accessor_string(name, storage)                                \
  tTJSVariant get_##name() const { return tTJSVariant(storage); }              \
  void        set_##name(tTJSVariant v) {                                      \
    auto s  = v.AsStringNoAddRef();                                     \
    storage = s->LongString ? s->LongString : s->ShortString;           \
  }

#define property_delegate(name) NCB_PROPERTY(name, get_##name, set_##name);

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

  // property accessor
  property_accessor(vertical, bool, m_vertical);

  property_accessor(bold, bool, m_state.bold);
  property_accessor(italic, bool, m_state.italic);
  property_accessor_string(face, m_state.face);
  property_accessor(fontSize, int, m_state.fontSize);
  property_accessor_cast(chColor, RgbColor, tjs_int, m_state.chColor);
  property_accessor(rubySize, int, m_state.rubySize);
  property_accessor(rubyOffset, int, m_state.rubyOffset);
  property_accessor(shadow, bool, m_state.shadow);
  property_accessor_cast(shadowColor, RgbColor, tjs_int, m_state.shadowColor);
  property_accessor(edge, bool, m_state.edge);
  property_accessor(lineSpacing, int, m_state.lineSpacing);
  property_accessor(pitch, int, m_state.pitch);
  property_accessor(lineSize, int, m_state.lineSize);

  property_accessor(defaultBold, bool, m_default.bold);
  property_accessor(defaultItalic, bool, m_default.italic);
  property_accessor_string(defaultFace, m_default.face);
  property_accessor(defaultFontSize, int, m_default.fontSize);
  property_accessor_cast(defaultChColor, RgbColor, tjs_int, m_default.chColor);
  property_accessor(defaultRubySize, int, m_default.rubySize);
  property_accessor(defaultRubyOffset, int, m_default.rubyOffset);
  property_accessor(defaultShadow, bool, m_default.shadow);
  property_accessor_cast(defaultShadowColor, RgbColor, tjs_int,
                         m_default.shadowColor);
  property_accessor(defaultEdge, bool, m_default.edge);
  property_accessor(defaultLineSpacing, int, m_default.lineSpacing);
  property_accessor(defaultPitch, int, m_default.pitch);
  property_accessor(defaultLineSize, int, m_default.lineSize);

private:
  int m_boxWidth  = 0;
  int m_boxHeight = 0;

  int m_x = 0;
  int m_y = 0;

  int  m_indent            = 0;
  int  m_autoIndent        = 0;
  bool m_overflow          = false;
  bool m_isBeginningOfLine = true;

  bool m_vertical = false;

  TextRenderOptions m_options{};
  TextRenderState   m_default{};
  TextRenderState   m_state{};

  std::vector<CharacterInfo> m_characters{};
  std::vector<CharacterInfo> m_buffer{};
  uint32_t                   m_mode = 0;

  void pushCharacter(tjs_char ch);
  void pushGraphicalCharacter(tjs_string const& graph);
  void performLinebreak();
  void flush(bool force = false);
  void updateFont();
};

enum TextRenderAlignment {
  kTextRenderAlignmentLeft   = -1,
  kTextRenderAlignmentCenter = 0,
  kTextRenderAlignmentRight  = 1,
};

// [LEADING] [NORMAL] [FOLLOWING] の形になるように文字をセグメンテーションする．

enum TextRenderMode {
  kTextRenderModeLeading = 0,
  kTextRenderModeNormal,
  kTextRenderModeFollowing,
};

// -------------------------------------------------------------------

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

static void read_integer(tTJSString const &str, size_t &i, int &value) {
  tjs_char ch;
  bool     is_negative = false;

  while (true) {
    if (!readchar(str, i, ch)) {
      TVPThrowExceptionMessage(
          TJS_W("TextRenderBase::render() failed to "
                "parse: expected either integer or ';', found EOF"));
    }

    if ('0' <= ch && ch <= '9') {
      value = value * 10 + (ch - '0');
      continue;
    } else if (ch == '-') {
      is_negative = !is_negative;
      continue;
    } else if (ch == ';') {
      if (is_negative) {
        value = -value;
      }
      return;
    }

    TVPThrowExceptionMessage(
        TJS_W("TextRenderBase::render() failed to "
              "parse: expected either integer or ';', found '%1'"),
        ch);
  }
}

bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all,
                            bool same) {
  // 入力のパース

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
      case 'f': // フォントフェイス
      {
        tjs_string faceName{};

        while (true) {
          if (!readchar(text, i, ch)) {
            TVPThrowExceptionMessage(
                TJS_W("TextRenderBase::render() failed to "
                      "parse: expected character, found EOF"));
          }

          if (ch == ';')
            break;

          faceName += ch;
        }

        dbg_print(
            TVPFormatMessage(TJS_W("change font face name: %1"), faceName));

        m_state.face = faceName;

        break;
      }
      case 'b': // フォントの装飾
      {
        if (!readchar(text, i, ch) && (ch == '0' || ch == '1')) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse %%b: expected either '0' or '1', found EOF"));
        }
        bool flag = ch == '1';
        if (flag)
          dbg_print(TJS_W("set bold"));
        else
          dbg_print(TJS_W("unset bold"));

        m_state.bold = flag;

        break;
      }
      case 'i': {
        if (!readchar(text, i, ch) && (ch == '0' || ch == '1')) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse %%i: expected either '0' or '1', found EOF"));
        }
        bool flag = ch == '1';
        if (flag)
          dbg_print(TJS_W("set italic (oblique)"));
        else
          dbg_print(TJS_W("unset italic (oblique)"));

        m_state.italic = flag;

        break;
      }
      case 's': {
        if (!readchar(text, i, ch) && (ch == '0' || ch == '1')) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse %%s: expected either '0' or '1', found EOF"));
        }
        bool flag = ch == '1';
        if (flag)
          dbg_print(TJS_W("set shadow"));
        else
          dbg_print(TJS_W("unset shadow"));

        m_state.shadow = flag;

        break;
      }
      case 'e': {
        if (!readchar(text, i, ch) && (ch == '0' || ch == '1')) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse %%e: expected either '0' or '1', found '%1'"),
              ch);
        }
        bool flag = ch == '1';
        if (flag)
          dbg_print(TJS_W("set edge"));
        else
          dbg_print(TJS_W("unset edge"));

        m_state.edge = flag;

        break;
      }
      case 'B': // TODO: Big
        dbg_print(TJS_W("big font"));
        break;
      case 'S': // TODO: Small
        dbg_print(TJS_W("small font"));
        break;
      case 'r': // Reset
        dbg_print(TJS_W("reset"));
        m_state = m_default;
        break;
      case 'C': // TODO: Centre
        dbg_print(TJS_W("centre"));
        break;
      case 'R': // TODO: Right
        dbg_print(TJS_W("right"));
        break;
      case 'L': // TODO: Left
        dbg_print(TJS_W("left"));
        break;
      case 'p': // ピッチ，%p[0-9]+;
      {
        int value = 0;
        read_integer(text, i, value);
        m_state.pitch = value;
        break;
      }
      case 'd': // 文字あたり表示時間指定，%d[0-9]+;
      {
        int value = 0;
        read_integer(text, i, value);
        // TODO: text speed
        break;
      }
      case 'w': // 時間待ち，%w[0-9]+;
      {
        int value = 0;
        read_integer(text, i, value);
        // TODO: wait
        break;
      }
      case 'D': // %D[0-9]+; || %D$.+;
      {
        if (ch == '$') {
          tjs_string labelName{};

          while (true) {
            if (!readchar(text, i, ch)) {
              TVPThrowExceptionMessage(
                  TJS_W("TextRenderBase::render() failed to "
                        "parse: expected character, found EOF"));
            }

            if (ch == ';')
              break;

            labelName += ch;
          }

          // TODO: labelName

        } else {
          int value = 0;
          read_integer(text, i, value);

          // TODO: label?
        }
        //
        break;
      }
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': {
        int value = static_cast<int>(ch - '0');
        read_integer(text, i, value);

        // font size
        m_state.fontSize = m_default.fontSize * value / 100;

        dbg_print(
            TVPFormatMessage(TJS_W("new font size: %1 px"), m_state.fontSize));

        updateFont();

        break;
      }
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
        flush();
        performLinebreak();
        break;
      case 't':
        // タブ
        pushCharacter('\t');
        break;
      case 'i':
        m_indent = m_x;
        break;
      case 'r':
        m_indent = 0;
        break;
      case 'w':
        pushCharacter(' ');
        break;
      case 'k':
        // TODO: キー待ち
        //
        break;
      case 'x':
        // TODO: nul文字
        // unknown behaviour: possible UB
        break;
      default:
        goto __draw_normal; // chをふつうの文字列として描画する
        break;
      }
      break;
    case '[': {
      // [ .* ]
      // [ .*, [0-9]+ ]
      tjs_string ruby{};

      while (true) {
        if (!readchar(text, i, ch)) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse: expected character, found EOF"));
        }

        if (ch == ']')
          break;

        ruby += ch;
      }

      // TODO: implement ruby

      break;
    }
    case '#': {
      // [ .* ]
      // [ .*, [0-9]+ ]
      RgbColor colour = 0x00;

      while (true) {
        if (!readchar(text, i, ch)) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse: expected character, found EOF"));
        }

        if (ch == ';')
          break;

        RgbColor c = 0;
        if ('0' <= ch && ch <= '9') {
          c = static_cast<RgbColor>(ch - '0');
        } else if ('A' <= ch && ch <= 'F') {
          c = 0x0a + static_cast<RgbColor>(ch - 'A');
        } else if ('a' <= ch && ch <= 'f') {
          c = 0x0a + static_cast<RgbColor>(ch - 'a');
        } else {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse: expected hexadecimal number, found '%1'"),
              ch);
        }

        colour = (colour << 4) || c;
      }

      m_state.chColor = colour;

      break;
    }
    case '&': {
      // '&' .+ ';'
      tjs_string graph{};

      while (true) {
        if (!readchar(text, i, ch)) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse: expected character, found EOF"));
        }

        if (ch == ';')
          break;

        graph += ch;
      }

      pushGraphicalCharacter(graph);
      break;
    }
    case '$': {
      // '&' .+ ';'
      tjs_string varName{};

      while (true) {
        if (!readchar(text, i, ch)) {
          TVPThrowExceptionMessage(
              TJS_W("TextRenderBase::render() failed to "
                    "parse: expected character, found EOF"));
        }

        if (ch == ';')
          break;

        varName += ch;
      }

      // TODO: implement eval

      break;
    }
    default:
    __draw_normal:
      // タダの文字として処理する
      // TODO: character should include format options;
      //       as the font is lazy-evaluated/drawn
      //       (restrictions for line-breaking algorithm)
      pushCharacter(ch);
      break;
    }
  }

  return !m_overflow;
}

void TextRenderBase::performLinebreak() {
  auto rasterizer     = GetCurrentRasterizer();
  m_x                 = m_indent;
  m_isBeginningOfLine = true;
  m_y += rasterizer->GetAscentHeight() + m_state.lineSpacing;
}

void pushGraphicalCharacter(tjs_string const& graph) {
  // TODO: implement graphical characters
}

void TextRenderBase::pushCharacter(tjs_char ch) {
  if ((0xD800 <= ch && ch <= 0xDBFF) /* upper surrogate-pair */
      || (0xDC00 <= ch && ch <= 0xDFFF) /* lower surrogate-pair */) {
    TVPThrowExceptionMessage(TJS_W("unexpected character: surrogate pair"));
  }

  auto isLeadingChar = m_options.leading.find_first_of(ch) != std::string::npos;
  auto isFollowingChar =
      m_options.following.find_first_of(ch) != std::string::npos;
  auto isIndent     = m_options.begin.find_first_of(ch) != std::string::npos;
  auto isIndentDecr = m_options.end.find_first_of(ch) != std::string::npos;

  uint32_t current;

  if (isLeadingChar) {
    current = kTextRenderModeLeading;
  } else if (isFollowingChar) {
    current = kTextRenderModeFollowing;
  } else {
    current = kTextRenderModeNormal;
  }

  if (m_mode == kTextRenderModeFollowing || m_mode != kTextRenderModeLeading) {
    flush();
  }

  auto rasterizer  = GetCurrentRasterizer();
  auto text_height = rasterizer->GetAscentHeight();

  int advance_width = 0, advance_height = 0;

  rasterizer->GetTextExtent(ch, advance_width, advance_height);

  CharacterInfo info{
      .bold     = m_state.bold,
      .italic   = m_state.italic,
      .graph    = false,
      .vertical = false,
      .face     = m_state.face,
      .x        = 0,
      .y        = 0,
      .cw       = advance_width,
      .size     = text_height,
      .color    = m_state.chColor,
      .edge =
          m_state.edge ? std::make_optional(m_state.edgeColor) : (std::nullopt),
      .shadow = m_state.shadow ? std::make_optional(m_state.shadowColor)
                               : (std::nullopt),
      .text = (tjs_string() + ch),
  };

  m_buffer.push_back(std::move(info));

  if (m_autoIndent) {
    // pre-indent
    if (m_isBeginningOfLine && m_autoIndent < 0) {
      m_x -= advance_width;
    }

    if (isIndent) {
      m_indent = m_x + advance_width;
      // TODO: register pair
    }

    if (isIndentDecr && m_indent > 0) {
      flush(); // FIXME: not safe?
      m_indent = 0;
    }
  }

  m_mode              = current;
  m_isBeginningOfLine = false;
}

void TextRenderBase::flush(bool force) {
  if (m_buffer.empty()) {
    return;
  }

  // try place all characters in the same line

  auto x = m_x;

  for (auto &ch : m_buffer) {
    auto advance_width = ch.cw;
    auto new_x         = advance_width + x + m_state.pitch;

    if (m_boxWidth < new_x) {
      if (force) {
        performLinebreak();
        x     = m_x;
        new_x = advance_width + x + m_state.pitch;
      } else {
        performLinebreak();
        flush(true);
        return;
      }
    }

    ch.x = x;
    ch.y = m_y;

    x = new_x;
  }

  m_x = x;
  m_characters.insert(m_characters.end(), m_buffer.begin(), m_buffer.end());
  m_buffer.clear();
}

void TextRenderBase::setRenderSize(int width, int height) {
  m_boxWidth  = width;
  m_boxHeight = height;

  dbg_print(
      TVPFormatMessage(TJS_W("set render size: (%1, %2)"), width, height));

  clear();
}

void TextRenderBase::setDefault(tTJSVariant defaultSettings) {
  dbg_print(TJS_W("set default format"));
  m_default.deserialize(defaultSettings);
}

void TextRenderBase::setOption(tTJSVariant options) {
  dbg_print(TJS_W("set option"));
  m_options.deserialize(options);
}

tTJSVariant TextRenderBase::getCharacters(int start, int end) {
  // TODO: only (0, 0) is observed
  auto array = TJSCreateArrayObject();
  dbg_print(TVPFormatMessage(TJS_W("get characters: [%1, %2]"), start, end));

  if ((end < start) || (start == 0 && end == 0)) {
    for (size_t i = 0, cnt = m_characters.size(); i < cnt; ++i) {
      auto ch = m_characters[i].serialize();
      array->PropSetByNum(TJS_MEMBERENSURE, i, &ch, array);
    }
  } else {
    // TODO: unknown behaviour
  }

  return tTJSVariant(array, array);
}

void TextRenderBase::clear() {
  dbg_print(TJS_W("clear character buffer and format"));

  m_characters.clear();

  m_state    = m_default;
  m_overflow = false;

  // カーソル位置を戻す
  m_x      = 0;
  m_y      = 0;
  m_indent = 0;

  m_isBeginningOfLine = true;

  // ラスタライザを指定された書式で初期化
  updateFont();
}

void TextRenderBase::updateFont() {
  auto rasterizer = GetCurrentRasterizer();
  auto font       = tTVPFont{
      .Height = m_state.fontSize, // height of text
      .Flags  = static_cast<tjs_uint32>((m_state.bold ? TVP_TF_BOLD : 0) |
                                       (m_state.italic ? TVP_TF_ITALIC : 0)),
      .Angle  = 0,
      .Face   = m_state.face, // TODO: this may fuck up the font settings by
                              // forcing the fallback font (in most cases)
  };

  rasterizer->ApplyFont(font);
}

void TextRenderBase::done() {
  dbg_print(TJS_W("flush character buffer"));
  flush();
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

  property_delegate(vertical);
  property_delegate(bold);
  property_delegate(italic);
  property_delegate(face);
  property_delegate(fontSize);
  property_delegate(chColor);
  property_delegate(rubySize);
  property_delegate(rubyOffset);
  property_delegate(shadow);
  property_delegate(shadowColor);
  property_delegate(edge);
  property_delegate(lineSpacing);
  property_delegate(pitch);
  property_delegate(lineSize);

  property_delegate(defaultBold);
  property_delegate(defaultItalic);
  property_delegate(defaultFace);
  property_delegate(defaultFontSize);
  property_delegate(defaultChColor);
  property_delegate(defaultRubySize);
  property_delegate(defaultRubyOffset);
  property_delegate(defaultShadow);
  property_delegate(defaultShadowColor);
  property_delegate(defaultEdge);
  property_delegate(defaultLineSpacing);
  property_delegate(defaultPitch);
  property_delegate(defaultLineSize);
};
