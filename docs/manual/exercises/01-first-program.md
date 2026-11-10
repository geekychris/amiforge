# Exercise 1: A first program from a template

You make a Shell program from the **Console C program** template and change it into
`wordcount`, which counts the lines, words and characters of a file. You'll use the template
chooser, the editor's completion and AutoDocs, the project settings, and Build and Run.

The finished project is in `examples/manual/ex01-wordcount/`.

## 1. Make the project

1. Choose **Project > New Project from Template...**.
2. Choose **Console C program**, and type `wordcount` as the name. Choose a drawer to make it
   in, such as `Work:`.
3. Type your name as the author when asked.

AmiForge makes `Work:wordcount/` with `wordcount.c` and `wordcount.afp`, and opens
`wordcount.c`. Press **F7** to build it and **Ctrl-F5** to run it: it says hello to `WORLD`,
the argument the template put in the project's settings.

## 2. Read a file instead

The template reads its arguments with `ReadArgs()`. Change the template line and the `enum`
under it:

<!-- fragment -->
```c
#define TEMPLATE "FILE/A,LONGEST/S"
enum { ARG_FILE, ARG_LONGEST, ARG_COUNT };
```

Then replace the body of `main()` after the `ReadArgs()` check with code that opens the file
and reads it a character at a time. Put the cursor on `Open` after typing `IDOS->Open` and
press **F1**: the AutoDoc explains `MODE_OLDFILE` and what `Open()` returns when it fails.
Type `IDOS->` and the completion list shows dos.library's functions; choose `FGetC`.

<!-- fragment -->
```c
const char *name = (const char *)args[ARG_FILE];
BPTR fh = IDOS->Open(name, MODE_OLDFILE);
if (!fh) {
    IDOS->PrintFault(IDOS->IoErr(), name);
    IDOS->FreeArgs(rda);
    return RETURN_FAIL;
}

long lines = 0, words = 0, chars = 0;
int inword = 0, c;
while ((c = IDOS->FGetC(fh)) >= 0) {
    chars++;
    if (c == '\n') lines++;
    if (isspace(c)) inword = 0;
    else if (!inword) {
        inword = 1;
        words++;
    }
}
IDOS->Close(fh);
printf("%ld lines, %ld words, %ld characters in %s\n", lines, words, chars, name);
```

Remove the lines that used `name[]` and `times`, which are no longer needed.

## 3. Build and fix

Press **F7**. If you made a mistake, the **Build** tab lists the error and the editor marks the
line; click the error to go there. Errors that are typical of the Amiga (an OS function called
without its interface, a missing `<proto/...>` header) get an `Explain:` line under them:
double-click it.

## 4. Give it a file and run it

Choose **Project > Settings...** and set **Program arguments** to `S:Startup-Sequence`. Save
the settings and press **Ctrl-F5**:

```output
41 lines, 128 words, 1101 characters in S:Startup-Sequence
```

(Your numbers depend on your Startup-Sequence.)

## Try this

- Make `LONGEST` print the length of the longest line. The solution does this.
- Accept several files (`FILE/A/M`) and print a total.
- Count only the words that start with a capital letter.
- Run it on a binary file: what happens to the word count?
