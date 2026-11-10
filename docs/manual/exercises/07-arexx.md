# Exercise 7: Driving AmiForge from ARexx

You write a macro for the Macros menu that finds the TODO lines in a file, and a script that
builds the project from a Shell and prints its errors, using the commands AmiForge provides for
scripts.

The two scripts are in `examples/manual/ex07-arexx/`.

## 1. A macro

Make `TodoCount.rexx` in AmiForge's `Rexx` drawer (the Macros menu's **Open Macros Drawer**
opens it):

```rexx
/* TodoCount.rexx - counts the lines of the current file that say TODO */
OPTIONS RESULTS
IF ADDRESS() = 'REXX' THEN ADDRESS AMIFORGE

'GETTEXT ALL'
text = RESULT
nl = '0A'x
count = 0
first = 0
line = 0
DO WHILE text ~= ''
    PARSE VAR text one (nl) text
    line = line + 1
    IF POS('TODO', one) > 0 THEN DO
        count = count + 1
        IF first = 0 THEN first = line
    END
END

IF count = 0 THEN 'STATUS No TODO in this file'
ELSE DO
    'GOTO' first
    'STATUS' count 'TODO line(s); the first is line' first
END
EXIT 0
```

Choose **Macros > Rescan Macros**: **TodoCount** appears in the Macros menu. Open a file with
a few `TODO` comments and choose it: the status bar says how many there are, and the cursor
goes to the first.

`GETTEXT ALL` returns the whole file, `GOTO` moves the cursor, and `STATUS` writes in the
status bar; **Macros > ARexx Commands...** lists the others.

## 2. A script from the Shell

The commands for scripts never open a requester and return at once, and their results are
easy for a program to read; they're sent wrapped as `MCP <command>`, and the answer is
`AF1 <rc> <more> <length> <payload>` (see [ARexx and MCP
control](../13-arexx-mcp.md#the-format)). `BuildReport.rexx` starts a build with `STARTBUILD`,
asks `GETSTATE` once a second until `build=idle`, and prints what `ERRORS` lists:

```rexx
/* BuildReport.rexx - builds AmiForge's project and prints the errors */
OPTIONS RESULTS
ADDRESS AMIFORGE

'MCP STARTBUILD'
DO FOREVER
    ADDRESS COMMAND 'Wait 1'
    'MCP GETSTATE'
    state = Unescape(Payload())
    IF POS('build=idle', state) > 0 THEN LEAVE
END
PARSE VAR state . 'buildresult=' outcome '0A'x .
SAY 'Build:' outcome

'MCP ERRORS'
errors = Unescape(Payload())
DO WHILE errors ~= ''
    PARSE VAR errors one '0A'x errors
    PARSE VAR one kind '09'x file '09'x line '09'x col '09'x message '09'x .
    IF kind ~= '' THEN SAY file':'line':' kind':' message
END
EXIT 0
```

`Payload()` takes the payload out of the RESULT, and `Unescape()` turns `\n`, `\t` and `\\`
back into a new line, a tab and a backslash; both are in the solution. Put a mistake in the
project's code and run the script from a Shell with `rx BuildReport.rexx`:

```output
Build: failed
wordcount.c:31: error: 'lnies' undeclared (first use in this function)
```

## Try this

- Make the macro show every TODO line in a requester, with `REQUEST`.
- Extend `BuildReport.rexx` to run the program when the build is good (`RUNPROGRAM CAPTURE`,
  then `RUNOUTPUT`).
- Write a macro that comments out the selection with `GETTEXT` and `REPLACE`.
