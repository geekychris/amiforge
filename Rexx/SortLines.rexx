/* SortLines.rexx - an AmiForge macro: sort the selected lines alphabetically. */
OPTIONS RESULTS
IF ADDRESS() = 'REXX' THEN ADDRESS AMIFORGE

'GETTEXT'
text = RESULT
IF text = '' THEN DO
    'REQUEST Select the lines to sort first.'
    EXIT 5
END
/* keep a final newline */
nl = '0a'x
final = RIGHT(text, 1) = nl
IF final THEN text = LEFT(text, LENGTH(text) - 1)
n = 0
DO WHILE text ~= ''
    PARSE VAR text line (nl) text
    n = n + 1
    lines.n = line
END
/* insertion sort */
DO i = 2 TO n
    v = lines.i
    j = i - 1
    DO WHILE j > 0
        IF lines.j <<= v THEN LEAVE
        k = j + 1
        lines.k = lines.j
        j = j - 1
    END
    k = j + 1
    lines.k = v
END
out = ''
DO i = 1 TO n
    out = out || esc(lines.i)
    IF i < n | final THEN out = out || '\n'
END
'REPLACE' out
'STATUS Sorted' n 'lines'
EXIT 0

/* REPLACE takes one line: newlines, tabs and backslashes go as \n, \t, \\ */
esc: PROCEDURE
    PARSE ARG s
    o = ''
    DO i = 1 TO LENGTH(s)
        c = SUBSTR(s, i, 1)
        SELECT
            WHEN c = '\' THEN o = o || '\\'
            WHEN c = '09'x THEN o = o || '\t'
            OTHERWISE o = o || c
        END
    END
    RETURN o
