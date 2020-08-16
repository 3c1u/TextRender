/**
 * @file      textrender.cc
 * @author    Hikaru Terazono (3c1u) <3c1u@vulpesgames.tokyo>
 * @brief     Drop-in replacement for TextRender.dll.
 * @date      2020-08-16
 *
 * @copyright Copyright (c) 2020 Hikaru Terazono. All rights reserved.
 *
 */

#include "ncbind/ncbind.hpp"

#include <optional>

// use Kirikiri-Z rasterizer for layouting
#include "FontRasterizer.h"
FontRasterizer *GetCurrentRasterizer();

using RgbColor = uint32_t;

struct TextRenderDefault {
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
  //
  int m_boxWidth  = 0;
  int m_boxHeight = 0;
};

TextRenderBase::TextRenderBase() {}

TextRenderBase::~TextRenderBase() {}

bool TextRenderBase::render(tTJSString text, int autoIndent, int diff, int all,
                            bool _reserved) {
  // TODO:
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
