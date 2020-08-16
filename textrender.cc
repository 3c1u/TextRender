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

/**
 * @brief The base of the TextRender class. This only performs the text
 * layouting and the line breaking (禁則処理)．
 */
class TextRenderBase {
public:
  TextRenderBase() {}

  virtual ~TextRenderBase() {}

  bool render(tTJSString text, int autoIndent, int diff, int all,
              bool _reserved) {
    // TODO:
    return true;
  }

  void setRenderSize(int width, int height) {
    // TODO:
  }

  void setDefault(tTJSVariant defaultSettings) {
    // TODO:
  }

  void setOption(tTJSVariant options) {
    // TODO:
  }

  tTJSVariant getCharacters(int start, int end) {
    // TODO:
    auto array = TJSCreateArrayObject();
    return tTJSVariant(array, array);
  }

  void clear() {
    // TODO:
  }

  void done() {
    // TODO:
  }
};

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
