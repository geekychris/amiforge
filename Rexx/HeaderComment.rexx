/* HeaderComment.rexx - an AmiForge macro: put a header comment at the top of the file
   with its name and today's date. */
OPTIONS RESULTS
IF ADDRESS() = 'REXX' THEN ADDRESS AMIFORGE

'GETFILE'
file = RESULT
IF file = '' THEN file = 'untitled'
p = MAX(LASTPOS('/', file), LASTPOS(':', file))
name = SUBSTR(file, p + 1)
d = DATE('S')
today = LEFT(d, 4) || '-' || SUBSTR(d, 5, 2) || '-' || RIGHT(d, 2)

'GOTO 1 1'
'INSERT /*\n * ' || name || '\n *\n * Created ' || today || '.\n */\n\n'
'STATUS Added a header comment'
EXIT 0
