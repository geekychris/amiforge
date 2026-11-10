/* BuildReport.rexx - builds AmiForge's project from a Shell and prints the errors:
       rx BuildReport.rexx
   It uses the commands for scripts (sent as MCP <command>), which never open a requester
   and return at once: STARTBUILD starts the build, GETSTATE says when it has finished, and
   ERRORS lists the errors, one a line with tab-separated fields.
   (The AmiForge manual's exercise 7.) */
OPTIONS RESULTS
ADDRESS AMIFORGE

'MCP STARTBUILD'
IF Payload() ~= '' & Code() ~= 0 THEN DO
    SAY 'The build did not start:' Unescape(Payload())
    EXIT 10
END

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

/* RESULT of an MCP command: "AF1 rc more length payload" */
Code: PROCEDURE EXPOSE RESULT
    PARSE VAR RESULT 'AF1' rc .
    RETURN rc

Payload: PROCEDURE EXPOSE RESULT
    PARSE VAR RESULT 'AF1' rc more length payload
    RETURN payload

/* \n, \t and \\ back into a new line, a tab and a backslash */
Unescape: PROCEDURE
    PARSE ARG s
    out = ''
    DO WHILE s ~= ''
        i = POS('\', s)
        IF i = 0 THEN DO; out = out || s; LEAVE; END
        out = out || LEFT(s, i - 1)
        c = SUBSTR(s, i + 1, 1)
        SELECT
            WHEN c = 'n' THEN out = out || '0A'x
            WHEN c = 't' THEN out = out || '09'x
            WHEN c = 's' THEN out = out || ' '
            OTHERWISE out = out || c
        END
        s = SUBSTR(s, i + 2)
    END
    RETURN out
