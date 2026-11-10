// --- markdown --- the Markdown block tree (md4c) and its layout; see Markdown.h.
#include "Markdown.h"

#include "../../third_party/md4c/md4c.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace af {
namespace md {

// --- text ------------------------------------------------------------------------------------

std::string toLatin1(const std::string &t)
{
    std::string o;
    o.reserve(t.size());
    for (size_t i = 0; i < t.size();) {
        unsigned char c = (unsigned char)t[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        bool ok = len > 1 && i + len <= t.size();
        for (int k = 1; ok && k < len; k++) ok = ((unsigned char)t[i + k] & 0xC0) == 0x80;
        if (len == 1 || !ok) {   // ASCII, or not UTF-8: Latin-1 already
            o += (char)c;
            i++;
            continue;
        }
        uint32_t cp = c & (0xFF >> (len + 1));
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((unsigned char)t[i + k] & 0x3F);
        i += len;
        if (cp < 0x100) { o += (char)cp; continue; }
        switch (cp) {
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: o += '-'; break;
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: o += '\''; break;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: o += '"'; break;
        case 0x2026: o += "..."; break;
        case 0x2022: case 0x2023: case 0x25CF: case 0x25E6: o += '\xB7'; break;   // bullets -> middle dot
        case 0x2192: case 0x27F6: o += "->"; break;
        case 0x2190: case 0x27F5: o += "<-"; break;
        case 0x2194: o += "<->"; break;
        case 0x21D2: o += "=>"; break;
        case 0x2264: o += "<="; break;
        case 0x2265: o += ">="; break;
        case 0x2260: o += "!="; break;
        case 0x2248: o += "~"; break;
        case 0x2122: o += "(TM)"; break;
        case 0x2713: case 0x2714: case 0x2705: o += 'x'; break;
        case 0x2717: case 0x2718: case 0x274C: o += 'x'; break;
        case 0x2009: case 0x200A: case 0x202F: case 0x2002: case 0x2003: o += ' '; break;
        case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: case 0xFE0F: break;   // invisible
        case 0x20AC: o += "EUR"; break;
        default: o += '?'; break;
        }
    }
    return o;
}

static std::string utf8Of(uint32_t cp)
{
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

// "&amp;" "&#233;" "&#x2014;" -> UTF-8 (then Latin-1 like the rest); unknown ones as written
static std::string entity(const std::string &e)
{
    static const struct { const char *name; uint32_t cp; } named[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
        { "copy", 0xA9 }, { "reg", 0xAE }, { "deg", 0xB0 }, { "middot", 0xB7 }, { "times", 0xD7 },
        { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "rarr", 0x2192 }, { "larr", 0x2190 },
        { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 },
        { "trade", 0x2122 }, { "euro", 0x20AC }, { "eacute", 0xE9 }, { "auml", 0xE4 }, { "ouml", 0xF6 },
        { "uuml", 0xFC }, { "szlig", 0xDF },
    };
    if (e.size() < 3 || e[0] != '&' || e.back() != ';') return e;
    std::string body = e.substr(1, e.size() - 2);
    if (body[0] == '#') {
        uint32_t cp = (body.size() > 1 && (body[1] == 'x' || body[1] == 'X')) ? strtoul(body.c_str() + 2, nullptr, 16)
                                                                               : strtoul(body.c_str() + 1, nullptr, 10);
        return cp ? utf8Of(cp) : e;
    }
    for (auto &n : named)
        if (body == n.name) return utf8Of(n.cp);
    return e;
}

// --- parsing --------------------------------------------------------------------------------

const char *blockName(BlockType t)
{
    static const char *names[] = { "doc", "quote", "ul", "ol", "li", "hr", "h", "code", "html", "p",
                                   "table", "thead", "tbody", "tr", "th", "td" };
    return names[(int)t];
}

namespace {

struct Ctx {
    Doc *doc = nullptr;
    Block *cur = nullptr;
    int depth = 0, skip = 0;
    size_t blocks = 0;
    int em = 0, strong = 0, code = 0, del = 0, u = 0;
    std::vector<int> links;     // the open links (nested in images...)
    std::string htmlRaw;        // an HTML block's text, converted when it ends
    int image = -1;             // the open image (its text is the alt)
    std::string alt;

    uint8_t style() const
    {
        return (uint8_t)((em ? Italic : 0) | (strong ? Bold : 0) | (code ? CodeSpan : 0) | (del ? Strike : 0) |
                         (u ? Underline : 0));
    }
    void add(const std::string &utf8)
    {
        if (utf8.empty() || !cur) return;
        std::string t = toLatin1(utf8);
        if (image >= 0) { alt += t; return; }
        int link = links.empty() ? -1 : links.back();
        auto &runs = cur->runs;
        if (!runs.empty() && runs.back().style == style() && runs.back().link == link && runs.back().image < 0 &&
            runs.back().text != "\n" && t != "\n") {
            runs.back().text += t;
            return;
        }
        Run r;
        r.text = t;
        r.style = style();
        r.link = link;
        runs.push_back(r);
    }
};

std::string attr(const MD_ATTRIBUTE &a)
{
    std::string s;
    if (!a.text) return s;
    // its substrings: entities decoded, the rest as it is
    for (int i = 0; a.substr_offsets && a.substr_offsets[i] < a.size; i++) {
        MD_OFFSET b = a.substr_offsets[i], e = a.substr_offsets[i + 1];
        std::string piece(a.text + b, e - b);
        s += a.substr_types[i] == MD_TEXT_ENTITY ? entity(piece) : a.substr_types[i] == MD_TEXT_NULLCHAR ? "" : piece;
    }
    if (!a.substr_offsets) s.assign(a.text, a.size);
    return s;
}

// --- a little HTML ------------------------------------------------------------------------------

struct Tag {
    std::string name;     // lower case, "/a" for a closing tag, "!" for a comment
    std::string raw;
};

std::string tagAttr(const std::string &tag, const std::string &name)
{
    // name="value", name='value' or name=value, any case
    std::string low;
    for (char ch : tag) low += (char)tolower((unsigned char)ch);
    size_t p = 0;
    while ((p = low.find(name, p)) != std::string::npos) {
        bool start = p > 0 && (isspace((unsigned char)low[p - 1]));
        size_t e = p + name.size();
        while (e < low.size() && isspace((unsigned char)low[e])) e++;
        if (!start || e >= low.size() || low[e] != '=') { p++; continue; }
        e++;
        while (e < low.size() && isspace((unsigned char)low[e])) e++;
        if (e >= tag.size()) return "";
        char q = tag[e];
        if (q == '"' || q == '\'') {
            size_t f = tag.find(q, e + 1);
            return tag.substr(e + 1, f == std::string::npos ? std::string::npos : f - e - 1);
        }
        size_t f = e;
        while (f < tag.size() && !isspace((unsigned char)tag[f]) && tag[f] != '>' && tag[f] != '/') f++;
        return tag.substr(e, f - e);
    }
    return "";
}

std::string tagName(const std::string &t)
{
    if (t.compare(0, 4, "<!--") == 0) return "!";
    size_t i = 1;
    std::string n;
    if (i < t.size() && t[i] == '/') { n = "/"; i++; }
    while (i < t.size() && (isalnum((unsigned char)t[i]))) n += (char)tolower((unsigned char)t[i++]);
    return n;
}

// text with its entities decoded (as UTF-8)
std::string decodeEntities(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            size_t e = s.find(';', i);
            if (e != std::string::npos && e - i <= 10) {
                o += entity(s.substr(i, e - i + 1));
                i = e;
                continue;
            }
        }
        o += s[i];
    }
    return o;
}

void addImage(Ctx &c, const std::string &tag)
{
    Image im;
    im.src = toLatin1(decodeEntities(tagAttr(tag, "src")));
    im.alt = toLatin1(decodeEntities(tagAttr(tag, "alt")));
    im.width = atoi(tagAttr(tag, "width").c_str());
    if (im.src.empty() || !c.cur) return;
    c.doc->images.push_back(im);
    Run r;
    r.text = im.alt;
    r.style = c.style();
    r.link = c.links.empty() ? -1 : c.links.back();
    r.image = (int)c.doc->images.size() - 1;
    c.cur->runs.push_back(r);
}

bool commonTag(Ctx &c, const std::string &name, const std::string &raw)
{
    if (name == "img") addImage(c, raw);
    else if (name == "br") c.add("\n");
    else if (name == "a") {
        std::string href = tagAttr(raw, "href");
        c.doc->links.push_back(toLatin1(decodeEntities(href)));
        c.links.push_back((int)c.doc->links.size() - 1);
    } else if (name == "/a") { if (!c.links.empty()) c.links.pop_back(); }
    else if (name == "b" || name == "strong") c.strong++;
    else if (name == "/b" || name == "/strong") c.strong = std::max(0, c.strong - 1);
    else if (name == "i" || name == "em") c.em++;
    else if (name == "/i" || name == "/em") c.em = std::max(0, c.em - 1);
    else if (name == "code" || name == "tt") c.code++;
    else if (name == "/code" || name == "/tt") c.code = std::max(0, c.code - 1);
    else return false;
    return true;
}

void inlineHtml(Ctx &c, const std::string &s)
{
    std::string n = tagName(s);
    commonTag(c, n, s);   // others are dropped
}

// an HTML block: its text and the tags above as runs; tables as table blocks under it
void htmlBlock(Ctx &c, const std::string &raw)
{
    Block *html = c.cur;
    Block *table = nullptr, *row = nullptr;
    int savedEm = c.em, savedStrong = c.strong, savedCode = c.code;
    size_t savedLinks = c.links.size();
    auto text = [&](std::string t) {
        // white space collapses, as in HTML
        std::string o;
        bool sp = false;
        for (char ch : t) {
            if (isspace((unsigned char)ch)) { sp = true; continue; }
            if (sp && !o.empty()) o += ' ';
            if (sp && o.empty() && !c.cur->runs.empty() && c.cur->runs.back().text != "\n") o += ' ';
            sp = false;
            o += ch;
        }
        if (sp && !o.empty()) o += ' ';
        if (!o.empty()) c.add(decodeEntities(o));
    };
    size_t i = 0;
    while (i < raw.size() && c.blocks < kMaxBlocks) {
        size_t lt = raw.find('<', i);
        if (lt == std::string::npos) { text(raw.substr(i)); break; }
        if (lt > i) text(raw.substr(i, lt - i));
        size_t gt;
        if (raw.compare(lt, 4, "<!--") == 0) {
            gt = raw.find("-->", lt);
            i = gt == std::string::npos ? raw.size() : gt + 3;
            continue;
        }
        gt = raw.find('>', lt);
        if (gt == std::string::npos) { text(raw.substr(lt)); break; }
        std::string tag = raw.substr(lt, gt - lt + 1);
        std::string n = tagName(tag);
        i = gt + 1;
        if (commonTag(c, n, tag)) continue;
        auto newBlock = [&](Block *parent, BlockType t) {
            auto b = std::make_unique<Block>();
            b->type = t;
            b->parent = parent;
            Block *p = b.get();
            parent->children.push_back(std::move(b));
            c.blocks++;
            return p;
        };
        if (n == "table" && !table && c.depth < kMaxDepth) {
            table = newBlock(html, BlockType::Table);
            newBlock(table, BlockType::TBody);
        } else if (n == "/table") {
            table = row = nullptr;
            c.cur = html;
        } else if (n == "tr" && table) {
            row = newBlock(table->children.back().get(), BlockType::TR);
            c.cur = html;
        } else if ((n == "td" || n == "th") && table) {
            if (!row) row = newBlock(table->children.back().get(), BlockType::TR);
            Block *cell = newBlock(row, n == "th" ? BlockType::TH : BlockType::TD);
            std::string al = tagAttr(tag, "align");
            cell->align = al == "center" ? 'c' : al == "right" ? 'r' : al == "left" ? 'l' : 0;
            c.cur = cell;
        } else if ((n == "/td" || n == "/th") && table) {
            c.cur = html;
        } else if (n == "p" || n == "/p" || n == "div" || n == "/div" || (n.size() == 2 && n[0] == 'h' && isdigit((unsigned char)n[1])) ||
                   (n.size() == 3 && n[1] == 'h' && isdigit((unsigned char)n[2])) || n == "li" || n == "hr") {
            if (!c.cur->runs.empty() && c.cur->runs.back().text != "\n") c.add("\n");
        }
    }
    c.cur = html;
    c.em = savedEm;
    c.strong = savedStrong;
    c.code = savedCode;
    c.links.resize(std::min(savedLinks, c.links.size()));
    // trailing breaks
    while (!html->runs.empty() && html->runs.back().text == "\n") html->runs.pop_back();
}

int enterBlock(MD_BLOCKTYPE type, void *detail, void *ud)
{
    Ctx &c = *(Ctx *)ud;
    if (type == MD_BLOCK_DOC) {
        c.cur = &c.doc->root;
        return 0;
    }
    if (c.skip || c.depth >= kMaxDepth || c.blocks >= kMaxBlocks) {
        c.skip++;
        c.doc->truncated = true;
        return 0;
    }
    auto b = std::make_unique<Block>();
    b->parent = c.cur;
    switch (type) {
    case MD_BLOCK_QUOTE: b->type = BlockType::Quote; break;
    case MD_BLOCK_UL: b->type = BlockType::UL; b->tight = ((MD_BLOCK_UL_DETAIL *)detail)->is_tight != 0; break;
    case MD_BLOCK_OL:
        b->type = BlockType::OL;
        b->tight = ((MD_BLOCK_OL_DETAIL *)detail)->is_tight != 0;
        b->start = ((MD_BLOCK_OL_DETAIL *)detail)->start;
        break;
    case MD_BLOCK_LI: {
        b->type = BlockType::LI;
        auto *d = (MD_BLOCK_LI_DETAIL *)detail;
        b->task = d->is_task != 0;
        b->checked = b->task && (d->task_mark == 'x' || d->task_mark == 'X');
        break;
    }
    case MD_BLOCK_HR: b->type = BlockType::HR; break;
    case MD_BLOCK_H: b->type = BlockType::Heading; b->level = (int)((MD_BLOCK_H_DETAIL *)detail)->level; break;
    case MD_BLOCK_CODE: b->type = BlockType::Code; b->info = toLatin1(attr(((MD_BLOCK_CODE_DETAIL *)detail)->lang)); break;
    case MD_BLOCK_HTML: b->type = BlockType::Html; break;
    case MD_BLOCK_P: b->type = BlockType::Para; break;
    case MD_BLOCK_TABLE: b->type = BlockType::Table; break;
    case MD_BLOCK_THEAD: b->type = BlockType::THead; break;
    case MD_BLOCK_TBODY: b->type = BlockType::TBody; break;
    case MD_BLOCK_TR: b->type = BlockType::TR; break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        b->type = type == MD_BLOCK_TH ? BlockType::TH : BlockType::TD;
        MD_ALIGN a = ((MD_BLOCK_TD_DETAIL *)detail)->align;
        b->align = a == MD_ALIGN_LEFT ? 'l' : a == MD_ALIGN_CENTER ? 'c' : a == MD_ALIGN_RIGHT ? 'r' : 0;
        break;
    }
    default: b->type = BlockType::Para; break;
    }
    Block *p = b.get();
    c.cur->children.push_back(std::move(b));
    c.cur = p;
    c.depth++;
    c.blocks++;
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void *, void *ud)
{
    Ctx &c = *(Ctx *)ud;
    if (type == MD_BLOCK_DOC) return 0;
    if (c.skip) { c.skip--; return 0; }
    if (type == MD_BLOCK_HTML && c.cur && c.cur->type == BlockType::Html) {
        htmlBlock(c, c.htmlRaw);
        c.htmlRaw.clear();
    }
    if (c.cur && c.cur->parent) c.cur = c.cur->parent;
    c.depth--;
    return 0;
}

int enterSpan(MD_SPANTYPE type, void *detail, void *ud)
{
    Ctx &c = *(Ctx *)ud;
    switch (type) {
    case MD_SPAN_EM: c.em++; break;
    case MD_SPAN_STRONG: c.strong++; break;
    case MD_SPAN_CODE: c.code++; break;
    case MD_SPAN_DEL: c.del++; break;
    case MD_SPAN_U: c.u++; break;
    case MD_SPAN_A:
        c.doc->links.push_back(toLatin1(attr(((MD_SPAN_A_DETAIL *)detail)->href)));
        c.links.push_back((int)c.doc->links.size() - 1);
        break;
    case MD_SPAN_IMG:
        if (c.image < 0) {
            Image im;
            im.src = toLatin1(attr(((MD_SPAN_IMG_DETAIL *)detail)->src));
            c.doc->images.push_back(im);
            c.image = (int)c.doc->images.size() - 1;
            c.alt.clear();
        }
        break;
    default: break;
    }
    return 0;
}

int leaveSpan(MD_SPANTYPE type, void *detail, void *ud)
{
    Ctx &c = *(Ctx *)ud;
    switch (type) {
    case MD_SPAN_EM: c.em--; break;
    case MD_SPAN_STRONG: c.strong--; break;
    case MD_SPAN_CODE: c.code--; break;
    case MD_SPAN_DEL: c.del--; break;
    case MD_SPAN_U: c.u--; break;
    case MD_SPAN_A: if (!c.links.empty()) c.links.pop_back(); break;
    case MD_SPAN_IMG: {
        if (c.image < 0 || c.doc->images[c.image].src != toLatin1(attr(((MD_SPAN_IMG_DETAIL *)detail)->src))) break;
        c.doc->images[c.image].alt = c.alt;
        if (c.cur) {
            Run r;
            r.text = c.alt;
            r.style = c.style();
            r.link = c.links.empty() ? -1 : c.links.back();
            r.image = c.image;
            c.cur->runs.push_back(r);
        }
        c.image = -1;
        break;
    }
    default: break;
    }
    return 0;
}

int text(MD_TEXTTYPE type, const MD_CHAR *t, MD_SIZE size, void *ud)
{
    Ctx &c = *(Ctx *)ud;
    std::string s(t, size);
    switch (type) {
    case MD_TEXT_NULLCHAR: break;
    case MD_TEXT_BR: c.add("\n"); break;
    case MD_TEXT_SOFTBR: c.add(" "); break;
    case MD_TEXT_ENTITY: c.add(entity(s)); break;
    case MD_TEXT_HTML:
        // an HTML block is converted when it ends; inline HTML: <img>, <a href>, <br>
        if (c.cur && c.cur->type == BlockType::Html && !c.skip) c.htmlRaw += s;
        else inlineHtml(c, s);
        break;
    default:
        if (c.cur && c.cur->type == BlockType::Html && !c.skip) c.htmlRaw += s;
        else c.add(s);
        break;
    }
    return 0;
}

void dumpBlock(const Block &b, int depth, const Doc &doc, std::string &out)
{
    out += std::string(depth * 2, ' ');
    out += blockName(b.type);
    if (b.type == BlockType::Heading) out += std::to_string(b.level);
    if (b.type == BlockType::OL && b.start != 1) out += " start=" + std::to_string(b.start);
    if ((b.type == BlockType::UL || b.type == BlockType::OL) && !b.tight) out += " loose";
    if (b.type == BlockType::LI && b.task) out += b.checked ? " [x]" : " [ ]";
    if ((b.type == BlockType::TH || b.type == BlockType::TD) && b.align) out += std::string(" align=") + b.align;
    if (b.type == BlockType::Code && !b.info.empty()) out += " " + b.info;
    if (!b.runs.empty()) {
        out += " \"";
        for (auto &r : b.runs) {
            std::string t = r.text;
            for (size_t i = 0; (i = t.find('\n', i)) != std::string::npos;) t.replace(i, 1, "\\n");
            if (r.image >= 0) {
                std::string img = "![" + t + "](img " + std::to_string(r.image) + ")";
                out += r.link >= 0 ? "[" + img + "](link " + std::to_string(r.link) + ")" : img;
                continue;
            }
            std::string open, close;
            if (r.style & CodeSpan) { open += "`"; close = "`" + close; }
            if (r.style & Bold) { open += "**"; close = "**" + close; }
            if (r.style & Italic) { open += "*"; close = "*" + close; }
            if (r.style & Strike) { open += "~~"; close = "~~" + close; }
            if (b.type == BlockType::Code) open.clear(), close.clear();
            std::string piece = open + t + close;
            if (r.link >= 0) piece = "[" + piece + "](link " + std::to_string(r.link) + ")";
            out += piece;
        }
        out += "\"";
    }
    out += "\n";
    for (auto &c : b.children) dumpBlock(*c, depth + 1, doc, out);
}

} // namespace

bool parse(const std::string &utf8In, Doc &out)
{
    out = Doc();
    std::string utf8 = utf8In;
    if (utf8.size() > kMaxInput) {
        utf8.resize(kMaxInput);
        out.truncated = true;
    }
    Ctx c;
    c.doc = &out;
    c.cur = &out.root;
    MD_PARSER p;
    memset(&p, 0, sizeof p);
    p.abi_version = 0;
    p.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEURLAUTOLINKS |
              MD_FLAG_PERMISSIVEWWWAUTOLINKS;
    p.enter_block = enterBlock;
    p.leave_block = leaveBlock;
    p.enter_span = enterSpan;
    p.leave_span = leaveSpan;
    p.text = text;
    return md_parse(utf8.data(), (MD_SIZE)utf8.size(), &p, &c) == 0;
}

std::string dump(const Doc &doc)
{
    std::string out;
    dumpBlock(doc.root, 0, doc, out);
    if (doc.truncated) out += "(truncated)\n";
    return out;
}

std::string plainText(const Block &b)
{
    std::string s;
    for (auto &r : b.runs) s += r.text;
    return s;
}

// --- links -----------------------------------------------------------------------------------

LinkKind linkKind(const std::string &href)
{
    std::string h;
    for (char ch : href) h += (char)tolower((unsigned char)ch);
    if (h.compare(0, 7, "http://") == 0 || h.compare(0, 8, "https://") == 0 || h.compare(0, 4, "www.") == 0 ||
        h.compare(0, 7, "mailto:") == 0 || h.compare(0, 6, "ftp://") == 0)
        return LinkKind::Web;
    if (!h.empty() && h[0] == '#') return LinkKind::Anchor;
    size_t colon = h.find(':');
    if (colon != std::string::npos && colon + 2 < h.size() && h[colon + 1] == '/' && h[colon + 2] == '/')
        return LinkKind::Other;   // another scheme
    return h.empty() ? LinkKind::Other : LinkKind::File;
}

std::string resolvePath(const std::string &docPath, const std::string &hrefIn)
{
    std::string href = hrefIn;
    size_t cut = href.find_first_of("#?");
    if (cut != std::string::npos) href.resize(cut);
    // %20 and friends
    std::string h;
    for (size_t i = 0; i < href.size(); i++) {
        if (href[i] == '%' && i + 2 < href.size() && isxdigit((unsigned char)href[i + 1]) && isxdigit((unsigned char)href[i + 2])) {
            h += (char)strtol(href.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else h += href[i];
    }
    if (h.find(':') != std::string::npos) return h;   // an AmigaDOS path already
    size_t slash = docPath.find_last_of("/:");
    std::string dir = slash == std::string::npos ? "" : docPath.substr(0, slash + 1);
    size_t p = 0;
    while (p < h.size()) {
        size_t e = h.find('/', p);
        std::string seg = h.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? h.size() : e + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            // up one drawer: "Work:a/b/" -> "Work:a/"; at the volume, an AmigaDOS "/"
            if (!dir.empty() && dir.back() == '/') {
                size_t prev = dir.find_last_of("/:", dir.size() - 2);
                if (prev != std::string::npos && dir.compare(prev + 1, std::string::npos, "/") != 0) {
                    dir.resize(prev + 1);
                    continue;
                }
            }
            dir += "/";
            continue;
        }
        dir += seg;
        if (e != std::string::npos) dir += "/";
    }
    return dir;
}

// --- layout ----------------------------------------------------------------------------------

namespace {

struct Token {
    std::string text;
    int font = FBody;
    uint8_t style = 0;
    int link = -1;
    int image = -1;          // a picture (w x h) or, if alt, its alt box
    bool alt = false;
    bool space = false, brk = false;
    int w = 0, h = 0;
};

struct Lay {
    const Doc &doc;
    Metrics &m;
    Layout out;
    int lh, gap;

    Lay(const Doc &d, Metrics &mm) : doc(d), m(mm)
    {
        lh = std::max(1, m.lineHeight(FBody));
        gap = std::max(2, lh / 2);
    }

    static int fontFor(int base, uint8_t style)
    {
        if (style & CodeSpan) return FMono;
        if (base >= FH1) return base;
        bool b = (style & Bold) || base == FBold, i = (style & Italic) != 0;
        return b && i ? FBoldItalic : b ? FBold : i ? FItalic : FBody;
    }

    void push(const Item &it)
    {
        out.items.push_back(it);
        out.width = std::max(out.width, it.x + it.w);
    }

    // words and spaces of the runs, measured
    std::vector<Token> tokens(const std::vector<Run> &runs, int base, int maxW)
    {
        std::vector<Token> v;
        for (auto &r : runs) {
            if (r.image >= 0) {
                Token t;
                t.image = r.image;
                t.link = r.link;
                int w = 0, h = 0;
                const Image &im = doc.images[r.image];
                int cap = im.width > 0 ? std::min(maxW, im.width) : maxW;
                if (m.imageSize(im.src, w, h) && w > 0 && h > 0) {
                    if (w > cap) { h = (int)((long long)h * cap / w); w = cap; }
                    t.w = std::max(1, w);
                    t.h = std::max(1, h);
                } else {
                    t.alt = true;
                    t.text = r.text.empty() ? "[picture]" : r.text;
                    t.font = FItalic;
                    t.w = std::min(maxW, m.textWidth(FItalic, t.text) + 8);
                    t.h = m.lineHeight(FItalic) + 4;
                }
                v.push_back(t);
                continue;
            }
            if (r.text == "\n") {
                Token t;
                t.brk = true;
                v.push_back(t);
                continue;
            }
            int font = fontFor(base, r.style);
            size_t i = 0;
            const std::string &s = r.text;
            while (i < s.size()) {
                Token t;
                t.font = font;
                t.style = r.style;
                t.link = r.link;
                size_t j = i;
                bool sp = s[i] == ' ' || s[i] == '\t';
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t') == sp) j++;
                t.text = sp ? std::string(" ") : s.substr(i, j - i);
                t.space = sp;
                t.w = m.textWidth(font, t.text);
                t.h = m.lineHeight(font);
                v.push_back(t);
                i = j;
            }
        }
        return v;
    }

    // Word-wrap the runs into [x, x + w); returns the height used. align: 0/l, c, r.
    int flow(const std::vector<Run> &runs, int base, int x, int w, int y0, Color color, char align = 0)
    {
        w = std::max(w, 8);
        std::vector<Token> toks = tokens(runs, base, w);
        // a word wider than the line is cut into pieces that fit
        std::vector<Token> fit;
        for (auto &t : toks) {
            if (t.space || t.brk || t.image >= 0 || t.w <= w || t.text.size() < 2) { fit.push_back(t); continue; }
            std::string rest = t.text;
            while (!rest.empty()) {
                size_t n = rest.size();
                while (n > 1 && m.textWidth(t.font, rest.substr(0, n)) > w) n--;
                Token p = t;
                p.text = rest.substr(0, n);
                p.w = m.textWidth(t.font, p.text);
                fit.push_back(p);
                rest = rest.substr(n);
            }
        }
        int y = y0;
        size_t i = 0;
        bool any = false;
        while (i < fit.size()) {
            // one line: tokens [i, j)
            size_t j = i;
            int lw = 0;
            while (j < fit.size() && fit[j].space && j == i) j++, i++;   // no spaces at a line's start
            if (i >= fit.size()) break;
            while (j < fit.size()) {
                const Token &t = fit[j];
                if (t.brk) break;
                if (!t.space && lw + t.w > w && j > i) break;
                lw += t.w;
                j++;
            }
            size_t end = j;
            while (end > i && fit[end - 1].space) { lw -= fit[end - 1].w; end--; }
            int asc = 0, desc = 0;
            for (size_t k = i; k < end; k++) {
                const Token &t = fit[k];
                if (t.image >= 0) asc = std::max(asc, t.h);
                else {
                    asc = std::max(asc, m.baseline(t.font));
                    desc = std::max(desc, m.lineHeight(t.font) - m.baseline(t.font));
                }
            }
            if (end == i) { asc = m.baseline(base); desc = m.lineHeight(base) - asc; }
            int lx = x + (align == 'r' ? w - lw : align == 'c' ? (w - lw) / 2 : 0);
            for (size_t k = i; k < end; k++) {
                const Token &t = fit[k];
                Item it;
                it.x = lx;
                it.w = t.w;
                it.link = t.link;
                if (t.image >= 0) {
                    it.kind = t.alt ? ItemKind::AltBox : ItemKind::Picture;
                    it.image = t.image;
                    it.y = y + asc - t.h;
                    it.h = t.h;
                    it.text = t.text;
                    it.font = t.font;
                    push(it);
                } else {
                    it.kind = ItemKind::Text;
                    it.font = t.font;
                    it.style = t.style;
                    it.color = t.link >= 0 ? CLink : color;
                    it.y = y + asc - m.baseline(t.font);
                    it.h = m.lineHeight(t.font);
                    it.text = t.text;
                    // one item for the words and spaces of the same look in a row
                    Item *prev = out.items.empty() ? nullptr : &out.items.back();
                    if (prev && prev->kind == ItemKind::Text && k > i && prev->font == it.font && prev->style == it.style &&
                        prev->link == it.link && prev->color == it.color && prev->y == it.y && prev->x + prev->w == it.x &&
                        !(it.style & CodeSpan)) {
                        prev->text += it.text;
                        prev->w += it.w;
                        out.width = std::max(out.width, prev->x + prev->w);
                    } else {
                        if (t.style & CodeSpan) {
                            Item bg;
                            bg.kind = ItemKind::CodeBg;
                            bg.x = it.x - 1;
                            bg.y = it.y;
                            bg.w = it.w + 2;
                            bg.h = it.h;
                            push(bg);
                        }
                        push(it);
                    }
                }
                lx += t.w;
            }
            y += asc + desc;
            any = true;
            i = j;
            if (i < fit.size() && fit[i].brk) {
                i++;
                if (i >= fit.size()) y += m.lineHeight(base);   // a break at the end: an empty line
            }
        }
        if (!any) return 0;
        return y - y0;
    }

    // a block's height, laid out at (x, y) w wide
    int block(const Block &b, int x, int w, int y0, int listDepth, Color color)
    {
        int y = y0;
        switch (b.type) {
        case BlockType::Doc:
        case BlockType::THead:
        case BlockType::TBody:
        case BlockType::TR:
        case BlockType::TH:
        case BlockType::TD:
            y += children(b, x, w, y, listDepth, color);
            break;
        case BlockType::Para:
            y += flow(b.runs, FBody, x, w, y, color);
            y += gap;
            break;
        case BlockType::Heading: {
            int font = FH1 + std::max(0, std::min(5, b.level - 1));
            if (y > 0) y += gap / 2;
            y += flow(b.runs, font, x, w, y, CHeading);
            if (b.level <= 2) {
                Item r;
                r.kind = ItemKind::Rule;
                r.x = x;
                r.y = y + 2;
                r.w = w;
                r.h = 1;
                r.color = CMuted;
                push(r);
                y += 4;
            }
            y += gap;
            break;
        }
        case BlockType::Html: {
            // its text, pictures and links flow like a paragraph; its tables are tables
            bool any = false;
            for (auto &r : b.runs) any = any || r.image >= 0 || r.text.find_first_not_of(" \n") != std::string::npos;
            if (any) y += flow(b.runs, FBody, x, w, y, color) + gap;
            if (!b.children.empty()) y += children(b, x, w, y, listDepth, color);
            break;
        }
        case BlockType::Code: {
            std::string all = plainText(b);
            std::vector<std::string> lines;
            size_t p = 0;
            while (p < all.size()) {
                size_t e = all.find('\n', p);
                std::string ln = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
                std::string ex;   // tabs to 4 columns
                for (char c : ln) {
                    if (c == '\t') do ex += ' '; while (ex.size() % 4);
                    else ex += c;
                }
                lines.push_back(ex);
                if (e == std::string::npos) break;
                p = e + 1;
            }
            int pad = 4, mlh = m.lineHeight(FMono), maxw = 0;
            for (auto &l : lines) maxw = std::max(maxw, m.textWidth(FMono, l));
            Item bg;
            bg.kind = ItemKind::CodeBg;
            bg.x = x;
            bg.y = y;
            bg.w = std::max(w, maxw + 2 * pad);
            bg.h = (int)lines.size() * mlh + 2 * pad;
            push(bg);
            int ly = y + pad;
            for (auto &l : lines) {
                if (!l.empty()) {
                    Item t;
                    t.kind = ItemKind::Text;
                    t.font = FMono;
                    t.x = x + pad;
                    t.y = ly;
                    t.w = m.textWidth(FMono, l);
                    t.h = mlh;
                    t.text = l;
                    t.color = CText;
                    push(t);
                }
                ly += mlh;
            }
            y += bg.h + gap;
            break;
        }
        case BlockType::HR: {
            Item r;
            r.kind = ItemKind::Rule;
            r.x = x;
            r.y = y + gap / 2;
            r.w = w;
            r.h = 2;
            r.color = CMuted;
            push(r);
            y += gap + 2;
            break;
        }
        case BlockType::Quote: {
            int ind = std::max(10, m.textWidth(FBody, "MM"));
            size_t bar = out.items.size();
            Item q;
            q.kind = ItemKind::QuoteBar;
            q.x = x + 2;
            q.y = y;
            q.w = 3;
            q.color = CMuted;
            push(q);
            int h = children(b, x + ind, w - ind, y, listDepth, CMuted);
            out.items[bar].h = std::max(0, h - gap);
            y += std::max(h, lh);
            break;
        }
        case BlockType::UL:
        case BlockType::OL: {
            unsigned n = b.start;
            int ind = m.textWidth(FBody, b.type == BlockType::OL ? "000. " : "MM");
            for (auto &c : b.children) {
                std::string mark;
                if (b.type == BlockType::OL) mark = std::to_string(n++) + ".";
                else mark = listDepth % 3 == 0 ? "\xB7" : listDepth % 3 == 1 ? "-" : "\xB0";
                if (c->task) mark = c->checked ? "[x]" : "[ ]";
                int iw = std::max(ind, m.textWidth(FBody, mark + " "));
                Item t;
                t.kind = ItemKind::Text;
                t.font = mark == "\xB7" ? FBold : FBody;
                t.text = mark;
                t.w = m.textWidth(t.font, mark);
                t.h = m.lineHeight(t.font);
                t.x = x + iw - t.w - m.textWidth(FBody, " ");
                t.y = y;
                t.color = color;
                push(t);
                int h = item(*c, x + iw, w - iw, y, listDepth + 1, color, b.tight);
                y += std::max(h, lh);
            }
            if (!b.tight || listDepth == 0) y += gap;
            break;
        }
        case BlockType::LI:
            y += item(b, x, w, y, listDepth, color, true);
            break;
        case BlockType::Table:
            y += table(b, x, w, y, color);
            y += gap;
            break;
        }
        return y - y0;
    }

    int item(const Block &li, int x, int w, int y0, int listDepth, Color color, bool tight)
    {
        int y = y0;
        if (!li.runs.empty()) y += flow(li.runs, FBody, x, w, y, color);   // a tight item's text
        for (auto &c : li.children) {
            int h = block(*c, x, w, y, c->type == BlockType::UL || c->type == BlockType::OL ? listDepth : listDepth, color);
            // in a tight list a paragraph takes no gap after it
            if (tight && c->type == BlockType::Para) h -= gap;
            y += h;
        }
        if (!tight) y += 0;
        return y - y0;
    }

    int children(const Block &b, int x, int w, int y0, int listDepth, Color color)
    {
        int y = y0;
        for (auto &c : b.children) y += block(*c, x, w, y, listDepth, color);
        return y - y0;
    }

    // a table: columns as wide as their widest cell (shrunk to fit, cells then wrap)
    int table(const Block &t, int x, int w, int y0, Color color)
    {
        std::vector<const Block *> rows;
        std::vector<bool> head;
        for (auto &sec : t.children)
            for (auto &r : sec->children)
                if (r->type == BlockType::TR) {
                    rows.push_back(r.get());
                    head.push_back(sec->type == BlockType::THead);
                }
        size_t cols = 0;
        for (auto *r : rows) cols = std::max(cols, r->children.size());
        if (!cols) return 0;
        cols = std::min<size_t>(cols, 64);
        int pad = 4;
        std::vector<int> nat(cols, 0);
        for (size_t ri = 0; ri < rows.size(); ri++)
            for (size_t c = 0; c < rows[ri]->children.size() && c < cols; c++) {
                int base = head[ri] ? FBold : FBody, cw = 0;
                for (auto &tk : tokens(rows[ri]->children[c]->runs, base, w)) cw += tk.brk ? 0 : tk.w;
                nat[c] = std::max(nat[c], cw + 2 * pad);
            }
        int total = 0;
        for (int n : nat) total += n;
        int minCol = m.textWidth(FBody, "MMM") + 2 * pad;
        std::vector<int> cw = nat;
        if (total > w) {
            // shrink the wide columns first: narrow ones keep their width
            int avail = std::max(w, (int)cols * minCol);
            for (int pass = 0; pass < 8; pass++) {
                int sum = 0, wide = 0;
                for (size_t c = 0; c < cols; c++) sum += cw[c];
                if (sum <= avail) break;
                int share = avail / (int)cols;
                for (size_t c = 0; c < cols; c++) if (cw[c] > share) wide += cw[c];
                int over = sum - avail;
                for (size_t c = 0; c < cols && wide > 0; c++)
                    if (cw[c] > share) cw[c] = std::max(minCol, cw[c] - (int)((long long)over * cw[c] / wide) - 1);
            }
        }
        int tw = 0;
        for (int v : cw) tw += v;
        int y = y0;
        std::vector<int> rowY;
        for (size_t ri = 0; ri < rows.size(); ri++) {
            rowY.push_back(y);
            size_t bgAt = out.items.size();
            if (head[ri]) {
                Item bg;
                bg.kind = ItemKind::TableHead;
                bg.x = x;
                bg.y = y;
                bg.w = tw;
                push(bg);
            }
            int rh = 0, cx = x;
            for (size_t c = 0; c < cols; c++) {
                if (c < rows[ri]->children.size()) {
                    const Block &cell = *rows[ri]->children[c];
                    int h = flow(cell.runs, head[ri] ? FBold : FBody, cx + pad, cw[c] - 2 * pad, y + pad, color, cell.align);
                    rh = std::max(rh, h);
                }
                cx += cw[c];
            }
            rh = std::max(rh, lh) + 2 * pad;
            if (head[ri]) out.items[bgAt].h = rh;
            y += rh;
        }
        rowY.push_back(y);
        // the lines
        for (int ry : rowY) {
            Item r;
            r.kind = ItemKind::Rule;
            r.x = x;
            r.y = ry;
            r.w = tw;
            r.h = 1;
            r.color = CMuted;
            push(r);
        }
        int cx = x;
        for (size_t c = 0; c <= cols; c++) {
            Item r;
            r.kind = ItemKind::Rule;
            r.x = cx;
            r.y = y0;
            r.w = 1;
            r.h = y - y0;
            r.color = CMuted;
            push(r);
            if (c < cols) cx += cw[c];
        }
        return y - y0 + 1;
    }
};

} // namespace

Layout layout(const Doc &doc, int width, Metrics &m)
{
    Lay l(doc, m);
    width = std::max(width, 40);
    int margin = 6;
    int h = l.block(doc.root, margin, width - 2 * margin, margin, 0, CText);
    l.out.height = h + 2 * margin;
    l.out.width = std::max(l.out.width + margin, width);
    return l.out;
}

int linkAt(const Layout &l, int x, int y)
{
    for (auto &it : l.items)
        if (it.link >= 0 && x >= it.x && x < it.x + it.w && y >= it.y && y < it.y + it.h) return it.link;
    return -1;
}

} // namespace md
} // namespace af
