' ${NAME}: a Boing BASIC console program.
' Run (Ctrl-F5) starts it in the interpreter with the arguments from Project > Settings,
' Build (F7) makes a native program, Shift-Ctrl-F5 stops it.
Option Explicit

Sub Main()
    Dim who As String, i As Integer
    who = Trim(Command())
    If who = "" Then who = "World"
    Print "Hello, " & who & "!"
    For i = 1 To 3
        Print "Line " & i
    Next i
End Sub
