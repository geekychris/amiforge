// Project templates: substitution and template.def parsing.
#include "aftest.h"
#include "TemplateEngine.h"

using namespace af::tmpl;

TEST(substitute_basic)
{
    Vars v = makeVars("My-App", "Jane <j@x.org>", 5, 3, 2026);
    CHECK_EQ(v["IDENT"], std::string("My_App"));
    CHECK_EQ(v["UPPER"], std::string("MY_APP"));
    CHECK_EQ(v["LOWER"], std::string("my_app"));
    CHECK_EQ(v["DATE"], std::string("5.3.2026"));
    CHECK_EQ(substitute("int ${LOWER}_main(void); /* ${NAME} by ${AUTHOR}, ${YEAR} */", v),
             std::string("int my_app_main(void); /* My-App by Jane <j@x.org>, 2026 */"));
    CHECK_EQ(substitute("${NAME:upper} ${NAME:lower} ${NAME:ident}", v), std::string("MY-APP my-app My_App"));
}

TEST(substitute_unknown_and_escapes)
{
    Vars v = makeVars("x", "", 1, 1, 2026);
    std::vector<std::string> unknown;
    CHECK_EQ(substitute("${NOPE} ${NAME:shout} $${NAME} ${NAME", v, &unknown),
             std::string("${NOPE} ${NAME:shout} ${NAME} ${NAME"));
    CHECK_EQ((int)unknown.size(), 2);
    CHECK_EQ(v["AUTHOR"], std::string("Anonymous"));
    CHECK_EQ(substitute("no vars here $ {}", v), std::string("no vars here $ {}"));
}

TEST(valid_names)
{
    std::string why;
    CHECK(validName("Hello", why));
    CHECK(validName("my_tool-2", why));
    CHECK(!validName("", why));
    CHECK(!validName("2fast", why));
    CHECK(!validName("has space", why));
    CHECK(!validName("dot.ted", why));
    CHECK(!validName("abcdefghijklmnopqrstuvwxyz", why));
}

static const char *DEF =
    "# a template\n"
    "[template]\n"
    "name=Shared Library\n"
    "description=A library with an IDL description\n"
    "order=70\n"
    "[project]\n"
    "output=${LOWER}.library\n"
    "cflags=-O0 -gstabs -Wall -Iinclude\n"
    "[files]\n"
    "file=init.c\n"
    "file=impl.c -> ${LOWER}_impl.c\n"
    "extra=lib.xml -> ${LOWER}.xml\n"
    "generated=${LOWER}_vectors.c\n"
    "extra=test.c -> test/${LOWER}test.c\n"
    "open=${LOWER}_impl.c\n"
    "[create]\n"
    "command=SDK:C/idltool -i -p -n -c ${LOWER}.xml\n"
    "gui=${LOWER}.afg\n";

TEST(parse_def)
{
    Def d;
    std::string err;
    CHECK(parseDef(DEF, d, err));
    CHECK_EQ(d.name, std::string("Shared Library"));
    CHECK_EQ(d.order, 70);
    CHECK_EQ((int)d.project.size(), 2);
    CHECK_EQ((int)d.files.size(), 5);
    CHECK(d.files[0].kind == FileSpec::Project && d.files[0].src == "init.c" && d.files[0].dest == "init.c");
    CHECK(d.files[2].kind == FileSpec::Extra);
    CHECK(d.files[3].kind == FileSpec::Generated);
    CHECK_EQ((int)d.commands.size(), 1);
    CHECK_EQ((int)d.guis.size(), 1);

    Def i = instantiate(d, makeVars("Maths", "", 1, 1, 2026));
    CHECK_EQ(i.project[0].second, std::string("maths.library"));
    CHECK_EQ(i.files[1].dest, std::string("maths_impl.c"));
    CHECK_EQ(i.files[1].src, std::string("impl.c"));          // sources aren't substituted
    CHECK_EQ(i.files[4].dest, std::string("test/mathstest.c"));
    CHECK_EQ(i.commands[0], std::string("SDK:C/idltool -i -p -n -c maths.xml"));
    CHECK_EQ(i.open[0], std::string("maths_impl.c"));
    CHECK_EQ(i.guis[0], std::string("maths.afg"));
}

TEST(parse_def_errors)
{
    Def d;
    std::string err;
    CHECK(!parseDef("[template]\ndescription=no name\n", d, err));
    CHECK(!parseDef("[template]\nname=x\n[bogus]\n", d, err));
    CHECK(!parseDef("[template]\nname=x\n[files]\nfile=a -> /abs\n", d, err));
    CHECK(!parseDef("[template]\nname=x\n[files]\nfile=a -> DH0:x\n", d, err));
    CHECK(!parseDef("name=x\n", d, err));
    CHECK(!parseDef("[template]\nname=x\n[files]\ncopy=a\n", d, err));
}

TEST(parent_dirs)
{
    auto p = parentDirs("a/b/c.c");
    CHECK_EQ((int)p.size(), 2);
    CHECK_EQ(p[0], std::string("a"));
    CHECK_EQ(p[1], std::string("a/b"));
    CHECK(parentDirs("c.c").empty());
}

TEST(plan_from_template)
{
    Def d;
    std::string err;
    CHECK(parseDef(DEF, d, err));
    Vars v = makeVars("Maths", "", 1, 1, 2026);
    Def i = instantiate(d, v);
    std::map<std::string, std::string> files = {
        { "init.c", "/* ${NAME} init */" }, { "impl.c", "int ${LOWER}_x;" }, { "lib.xml", "<library name=\"${LOWER}\"/>" },
        { "test.c", "${BOGUS}" },
    };
    Plan p = plan(i, v, [&](const std::string &src, std::string &out) {
        auto it = files.find(src);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    });
    CHECK_EQ((int)p.files.size(), 4);
    CHECK_EQ(p.files[0].path, std::string("init.c"));
    CHECK_EQ(p.files[0].content, std::string("/* Maths init */"));
    CHECK_EQ(p.files[1].path, std::string("maths_impl.c"));
    CHECK_EQ(p.files[1].content, std::string("int maths_x;"));
    CHECK_EQ(p.files[3].path, std::string("test/mathstest.c"));
    // project files: file= and generated=, not extra=
    CHECK_EQ((int)p.projectFiles.size(), 3);
    CHECK_EQ(p.projectFiles[2], std::string("maths_vectors.c"));
    CHECK_EQ((int)p.dirs.size(), 1);
    CHECK_EQ(p.dirs[0], std::string("test"));
    // the unknown ${BOGUS} is reported
    CHECK_EQ((int)p.errors.size(), 1);
    files.erase("init.c");
    Plan q = plan(i, v, [&](const std::string &src, std::string &out) {
        auto it = files.find(src);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    });
    CHECK_EQ((int)q.errors.size(), 2);
}

// Every template shipped in templates/ parses, and plans without missing files or unknown
// substitutions. (The runner starts in tests/host.)
#include <dirent.h>
#include <fstream>
#include <sstream>

static bool slurp(const std::string &p, std::string &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

TEST(shipped_templates_are_complete)
{
    std::string root = "../../templates";
    DIR *d = opendir(root.c_str());
    CHECK(d != nullptr);
    if (!d) return;
    int n = 0;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string dir = root + "/" + e->d_name, text, err;
        if (!slurp(dir + "/template.def", text)) continue;
        Def def;
        bool ok = parseDef(text, def, err);
        if (!ok) aftest::fail(__FILE__, __LINE__, std::string(e->d_name) + ": " + err);
        Vars v = makeVars("Check", "A <a@b.c>", 1, 2, 2026);
        Plan p = plan(instantiate(def, v), v, [&](const std::string &src, std::string &out) { return slurp(dir + "/" + src, out); });
        for (auto &x : p.errors) aftest::fail(__FILE__, __LINE__, std::string(e->d_name) + ": " + x);
        CHECK(!p.projectFiles.empty());
        CHECK(!p.open.empty());
        n++;
    }
    closedir(d);
    CHECK_EQ(n, 9);
}
