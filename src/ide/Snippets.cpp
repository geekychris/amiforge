// Snippet parsing and expansion.
#include "Snippets.h"

#include <algorithm>
#include <map>
#include <cctype>

namespace af {

// The built-in snippets: everyday C/C++ shapes and the AmigaOS 4 boilerplate that is tedious
// to type (opening a library and its interface, a window with an event loop, timer.device,
// sending an ARexx command).
static const std::vector<Snippet> builtins = {
    { "main", "a main() with the usual includes",
      "#include <proto/exec.h>\n"
      "#include <proto/dos.h>\n"
      "\n"
      "int main(int argc, char **argv)\n"
      "{\n"
      "\t${0}\n"
      "\treturn 0;\n"
      "}\n" },
    { "openlib", "open a library and get its interface",
      "struct Library *${1:Foo}Base = IExec->OpenLibrary(\"${2:foo}.library\", ${3:53});\n"
      "struct ${1}IFace *I${1} = ${1}Base ? (struct ${1}IFace *)IExec->GetInterface(${1}Base, \"main\", 1, NULL) : NULL;\n"
      "if (!I${1}) {\n"
      "\tIDOS->Printf(\"Can't open ${2}.library\\n\");\n"
      "\t${0}\n"
      "}\n"
      "IExec->DropInterface((struct Interface *)I${1});\n"
      "IExec->CloseLibrary(${1}Base);" },
    { "window", "open a window and handle its messages",
      "struct Window *${1:win} = IIntuition->OpenWindowTags(NULL,\n"
      "\tWA_Title, \"${2:My Window}\",\n"
      "\tWA_InnerWidth, ${3:320}, WA_InnerHeight, ${4:200},\n"
      "\tWA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,\n"
      "\tWA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY,\n"
      "\tTAG_END);\n"
      "if (${1}) {\n"
      "\tBOOL done = FALSE;\n"
      "\twhile (!done) {\n"
      "\t\tIExec->WaitPort(${1}->UserPort);\n"
      "\t\tstruct IntuiMessage *msg;\n"
      "\t\twhile ((msg = (struct IntuiMessage *)IExec->GetMsg(${1}->UserPort))) {\n"
      "\t\t\tuint32 cls = msg->Class;\n"
      "\t\t\tuint16 code = msg->Code;\n"
      "\t\t\tIExec->ReplyMsg((struct Message *)msg);\n"
      "\t\t\tif (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_VANILLAKEY && code == 27)) done = TRUE;\n"
      "\t\t\t${0}\n"
      "\t\t}\n"
      "\t}\n"
      "\tIIntuition->CloseWindow(${1});\n"
      "}" },
    { "timer", "wait with timer.device",
      "struct MsgPort *${1:timerPort} = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);\n"
      "struct TimeRequest *${2:timerReq} = (struct TimeRequest *)IExec->AllocSysObjectTags(ASOT_IOREQUEST,\n"
      "\tASOIOR_Size, sizeof(struct TimeRequest), ASOIOR_ReplyPort, ${1}, TAG_END);\n"
      "if (${2} && !IExec->OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)${2}, 0)) {\n"
      "\t${2}->Request.io_Command = TR_ADDREQUEST;\n"
      "\t${2}->Time.Seconds = ${3:0};\n"
      "\t${2}->Time.Microseconds = ${4:20000};\n"
      "\tIExec->DoIO((struct IORequest *)${2});\n"
      "\t${0}\n"
      "\tIExec->CloseDevice((struct IORequest *)${2});\n"
      "}\n"
      "IExec->FreeSysObject(ASOT_IOREQUEST, ${2});\n"
      "IExec->FreeSysObject(ASOT_PORT, ${1});" },
    { "rexx", "send a command to an ARexx port",
      "struct MsgPort *${1:replyPort} = (struct MsgPort *)IExec->AllocSysObjectTags(ASOT_PORT, TAG_END);\n"
      "struct RexxMsg *rm = IRexxSys->CreateRexxMsg(${1}, NULL, NULL);\n"
      "if (rm) {\n"
      "\tconst char *cmd = \"${2:COMMAND}\";\n"
      "\trm->rm_Args[0] = (STRPTR)IRexxSys->CreateArgstring(cmd, strlen(cmd));\n"
      "\trm->rm_Action = RXCOMM | RXFF_RESULT;\n"
      "\tIExec->Forbid();\n"
      "\tstruct MsgPort *port = IExec->FindPort(\"${3:PORTNAME}\");\n"
      "\tif (port) IExec->PutMsg(port, (struct Message *)rm);\n"
      "\tIExec->Permit();\n"
      "\tif (port) {\n"
      "\t\tIExec->WaitPort(${1});\n"
      "\t\tIExec->GetMsg(${1});\n"
      "\t\tif (rm->rm_Result1 == 0 && rm->rm_Result2) {\n"
      "\t\t\t${0}\n"
      "\t\t\tIRexxSys->DeleteArgstring((STRPTR)rm->rm_Result2);\n"
      "\t\t}\n"
      "\t}\n"
      "\tIRexxSys->DeleteArgstring(rm->rm_Args[0]);\n"
      "\tIRexxSys->DeleteRexxMsg(rm);\n"
      "}\n"
      "IExec->FreeSysObject(ASOT_PORT, ${1});" },
    { "tags", "a tag list",
      "struct TagItem ${1:tags}[] = {\n"
      "\t{ ${2:WA_Title}, (uint32)${3:\"Title\"} },\n"
      "\t{ TAG_END, 0 }\n"
      "};${0}" },
    { "for", "a counting loop",
      "for (${1:int} ${2:i} = 0; ${2} < ${3:count}; ${2}++) {\n"
      "\t${0}\n"
      "}" },
    { "while", "a while loop",
      "while (${1:condition}) {\n"
      "\t${0}\n"
      "}" },
    { "if", "an if statement",
      "if (${1:condition}) {\n"
      "\t${0}\n"
      "}" },
    { "switch", "a switch",
      "switch (${1:value}) {\n"
      "case ${2:0}:\n"
      "\t${0}\n"
      "\tbreak;\n"
      "default:\n"
      "\tbreak;\n"
      "}" },
    { "struct", "a struct",
      "struct ${1:Name} {\n"
      "\t${2:int field};\n"
      "};${0}" },
    { "class", "a C++ class",
      "class ${1:Name} {\n"
      "public:\n"
      "\t${1}();\n"
      "\t~${1}();\n"
      "\t${0}\n"
      "private:\n"
      "};" },
};

const std::vector<Snippet> &builtinSnippets() { return builtins; }

std::vector<Snippet> parseSnippets(const std::string &text)
{
    std::vector<Snippet> out;
    Snippet *cur = nullptr;
    bool firstBodyLine = true;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 8, "snippet ") == 0 || line == "snippet") {
            std::string rest = line.size() > 8 ? line.substr(8) : "";
            size_t a = rest.find_first_not_of(" \t");
            rest = a == std::string::npos ? "" : rest.substr(a);
            size_t sp = rest.find_first_of(" \t");
            Snippet s;
            s.name = rest.substr(0, sp);
            if (sp != std::string::npos) {
                size_t d = rest.find_first_not_of(" \t", sp);
                if (d != std::string::npos) s.description = rest.substr(d);
            }
            if (s.name.empty()) { cur = nullptr; continue; }
            out.push_back(s);
            cur = &out.back();
            firstBodyLine = true;
        } else if (cur && !line.empty() && line[0] == '\t') {
            if (!firstBodyLine) cur->body += '\n';
            cur->body += line.substr(1);
            firstBodyLine = false;
        } else if (cur && line.empty()) {
            // a blank line inside a body only counts if more body follows
            size_t save = pos;
            bool more = false;
            while (save < text.size()) {
                size_t n2 = text.find('\n', save);
                std::string l2 = text.substr(save, n2 == std::string::npos ? std::string::npos : n2 - save);
                if (!l2.empty() && l2 != "\r") { more = l2[0] == '\t'; break; }
                if (n2 == std::string::npos) break;
                save = n2 + 1;
            }
            if (more && !firstBodyLine) cur->body += '\n';
        } else {
            cur = nullptr;   // a comment or anything else ends the body
        }
    }
    return out;
}

std::vector<Snippet> mergeSnippets(const std::vector<Snippet> &base, const std::vector<Snippet> &user)
{
    std::vector<Snippet> out = base;
    for (auto &u : user) {
        bool replaced = false;
        for (auto &b : out)
            if (b.name == u.name) { b = u; replaced = true; break; }
        if (!replaced) out.push_back(u);
    }
    return out;
}

const Snippet *findSnippet(const std::vector<Snippet> &list, const std::string &name)
{
    for (auto &s : list)
        if (s.name == name) return &s;
    return nullptr;
}

SnippetExpansion expandSnippet(const std::string &body, const std::string &lineIndent, const std::string &indentUnit)
{
    SnippetExpansion ex;
    std::map<int, std::string> defaults;   // the first use of each number sets its text
    std::map<int, bool> seen;
    bool atLineStart = false;
    size_t i = 0;
    while (i < body.size()) {
        char c = body[i];
        if (c == '\n') {
            ex.text += '\n';
            ex.text += lineIndent;
            atLineStart = true;
            i++;
            continue;
        }
        if (c == '\t' && atLineStart) {
            ex.text += indentUnit;
            i++;
            continue;
        }
        atLineStart = false;
        if (c == '\\' && i + 1 < body.size() && (body[i + 1] == '$' || body[i + 1] == '}' || body[i + 1] == '\\')) {
            ex.text += body[i + 1];
            i += 2;
            continue;
        }
        if (c == '$' && i + 1 < body.size()) {
            int number = -1;
            std::string def;
            bool hasDefault = false;
            size_t j = i + 1;
            if (isdigit((unsigned char)body[j])) {
                number = 0;
                while (j < body.size() && isdigit((unsigned char)body[j])) number = number * 10 + (body[j++] - '0');
            } else if (body[j] == '{' && j + 1 < body.size() && isdigit((unsigned char)body[j + 1])) {
                j++;
                number = 0;
                while (j < body.size() && isdigit((unsigned char)body[j])) number = number * 10 + (body[j++] - '0');
                if (j < body.size() && body[j] == ':') {
                    hasDefault = true;
                    j++;
                    while (j < body.size() && body[j] != '}') {
                        if (body[j] == '\\' && j + 1 < body.size()) j++;
                        def += body[j++];
                    }
                }
                if (j < body.size() && body[j] == '}') j++;
                else number = -1;   // unterminated: literal text
            }
            if (number >= 0) {
                bool mirror = seen[number];
                if (!mirror) {
                    seen[number] = true;
                    defaults[number] = hasDefault ? def : "";
                }
                const std::string &t = defaults[number];
                SnippetField f;
                f.number = number;
                f.start = ex.text.size();
                ex.text += t;
                f.end = ex.text.size();
                f.mirror = mirror;
                ex.fields.push_back(f);
                i = j;
                continue;
            }
        }
        ex.text += c;
        i++;
    }
    return ex;
}

std::vector<size_t> SnippetExpansion::tabOrder() const
{
    std::vector<size_t> order;
    for (size_t k = 0; k < fields.size(); k++)
        if (!fields[k].mirror && fields[k].number > 0) order.push_back(k);
    std::sort(order.begin(), order.end(), [this](size_t a, size_t b) { return fields[a].number < fields[b].number; });
    for (size_t k = 0; k < fields.size(); k++)
        if (!fields[k].mirror && fields[k].number == 0) { order.push_back(k); break; }
    return order;
}

void adjustSnippetFields(std::vector<SnippetField> &fields, size_t pos, size_t length, bool inserted)
{
    for (auto &f : fields) {
        if (inserted) {
            if (pos < f.start) { f.start += length; f.end += length; }
            else if (pos <= f.end) f.end += length;
        } else {
            size_t dEnd = pos + length;
            auto clamp = [&](size_t p) { return p <= pos ? p : p >= dEnd ? p - length : pos; };
            f.start = clamp(f.start);
            f.end = clamp(f.end);
        }
    }
}

} // namespace af
