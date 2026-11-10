' Main.bas - rolls a die 600 times with the Dice class from dice.c, and counts the faces
Sub Main()
    Dim d As New Dice, counts(1 To 6) As Long, i As Long, face As Long
    d.Seed = 42
    For i = 1 To 600
        face = d.Roll()
        counts(face) = counts(face) + 1
    Next
    For face = 1 To 6
        Print face; ":"; counts(face); "("; Percent(counts(face), d.Rolls); "%)"
    Next
    Print "rolls:"; d.Rolls
End Sub
