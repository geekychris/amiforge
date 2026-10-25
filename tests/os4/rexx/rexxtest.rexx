/* rexxtest.rexx - tests AmiForge's ARexx port. AmiForge must be running.
     rx rexxtest.rexx [macrodir|-] [port]
   Results go to RAM:rexxtest.log ("PASS name" / "FAIL name: detail", then a summary).
   With macrodir, the three example macros are run against a scratch file too. port: the
   ARexx port to test (default AMIFORGE; e.g. a copy started with REXXPORT=AFTEST). */
OPTIONS RESULTS
OPTIONS FAILAT 100
PARSE ARG macros host .
IF macros = '-' THEN macros = ''
IF host = '' THEN host = 'AMIFORGE'
IF host ~= 'AMIFORGE' THEN macros = ''   /* the example macros address AMIFORGE */
CALL OPEN('log', 'RAM:rexxtest.log', 'W')
pass = 0
fail = 0
IF ~SHOW('P', host) THEN DO
    CALL WRITELN('log', 'FAIL no' host 'port')
    CALL CLOSE('log')
    EXIT 10
END
ADDRESS VALUE host

file = 'RAM:rexxtest.c'
CALL OPEN('f', file, 'W')
CALL WRITELN('f', 'int main(void)')
CALL WRITELN('f', '{')
CALL WRITELN('f', '    return 0;')
CALL WRITELN('f', '}')
CALL CLOSE('f')

'OPEN' file;                      CALL check RC = 0, 'OPEN', RC
'GETFILE';                        CALL check POS('rexxtest.c', RESULT) > 0, 'GETFILE', RESULT
'GOTO 3 5'
'GETLINE';                        CALL check RESULT = 3, 'GOTO+GETLINE', RESULT
'GETCOL';                         CALL check RESULT = 5, 'GETCOL', RESULT
'GETLINETEXT 3';                  CALL check RESULT = '    return 0;', 'GETLINETEXT', RESULT
'GETWORD';                        CALL check RESULT = 'return', 'GETWORD', RESULT
'GOTO LINE=1'
'INSERT /* top */\n'
'GETLINETEXT 1';                  CALL check RESULT = '/* top */', 'INSERT', RESULT
'GETTEXT ALL';                    CALL check LEFT(RESULT, 9) = '/* top */' & LENGTH(RESULT) > 40, 'GETTEXT ALL', LENGTH(RESULT)
'GOTO 1'
'FIND return';                    CALL check RC = 0 & RESULT = 4, 'FIND', RC RESULT
'GETTEXT';                        CALL check RESULT = 'return', 'GETTEXT selection', RESULT
'REPLACE exit'
'GETLINETEXT 4';                  CALL check RESULT = '    exit 0;', 'REPLACE', RESULT
'FIND no_such_text_here';         CALL check RC = 5, 'FIND not found', RC
'SELECT 2 3'
'GETTEXT';                        CALL check RESULT = 'int main(void)' || '0a'x || '{' || '0a'x, 'SELECT', RESULT
'SAVE';                           CALL check RC = 0, 'SAVE', RC
CALL OPEN('f', file, 'R')
l1 = READLN('f'); l2 = READLN('f'); l3 = READLN('f'); l4 = READLN('f')
CALL CLOSE('f')
CALL check l1 = '/* top */' & l4 = '    exit 0;', 'SAVE wrote the file', l1 '/' l4
'GOTO notanumber';                CALL check RC = 10, 'bad argument', RC
'GOTO';                           CALL check RC = 10, 'missing argument', RC
'HELP GOTO';                      CALL check POS('LINE/N/A', RESULT) > 0, 'HELP GOTO', RESULT
'HELP';                           CALL check POS('GETTEXT', RESULT) > 0 & POS('QUIT', RESULT) > 0, 'HELP', LENGTH(RESULT)
'GETPROJECT';                     CALL check RC = 0, 'GETPROJECT', RESULT
'STATUS ARexx test running'
'SAVEAS RAM:rexxtest2.c';         CALL check RC = 0 & POS('rexxtest2.c', RESULT) > 0, 'SAVEAS', RESULT
CALL check EXISTS('RAM:rexxtest2.c'), 'SAVEAS wrote', ''

/* --- mcp --- the MCP commands (results are "AF1 rc more length payload") */
'MCP GETSTATE';                   CALL check LEFT(RESULT, 6) = 'AF1 0 ' & POS('debug=none', RESULT) > 0, 'MCP GETSTATE', LEFT(RESULT, 60)
'MCP NOSUCHCOMMAND';              CALL check RC = 0 & WORD(RESULT, 2) = 20, 'MCP unknown command', RESULT
'MCP GOTO notanumber';            CALL check WORD(RESULT, 2) = 10 & POS('must be a number', RESULT) > 0, 'MCP error text', RESULT
'MCP READ FROMLINE=4 TOLINE=4';   CALL check POS('\t4\t4\t', RESULT) > 0 & POS('exit 0;', RESULT) > 0, 'MCP READ', RESULT
'MCP SUBST OLD=exit\s0; NEW=exit\s1;'
                                  CALL check WORD(RESULT, 2) = 0, 'MCP SUBST', RESULT
'GETLINETEXT 4';                  CALL check RESULT = '    exit 1;', 'MCP SUBST applied', RESULT
'MCP SUBST OLD=no_such_text';     CALL check WORD(RESULT, 2) = 5, 'MCP SUBST not found', RESULT
'MCP EDITLINES FROMLINE=1 TOLINE=1 TEXT=/*\stop2\s*/'
'GETLINETEXT 1';                  CALL check RESULT = '/* top2 */', 'MCP EDITLINES', RESULT
'MCP MCPBUF SLOT=1 CLEAR TEXT=int\sy'
'MCP MCPBUF SLOT=1 TEXT=;'
'MCP EDITLINES FROMLINE=2 TOLINE=1 TEXT=\@1'
'GETLINETEXT 2';                  CALL check RESULT = 'int y;', 'MCP MCPBUF + insert', RESULT
'MCP SYMBOLS';                    CALL check POS('function\tmain', RESULT) > 0, 'MCP SYMBOLS', RESULT
'MCP DOCUMENTS';                  CALL check POS('rexxtest2.c', RESULT) > 0, 'MCP DOCUMENTS', RESULT
'MCP LOCALS';                     CALL check WORD(RESULT, 2) = 10, 'MCP LOCALS without debugging', RESULT
'MCP REFERENCES NAME=main';       CALL check WORD(RESULT, 2) = 0, 'MCP REFERENCES', RESULT
'MCP HELP';                       CALL check WORD(RESULT, 3) > 0, 'MCP HELP is paged', WORD(RESULT, 3)
'MCPMORE';                        CALL check LEFT(RESULT, 4) = 'AF1 ', 'MCPMORE', LEFT(RESULT, 20)

IF macros ~= '' THEN DO
    /* the example macros */
    CALL OPEN('f', 'RAM:macrotest.c', 'W')
    CALL WRITELN('f', 'pear')
    CALL WRITELN('f', 'apple')
    CALL WRITELN('f', 'fig')
    CALL WRITELN('f', 'int x = 1;')
    CALL CLOSE('f')
    'OPEN RAM:macrotest.c'
    'SELECT 1 3'
    CALL runmacro macros || '/SortLines.rexx'
    'GETLINETEXT 1'; a = RESULT; 'GETLINETEXT 2'; b = RESULT; 'GETLINETEXT 3'; c = RESULT
    CALL check a = 'apple' & b = 'fig' & c = 'pear', 'SortLines macro', a b c
    'GOTO 4'
    CALL runmacro macros || '/IfZero.rexx'
    'GETLINETEXT 4'; a = RESULT; 'GETLINETEXT 5'; b = RESULT; 'GETLINETEXT 6'; c = RESULT
    CALL check a = '#if 0' & b = 'int x = 1;' & c = '#endif', 'IfZero macro', a '/' b '/' c
    CALL runmacro macros || '/HeaderComment.rexx'
    'GETLINETEXT 2'
    CALL check RESULT = ' * macrotest.c', 'HeaderComment macro', RESULT
    'CLOSE FORCE'
END

'CLOSE FORCE';                    CALL check RC = 0, 'CLOSE FORCE', RC
CALL WRITELN('log', 'DONE' pass 'passed,' fail 'failed')
CALL CLOSE('log')
EXIT fail

check: PROCEDURE EXPOSE pass fail
    PARSE ARG ok, name, detail
    IF ok THEN DO
        pass = pass + 1
        CALL WRITELN('log', 'PASS' name)
    END
    ELSE DO
        fail = fail + 1
        CALL WRITELN('log', 'FAIL' name':' detail)
    END
    RETURN

/* run a macro with AmiForge as its host, the way the Macros menu does */
runmacro: PROCEDURE
    PARSE ARG m
    ADDRESS COMMAND 'rx' m
    RETURN
