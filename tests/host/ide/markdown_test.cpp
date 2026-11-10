// The Markdown preview: md4c's block tree, Latin-1 text, the layout, links.
#include "aftest.h"
#include "Markdown.h"

#include <map>

using namespace af::md;

static bool has(const std::string &s, const std::string &part) { return s.find(part) != std::string::npos; }

// fixed-width fonts: 8 px a character (bold 9, headings wider), lines 16 px (headings taller)
struct FakeMetrics : Metrics {
    std::map<std::string, std::pair<int, int>> pics;
    int cw(int f) { return f == FBold || f == FBoldItalic ? 9 : f >= FH1 ? 16 - (f - FH1) * 2 : 8; }
    int textWidth(int f, const std::string &t) override { return cw(f) * (int)t.size(); }
    int lineHeight(int f) override { return f >= FH1 ? 32 - (f - FH1) * 3 : 16; }
    int baseline(int f) override { return lineHeight(f) * 3 / 4; }
    bool imageSize(const std::string &src, int &w, int &h) override
    {
        auto it = pics.find(src);
        if (it == pics.end()) return false;
        w = it->second.first;
        h = it->second.second;
        return true;
    }
};

static std::string tree(const std::string &md)
{
    Doc d;
    CHECK(parse(md, d));
    return dump(d);
}

TEST(md_commonmark_blocks)
{
    // headings: ATX and Setext, with closing #s
    CHECK_EQ(tree("# One\n## Two ##\n###### Six\n"), std::string("doc\n  h1 \"One\"\n  h2 \"Two\"\n  h6 \"Six\"\n"));
    CHECK_EQ(tree("Big\n===\n\nSmaller\n-------\n"), std::string("doc\n  h1 \"Big\"\n  h2 \"Smaller\"\n"));
    CHECK_EQ(tree("####### seven\n"), std::string("doc\n  p \"####### seven\"\n"));
    // paragraphs: soft breaks become spaces, two spaces or \ a hard break
    CHECK_EQ(tree("a\nb\n\nc  \nd\\\ne\n"), std::string("doc\n  p \"a b\"\n  p \"c\\nd\\ne\"\n"));
    // emphasis, strong, code spans, strike
    CHECK_EQ(tree("*a* _b_ **c** __d__ ***e*** `f g` ~~h~~\n"),
             std::string("doc\n  p \"*a* *b* **c** **d** ***e*** `f g` ~~h~~\"\n"));
    CHECK_EQ(tree("not*emph * here\n"), std::string("doc\n  p \"not*emph * here\"\n"));
    // code blocks: fenced (with info) and indented; no inline parsing inside
    CHECK_EQ(tree("```basic\nPrint *x*\n```\n"), std::string("doc\n  code basic \"Print *x*\\n\"\n"));
    CHECK_EQ(tree("~~~\na\n\nb\n~~~\n"), std::string("doc\n  code \"a\\n\\nb\\n\"\n"));
    CHECK_EQ(tree("    x = 1\n    y = 2\n"), std::string("doc\n  code \"x = 1\\ny = 2\\n\"\n"));
    // block quotes, nested, with lazy continuation
    CHECK_EQ(tree("> a\nlazy\n> > b\n"), std::string("doc\n  quote\n    p \"a lazy\"\n    quote\n      p \"b\"\n"));
    // lists: bullets, numbers with a start, nesting, loose, tasks
    CHECK_EQ(tree("- a\n- b\n  - c\n    1. d\n"),
             std::string("doc\n  ul\n    li \"a\"\n    li \"b\"\n      ul\n        li \"c\"\n          ol\n            li \"d\"\n"));
    CHECK_EQ(tree("3. x\n4. y\n"), std::string("doc\n  ol start=3\n    li \"x\"\n    li \"y\"\n"));
    CHECK_EQ(tree("- a\n\n- b\n"), std::string("doc\n  ul loose\n    li\n      p \"a\"\n    li\n      p \"b\"\n"));
    CHECK_EQ(tree("- [x] done\n- [ ] todo\n"), std::string("doc\n  ul\n    li [x] \"done\"\n    li [ ] \"todo\"\n"));
    // rules
    CHECK_EQ(tree("a\n\n***\n- - -\n___\n"), std::string("doc\n  p \"a\"\n  hr\n  hr\n  hr\n"));
    // links and images: inline, reference, autolinks
    Doc d;
    parse("[here](docs/ui.md#menus) and [ref][r] <https://x.org> www.amiga.org\n\n[r]: http://a.b/c\n\n![Shot *1*](img/a.png)\n", d);
    CHECK_EQ(d.links.size(), (size_t)4);
    CHECK_EQ(d.links[0], std::string("docs/ui.md#menus"));
    CHECK_EQ(d.links[1], std::string("http://a.b/c"));
    CHECK_EQ(d.links[2], std::string("https://x.org"));
    CHECK_EQ(d.images.size(), (size_t)1);
    CHECK_EQ(d.images[0].src, std::string("img/a.png"));
    CHECK_EQ(d.images[0].alt, std::string("Shot 1"));
    CHECK(has(dump(d), "p \"![Shot 1](img 0)\""));
    // GFM tables with alignment
    std::string t = tree("| L | C | R | - |\n|:--|:-:|--:|---|\n| a | b | c | d |\n");
    CHECK(has(t, "th align=l \"L\"") && has(t, "th align=c \"C\"") && has(t, "th align=r \"R\"") && has(t, "th \"-\""));
    CHECK(has(t, "td align=r \"c\""));
    // HTML blocks: their text, <img>, <a>, <br>, <b>, and tables (READMEs put pictures in them)
    CHECK_EQ(tree("<div>\nhi <b>there</b>\n</div>\n"), std::string("doc\n  html \"hi **there**\"\n"));
    CHECK_EQ(tree("<!-- a comment -->\n"), std::string("doc\n  html\n"));
    {
        Doc h;
        parse("<table><tr>\n<td align=\"center\"><a href=\"docs/g.md#x\"><img src=\"a.png\" width=\"200\" alt=\"A &amp; B\"><br>Caption</a></td>\n"
              "<td><img src='b.png' alt=Bee></td></tr></table>\n\nAfter <img src=\"c.png\" alt=\"inline\"> and <a href=\"u.md\">link</a>.\n", h);
        std::string t = dump(h);
        CHECK(has(t, "html\n    table\n      tbody\n        tr\n          td align=c \"[![A & B](img 0)](link 0)[\\n](link 0)[Caption](link 0)\"\n"
                     "          td \"![Bee](img 1)\"\n"));
        CHECK(has(t, "p \"After ![inline](img 2) and [link](link 1).\""));
        CHECK_EQ(h.images[0].width, 200);
        CHECK_EQ(h.images[0].src, std::string("a.png"));
        CHECK_EQ(h.links[0], std::string("docs/g.md#x"));
        CHECK_EQ(h.images[1].alt, std::string("Bee"));
    }
    CHECK_EQ(tree("&amp; &lt; &copy; &#233; &mdash; &#x2192;\n"), std::string("doc\n  p \"& < \xA9 \xE9 - ->\"\n"));
    // escapes
    CHECK_EQ(tree("\\*not\\* \\# x\n"), std::string("doc\n  p \"*not* # x\"\n"));
}

TEST(md_latin1)
{
    CHECK_EQ(toLatin1("a \xE2\x80\x94 b \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x98s\xE2\x80\x99 \xE2\x86\x92 \xE2\x80\xA6"),
             std::string("a - b \"q\" 's' -> ..."));
    CHECK_EQ(toLatin1("Caf\xC3\xA9 \xC3\x9F"), std::string("Caf\xE9 \xDF"));
    CHECK_EQ(toLatin1("Caf\xE9"), std::string("Caf\xE9"));                // already Latin-1
    CHECK_EQ(toLatin1("\xE2\x80\xA2 x \xE2\x9C\x93"), std::string("\xB7 x x"));
    CHECK_EQ(toLatin1("\xF0\x9F\x8E\xAE game"), std::string("? game"));   // emoji
    CHECK_EQ(toLatin1("cut \xE2\x80"), std::string("cut \xE2\x80"));      // truncated UTF-8: kept as bytes
    CHECK_EQ(toLatin1("\xEF\xBB\xBFstart"), std::string("start"));        // BOM
    Doc d;
    parse("# Boing \xE2\x80\x94 BASIC\n", d);
    CHECK_EQ(plainText(*d.root.children[0]), std::string("Boing - BASIC"));
}

TEST(md_layout_wrap)
{
    FakeMetrics m;
    Doc d;
    parse("aaaa bbbb cccc dddd\n", d);
    // 100 px: 88 for text after the margins = 11 characters -> two words a line
    Layout l = layout(d, 100, m);
    std::vector<const Item *> texts;
    for (auto &it : l.items) if (it.kind == ItemKind::Text) texts.push_back(&it);
    CHECK_EQ(texts.size(), (size_t)2);
    CHECK_EQ(texts[0]->text, std::string("aaaa bbbb"));
    CHECK_EQ(texts[1]->text, std::string("cccc dddd"));
    CHECK_EQ(texts[1]->y - texts[0]->y, 16);
    CHECK(texts[0]->x + texts[0]->w <= 100 - 6);
    // wide enough: one line
    l = layout(d, 400, m);
    int n = 0;
    for (auto &it : l.items) if (it.kind == ItemKind::Text) n++;
    CHECK_EQ(n, 1);
    // a word longer than the line is cut, never wider than the view
    parse("supercalifragilisticexpialidocious\n", d);
    l = layout(d, 100, m);
    for (auto &it : l.items) CHECK(it.x + it.w <= 100);
    CHECK(l.items.size() >= 3);
    // a minimum width: no endless loop, no zero-width lines
    l = layout(d, 1, m);
    CHECK(l.height > 0 && l.height < 100000);
    // hard breaks
    parse("one  \ntwo\n", d);
    l = layout(d, 400, m);
    texts.clear();
    for (auto &it : l.items) if (it.kind == ItemKind::Text) texts.push_back(&it);
    CHECK(texts.size() == 2 && texts[1]->y > texts[0]->y);
}

TEST(md_layout_blocks)
{
    FakeMetrics m;
    m.pics["shot.png"] = { 800, 400 };
    Doc d;
    parse("# Head\n\n**bold** and `code`\n\n- item\n\n> q\n\n```\nlong line of code that is wider than the view\n```\n\n"
          "| A | Bee |\n|---|--:|\n| 1 | 22 |\n\n![big](shot.png) ![gone](missing.png)\n\n---\n", d);
    Layout l = layout(d, 300, m);
    auto find = [&](ItemKind k, const std::string &text = "") -> const Item * {
        for (auto &it : l.items)
            if (it.kind == k && (text.empty() || it.text == text)) return &it;
        return nullptr;
    };
    const Item *h = find(ItemKind::Text, "Head");
    CHECK(h && h->font == FH1 && h->color == CHeading);
    CHECK(find(ItemKind::Rule) != nullptr);   // under the h1, the table, the hr
    const Item *b = find(ItemKind::Text, "bold");
    CHECK(b && b->font == FBold);
    const Item *c = find(ItemKind::Text, "code");
    CHECK(c && c->font == FMono);
    CHECK(find(ItemKind::CodeBg) != nullptr);
    const Item *bullet = find(ItemKind::Text, "\xB7");
    const Item *it = find(ItemKind::Text, "item");
    CHECK(bullet && it && bullet->x < it->x && bullet->y == it->y);
    const Item *bar = find(ItemKind::QuoteBar);
    const Item *q = find(ItemKind::Text, "q");
    CHECK(bar && q && bar->x < q->x && q->color == CMuted);
    // the code block isn't wrapped: the layout is wider than the view (scrolls sideways)
    const Item *code = find(ItemKind::Text, "long line of code that is wider than the view");
    CHECK(code && code->font == FMono);
    CHECK(l.width > 300);
    // the table: header shaded, right-aligned column
    CHECK(find(ItemKind::TableHead) != nullptr);
    const Item *bee = find(ItemKind::Text, "Bee"), *v22 = find(ItemKind::Text, "22");
    CHECK(bee && v22 && bee->font == FBold && v22->x + v22->w >= bee->x + bee->w - 1);
    // the picture is scaled down to the width, keeping its shape; the missing one is its alt box
    const Item *pic = find(ItemKind::Picture);
    CHECK(pic && pic->w <= 300 - 12 && pic->w * 400 == pic->h * 800);
    const Item *alt = find(ItemKind::AltBox);
    CHECK(alt && alt->text == "gone");
    // everything inside the layout's size
    for (auto &x : l.items) CHECK(x.y + x.h <= l.height && x.x + x.w <= l.width);
}

TEST(md_layout_html_pictures)
{
    FakeMetrics m;
    m.pics["a.png"] = { 640, 480 };
    Doc d;
    parse("<table><tr><td><img src=\"a.png\" width=\"200\" alt=\"A\"><br>Cap</td><td><img src=\"a.png\" width=\"200\"></td></tr></table>\n", d);
    Layout l = layout(d, 600, m);
    int pics = 0;
    for (auto &it : l.items)
        if (it.kind == ItemKind::Picture) {
            pics++;
            CHECK(it.w == 200 && it.h == 150);   // its width=, the shape kept
        }
    CHECK_EQ(pics, 2);
    // narrower than the pictures: they shrink with their cells
    l = layout(d, 200, m);
    for (auto &it : l.items) CHECK(it.x + it.w <= 200);
}

TEST(md_layout_table_shrinks)
{
    FakeMetrics m;
    Doc d;
    parse("| a | b |\n|---|---|\n| short | a very long cell text that must wrap into several lines |\n", d);
    Layout l = layout(d, 240, m);
    int maxRight = 0, lines = 0;
    for (auto &it : l.items) {
        if (it.kind == ItemKind::Rule) maxRight = std::max(maxRight, it.x + it.w);
        if (it.kind == ItemKind::Text && it.x > 50) lines++;
    }
    CHECK(maxRight <= 240);
    CHECK(lines >= 3);
}

TEST(md_links)
{
    FakeMetrics m;
    Doc d;
    parse("see [the docs](docs/ui.md) now\n", d);
    Layout l = layout(d, 400, m);
    const Item *li = nullptr;
    for (auto &it : l.items) if (it.link == 0) li = &it;
    CHECK(li && li->color == CLink && li->text == "the docs");
    CHECK_EQ(linkAt(l, li->x + 2, li->y + 2), 0);
    CHECK_EQ(linkAt(l, 1, 1), -1);
    CHECK(linkKind("docs/ui.md") == LinkKind::File);
    CHECK(linkKind("../README.md#x") == LinkKind::File);
    CHECK(linkKind("https://github.com/x") == LinkKind::Web);
    CHECK(linkKind("HTTP://x") == LinkKind::Web);
    CHECK(linkKind("mailto:a@b") == LinkKind::Web);
    CHECK(linkKind("#install") == LinkKind::Anchor);
    CHECK(linkKind("ftp://x") == LinkKind::Web);
    CHECK(linkKind("gopher://x") == LinkKind::Other);
    CHECK_EQ(resolvePath("Work:bb/README.md", "docs/ui.md#menus"), std::string("Work:bb/docs/ui.md"));
    CHECK_EQ(resolvePath("Work:bb/docs/ui.md", "../README.md"), std::string("Work:bb/README.md"));
    CHECK_EQ(resolvePath("Work:bb/docs/ui.md", "./forms%20events.md"), std::string("Work:bb/docs/forms events.md"));
    CHECK_EQ(resolvePath("Work:README.md", "../x.md"), std::string("Work:/x.md"));
    CHECK_EQ(resolvePath("Work:a/b.md", "DH1:c.md"), std::string("DH1:c.md"));
    CHECK_EQ(resolvePath("RAM:x.md", "y.md"), std::string("RAM:y.md"));
}

TEST(md_odd_input)
{
    FakeMetrics m;
    // deep nesting is cut off (and says so) instead of recursing without end
    std::string deep(5000, '>');
    deep += " x\n";
    Doc d;
    CHECK(parse(deep, d));
    CHECK(d.truncated);
    Layout l = layout(d, 300, m);
    CHECK(l.height > 0);
    std::string lists;
    for (int i = 0; i < 200; i++) lists += std::string(i * 2, ' ') + "- x\n";
    parse(lists, d);
    CHECK(d.truncated);
    layout(d, 300, m);
    // a big document: it still parses, in bounds
    std::string big;
    for (int i = 0; i < 20000; i++) big += "Line " + std::to_string(i) + " with *some* `code` and [a](b.md)\n\n";
    CHECK(parse(big, d));
    CHECK(d.root.children.size() == 20000);
    l = layout(d, 500, m);
    CHECK(l.height > 20000 * 16);
    // binary junk and stray NULs
    std::string junk;
    for (int i = 0; i < 4096; i++) junk += (char)((i * 37) & 0xFF);
    CHECK(parse(junk, d));
    layout(d, 200, m);
    CHECK(parse(std::string("a\0b\n", 4), d));
    // empty
    CHECK(parse("", d));
    l = layout(d, 300, m);
    CHECK(l.items.empty());
    // too big: cut at kMaxInput
    std::string huge(kMaxInput + 10, 'x');
    CHECK(parse(huge, d));
    CHECK(d.truncated);
}
