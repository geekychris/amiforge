// --- markdown --- Markdown for the preview: md4c (third_party/md4c, MIT) parses the text into
// a block tree, and the layout turns the tree into positioned items (text, boxes, rules,
// images) for a given width. No OS calls: the host tests check both; MarkdownView.cpp draws
// the items with graphics.library.
//
// Text is Latin-1 (the GUI's character set): UTF-8 from the file is converted, with the usual
// punctuation (dashes, quotes, arrows...) mapped to its nearest Latin-1/ASCII form.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace af {
namespace md {

// UTF-8 to Latin-1: characters below U+0100 as they are, common punctuation as ASCII
// ("—" -> "-", "“" -> "\"", "→" -> "->", "…" -> "..."), anything else '?'. Bytes that aren't
// valid UTF-8 are taken as Latin-1 already.
std::string toLatin1(const std::string &utf8);

// --- the block tree -----------------------------------------------------------------------

enum class BlockType { Doc, Quote, UL, OL, LI, HR, Heading, Code, Html, Para, Table, THead, TBody, TR, TH, TD };
const char *blockName(BlockType t);   // "doc", "quote", "ul"...

enum : uint8_t { Bold = 1, Italic = 2, CodeSpan = 4, Strike = 8, Underline = 16 };

// A piece of inline text in one style. `link` is an index into Doc::links (-1: none),
// `image` an index into Doc::images (the run's text is then the alt text). "\n" alone is a
// hard line break.
struct Run {
    std::string text;
    uint8_t style = 0;
    int link = -1;
    int image = -1;
};

struct Block {
    BlockType type = BlockType::Doc;
    int level = 0;            // Heading: 1-6
    unsigned start = 1;       // OL: the first number
    bool tight = true;        // UL/OL
    char align = 0;           // TH/TD: 'l', 'c', 'r' or 0
    bool task = false, checked = false;   // LI of a task list ("- [x] done")
    std::string info;         // Code: the fence's info string ("c", "basic")
    std::vector<Run> runs;    // Para, Heading, TH/TD, Code (its text, lines with "\n")
    std::vector<std::unique_ptr<Block>> children;
    Block *parent = nullptr;
};

struct Image {
    std::string src, alt;
    int width = 0;     // an HTML <img width=...>: drawn at most that wide (0: as wide as fits)
};

struct Doc {
    Block root;
    std::vector<std::string> links;   // hrefs as written
    std::vector<Image> images;
    bool truncated = false;           // too big or too deep: the rest was left out
};

// HTML in Markdown (README files use it for pictures): an HTML block's <img>, <a href>, <br>,
// <b>/<i>/<code>, <p>/<div>/<h1>... and <table>/<tr>/<td> become runs and table blocks under
// the html block; other tags are dropped, comments too. Inline HTML: <img>, <a>, <br>.

// Limits that keep odd input bounded (the preview must never take the IDE down).
const size_t kMaxInput = 4 * 1024 * 1024;   // bytes of Markdown parsed; the rest is cut
const int kMaxDepth = 32;                    // nested blocks (quotes in lists in quotes...)
const size_t kMaxBlocks = 50000;

bool parse(const std::string &utf8, Doc &out);
// The tree as text, one block per line, indented two spaces a level:
//   h1 "Title"   p "Some *text*" ...   with [link N] / [img N] markers; for tests and MCP.
std::string dump(const Doc &doc);
// The text of a block's runs (Latin-1), styles left out.
std::string plainText(const Block &b);

// --- the layout ---------------------------------------------------------------------------

// The fonts the layout asks for.
enum Font { FBody, FBold, FItalic, FBoldItalic, FMono, FH1, FH2, FH3, FH4, FH5, FH6, FCount };

struct Metrics {
    virtual ~Metrics() {}
    virtual int textWidth(int font, const std::string &text) = 0;
    virtual int lineHeight(int font) = 0;    // ascent + descent + leading
    virtual int baseline(int font) = 0;      // from the top of the line
    // the picture's natural size; false if it can't be loaded (the alt text is shown instead)
    virtual bool imageSize(const std::string &src, int &w, int &h) = 0;
};

enum class ItemKind {
    Text,       // text in `font`, top-left at x, y (h = the line height)
    CodeBg,     // the shaded background of a code span or block
    QuoteBar,   // the bar left of a block quote
    Rule,       // a horizontal rule, or a table's line (w or h may be 1)
    Picture,    // a picture scaled to w x h
    AltBox,     // a frame with the alt text of a picture that couldn't be loaded
    TableHead,  // the shaded background of a table's header row
};

enum Color : uint8_t { CText, CLink, CMuted, CHeading };

struct Item {
    ItemKind kind = ItemKind::Text;
    int x = 0, y = 0, w = 0, h = 0;
    int font = FBody;
    uint8_t style = 0;     // Strike/Underline for text
    Color color = CText;
    std::string text;
    int link = -1;         // Doc::links
    int image = -1;        // Doc::images
};

struct Layout {
    std::vector<Item> items;
    int width = 0;    // the widest item's right edge (code blocks and tables can be wider than asked)
    int height = 0;
};

// Lay `doc` out for a view `width` pixels wide (at least 40).
Layout layout(const Doc &doc, int width, Metrics &m);

// The link at (x, y) in layout coordinates, or -1.
int linkAt(const Layout &l, int x, int y);

// What a link is: a Markdown file next to the document (opened in the editor, with an
// optional #anchor), a web address (OpenURL), an anchor in this document, or something else.
enum class LinkKind { File, Web, Anchor, Other };
LinkKind linkKind(const std::string &href);
// A relative href resolved against the document's drawer (AmigaDOS paths: "Work:x/README.md"
// + "docs/a.md" -> "Work:x/docs/a.md", "../b.md" walks up). The #anchor is dropped.
std::string resolvePath(const std::string &docPath, const std::string &href);

} // namespace md
} // namespace af
