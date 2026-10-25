// Project templates: the template.def format and ${NAME}-style substitution. Portable C++
// (no OS calls) so the host unit tests build it as is; TemplatesUI.cpp does the file work.
//
// A template is a drawer in PROGDIR:Templates/ with a template.def and the files it lists:
//
//   [template]
//   name=Console C
//   description=A command-line program in C
//   order=10
//   [project]                 settings for the new .afp (any key the .afp has)
//   output=${IDENT}
//   [files]
//   file=main.c -> ${LOWER}.c   copied (with substitution) and added to the project
//   extra=README -> ReadMe      copied, not added to the project
//   generated=include/x.h       made by a command below; added to the project
//   open=${LOWER}.c             opened in the editor afterwards
//   [create]
//   command=SDK:C/idltool -i ${LOWER}.xml   run in the new project's drawer after copying
//   gui=${LOWER}.afg            generate the GUI code for this designer file and add it
//
// Substitutions: ${NAME} (as typed), ${IDENT} (a C identifier), ${UPPER}, ${LOWER},
// ${AUTHOR}, ${DATE} (d.m.yyyy), ${YEAR}; any of them with :upper, :lower or :ident
// (${NAME:upper}). $${ stands for a literal ${.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace af {
namespace tmpl {

using Vars = std::map<std::string, std::string>;

// Text with ${...} replaced; names that aren't in vars are left as they are and listed in unknown.
std::string substitute(const std::string &text, const Vars &vars, std::vector<std::string> *unknown = nullptr);

// The standard variables for a project called `name`.
Vars makeVars(const std::string &name, const std::string &author, int day, int month, int year);

// Whether `name` can be a project name (it becomes file and C names); why not in `why`.
bool validName(const std::string &name, std::string &why);

struct FileSpec {
    enum Kind { Project, Extra, Generated } kind = Project;
    std::string src, dest;   // src in the template's drawer; dest relative to the project ("a/b.c")
};

struct Def {
    std::string dir;                    // the template's drawer (filled in by the caller)
    std::string name, description;
    int order = 100;
    std::vector<std::pair<std::string, std::string>> project;   // .afp settings
    std::vector<FileSpec> files;
    std::vector<std::string> open;
    std::vector<std::string> commands;
    std::vector<std::string> guis;
};

bool parseDef(const std::string &text, Def &out, std::string &err);

// The definition with every destination, setting, command and file name substituted.
Def instantiate(const Def &d, const Vars &vars);

// What creating a project from a template does, worked out before anything is written.
struct Plan {
    struct Output { std::string path, content; };    // path relative to the project's drawer
    std::vector<std::string> dirs;                    // drawers to make first, in order
    std::vector<Output> files;
    std::vector<std::string> projectFiles;            // for the .afp, in order
    std::vector<std::pair<std::string, std::string>> project;
    std::vector<std::string> commands, guis, open;
    std::vector<std::string> errors;                  // e.g. a missing template file
};
// read(src, out) reads a file from the template's drawer.
Plan plan(const Def &instantiated, const Vars &vars,
          const std::function<bool(const std::string &src, std::string &out)> &read);

// The drawers a destination path needs ("a/b/c.c" -> {"a", "a/b"}), in creation order.
std::vector<std::string> parentDirs(const std::string &dest);

} // namespace tmpl
} // namespace af
