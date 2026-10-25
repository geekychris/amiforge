// Editor colour themes: plain data (0xRRGGBB), no OS or Scintilla calls, so the host tests can
// check them. sciSetupSourceEditor() styles a view with currentTheme().
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace af {

struct Theme {
    const char *name;           // "Light", "Dark"
    bool dark;
    // text and margins
    uint32_t textFore, textBack;
    uint32_t marginFore, marginBack;
    uint32_t foldBack, foldMarkFore, foldMarkBack;
    uint32_t indentGuide, controlChar;
    // syntax
    uint32_t comment, docComment, keyword, type, string, number, preprocessor, oper;
    uint32_t label, asmOper, taskMarker;
    uint32_t stringEolBack;     // background of an unterminated string
    // brace matching
    uint32_t braceFore, braceBack, braceBadFore, braceBadBack;
    // selection, caret
    uint32_t selection, selectionInactive, caret;
    uint32_t caretLine;         // 0xAARRGGBB: translucent over markers
    uint32_t additionalSelection, additionalCaret;   // multiple cursors
    // calltips
    uint32_t calltipFore, calltipBack, calltipHighlight;
    // autocompletion list
    uint32_t listFore, listBack, listSelFore, listSelBack;
    // markers (fore = outline, back = fill)
    uint32_t bpFore, bpBack, bpOffFore;
    uint32_t execFore, execBack, execLineBack;
    uint32_t errorBack, warningBack;
    uint32_t frameFore, frameBack;
    // other lexers
    uint32_t propsSectionBack, diffHeaderBack;
    uint32_t diffDeletedFore, diffDeletedBack, diffAddedFore, diffAddedBack, diffChangedFore, diffChangedBack;
    // editor features
    uint32_t minimapView;       // the main view's visible range in the minimap
    uint32_t snippetField;      // snippet placeholders
    uint32_t occurrence;        // Find All References highlight
    // AutoDocs viewer
    uint32_t docHeading, docLink, docTitle;
};

const std::vector<Theme> &themes();                 // the built-in themes, Light first
const Theme &themeNamed(const std::string &name);   // case-insensitive; Light if unknown
const Theme &currentTheme();
void setCurrentTheme(const std::string &name);      // unknown names select Light

// Scintilla wants 0xBBGGRR.
constexpr intptr_t sciColour(uint32_t rgb)
{
    return static_cast<intptr_t>(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}
// WCAG contrast ratio of two colours (1..21).
double contrastRatio(uint32_t a, uint32_t b);

} // namespace af
