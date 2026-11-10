/* boingstub - a stand-in for Boing BASIC's `boing` command, for testing AmiForge's Boing BASIC
 * projects before the real compiler is there. It answers like boing does:
 *
 *   boingstub check  prog.bbp|prog.bas         a line containing STUB_ERROR in a .bas gives
 *                                              "file:line:col: error: ..." (rc 10)
 *   boingstub build  prog.bbp -o out [--debug] writes `out` (an AmigaDOS script) and says so
 *   boingstub run    [--error-file F] prog.bbp [args]
 *                                              prints the files; a line with STUB_RUNTIME gives
 *                                              "file:line: runtime error 9: Subscript out of
 *                                              range" on the console and in F (rc 20); a line
 *                                              with STUB_WAIT waits for Ctrl-C
 *   boingstub symbols prog.bas                 kind<TAB>name<TAB>line for Sub/Function lines
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <proto/exec.h>
#include <proto/dos.h>

static char files[32][256];
static int nfiles;

static void dirOf(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/'), *c = strrchr(path, ':');
    const char *e = s > c ? s + 1 : c ? c + 1 : path;
    size_t len = (size_t)(e - path);
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = 0;
}

static void add(const char *f)
{
    if (nfiles < 32) {
        strncpy(files[nfiles], f, 255);
        files[nfiles][255] = 0;
        nfiles++;
    }
}

/* the .bas files of a project: Module=, Class=, and Form=x.afg -> x.bas */
static void collect(const char *target)
{
    size_t n = strlen(target);
    if (n < 4 || strcasecmp(target + n - 4, ".bbp") != 0) {
        add(target);
        return;
    }
    char dir[256], line[512];
    dirOf(target, dir, sizeof dir);
    FILE *f = fopen(target, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1, *e = v;
        while (*e && *e != '\n' && *e != '\r' && *e != ' ' && *e != '\'') e++;
        *e = 0;
        if (strcasecmp(line, "Module") && strcasecmp(line, "Class") && strcasecmp(line, "Form")) continue;
        char full[512];
        snprintf(full, sizeof full, "%s%s", dir, v);
        size_t fl = strlen(full);
        if (!strcasecmp(line, "Form") && fl > 4) strcpy(full + fl - 4, ".bas");
        add(full);
    }
    fclose(f);
}

/* the first line (1-based) of a file that contains `word`, 0 if none; its column in *col */
static int find(const char *file, const char *word, int *col)
{
    FILE *f = fopen(file, "r");
    char line[1024];
    int n = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *p = strstr(line, word);
        if (p) {
            if (col) *col = (int)(p - line) + 1;
            fclose(f);
            return n;
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: boingstub check|build|run|symbols prog.bbp ...\n");
        return 20;
    }
    const char *cmd = argv[1];
    const char *errFile = NULL;
    int a = 2;
    if (!strcmp(cmd, "run") && argc > 4 && !strcmp(argv[2], "--error-file")) {
        errFile = argv[3];
        a = 4;
    }
    const char *target = argv[a];
    collect(target);
    if (!strcmp(cmd, "check")) {
        int errors = 0;
        for (int i = 0; i < nfiles; i++) {
            int col = 0, l = find(files[i], "STUB_ERROR", &col);
            if (l) {
                printf("%s:%d:%d: error: expected an expression (stub)\n", files[i], l, col);
                errors++;
            }
        }
        if (errors) printf("%d error%s\n", errors, errors == 1 ? "" : "s");
        return errors ? 10 : 0;
    }
    if (!strcmp(cmd, "build")) {
        const char *out = "a.out";
        int debug = 0;
        for (int i = a + 1; i < argc; i++) {
            if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
            else if (!strcmp(argv[i], "--debug")) debug = 1;
        }
        FILE *f = fopen(out, "w");
        if (!f) {
            printf("boingstub: can't write %s\n", out);
            return 20;
        }
        fprintf(f, "echo \"%s, built by boingstub (%s)\"\n", target, debug ? "debug" : "release");
        fclose(f);
        IDOS->SetProtection(out, 0);
        printf("boingstub: %s -> %s (%s, %d files)\n", target, out, debug ? "-O0, #line" : "-O2", nfiles);
        return 0;
    }
    if (!strcmp(cmd, "run")) {
        printf("boingstub run %s:", target);
        for (int i = 0; i < nfiles; i++) printf(" %s", files[i]);
        for (int i = a + 1; i < argc; i++) printf(" [%s]", argv[i]);
        printf("\n");
        fflush(stdout);
        for (int i = 0; i < nfiles; i++) {
            if (find(files[i], "STUB_WAIT", NULL)) {
                printf("waiting for Ctrl-C...\n");
                fflush(stdout);
                IExec->Wait(SIGBREAKF_CTRL_C);
                printf("*** Break\n");
                return 10;
            }
            int l = find(files[i], "STUB_RUNTIME", NULL);
            if (l) {
                char msg[1024];
                snprintf(msg, sizeof msg, "%.900s:%d: runtime error 9: Subscript out of range\n", files[i], l);
                fputs(msg, stdout);
                if (errFile) {
                    FILE *f = fopen(errFile, "w");
                    if (f) {
                        fputs(msg, f);
                        fclose(f);
                    }
                }
                return 20;
            }
        }
        printf("Hello from the stub interpreter!\n");
        return 0;
    }
    if (!strcmp(cmd, "symbols")) {
        for (int i = 0; i < nfiles; i++) {
            FILE *f = fopen(files[i], "r");
            char line[512];
            int n = 0;
            if (!f) continue;
            while (fgets(line, sizeof line, f)) {
                n++;
                char *p = line;
                while (*p == ' ') p++;
                if (!strncasecmp(p, "Private ", 8)) p += 8;
                if (!strncasecmp(p, "Sub ", 4) || !strncasecmp(p, "Function ", 9)) {
                    int fn = tolower((unsigned char)*p) == 'f';
                    char *nm = p + (fn ? 9 : 4), *e = nm;
                    while (isalnum((unsigned char)*e) || *e == '_') e++;
                    *e = 0;
                    printf("%s\t%s\t%d\n", fn ? "function" : "sub", nm, n);
                }
            }
            fclose(f);
        }
        return 0;
    }
    printf("boingstub: unknown command %s\n", cmd);
    return 20;
}
