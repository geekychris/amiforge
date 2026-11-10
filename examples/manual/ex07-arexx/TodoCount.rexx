/* TodoCount.rexx - an AmiForge macro: counts the lines of the current file that say TODO,
   shows the count in the status bar, and puts the cursor on the first one.
   (The AmiForge manual's exercise 7: copy it to AmiForge's Rexx drawer and choose
   Macros > Rescan Macros.) */
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
