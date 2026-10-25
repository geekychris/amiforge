/* IfZero.rexx - an AmiForge macro: wrap the selected lines (or the cursor's line) in
   #if 0 ... #endif, to switch code off without commenting it out. */
OPTIONS RESULTS
IF ADDRESS() = 'REXX' THEN ADDRESS AMIFORGE

'GETTEXT'
text = RESULT
IF text = '' THEN DO
    'GETLINE'
    line = RESULT
    'SELECT' line line
    'GETTEXT'
    text = RESULT
END
nl = '0a'x
IF RIGHT(text, 1) ~= nl THEN text = text || nl
'REPLACE #if 0\n' || esc(text) || '#endif\n'
'STATUS Wrapped in #if 0'
EXIT 0

esc: PROCEDURE
    PARSE ARG s
    o = ''
    DO i = 1 TO LENGTH(s)
        c = SUBSTR(s, i, 1)
        SELECT
            WHEN c = '\' THEN o = o || '\\'
            WHEN c = '0a'x THEN o = o || '\n'
            WHEN c = '09'x THEN o = o || '\t'
            OTHERWISE o = o || c
        END
    END
    RETURN o
