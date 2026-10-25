/* rexxtest.rexx - tests AmiForge's ARexx port. AmiForge must be running.
     rx rexxtest.rexx [macrodir]
   Results go to RAM:rexxtest.log ("PASS name" / "FAIL name: detail", then a summary).
   With macrodir, the three example macros are run against a scratch file too. */
OPTIONS RESULTS
OPTIONS FAILAT 100
PARSE ARG macros
CALL OPEN('log', 'RAM:rexxtest.log', 'W')
pass = 0
fail = 0
IF ~SHOW('P', 'AMIFORGE') THEN DO
    CALL WRITELN('log', 'FAIL no AMIFORGE port')
    CALL CLOSE('log')
    EXIT 10
END
ADDRESS AMIFORGE

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
