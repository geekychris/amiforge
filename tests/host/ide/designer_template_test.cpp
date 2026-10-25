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
