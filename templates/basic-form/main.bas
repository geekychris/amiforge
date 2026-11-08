' ${NAME}: the code behind the form main.afg.
' Tools > GUI Designer shows the form: double-click a control to get to its event sub
' (Click for a button, Change for a text field), or choose one from its Events list.
' Run (Ctrl-F5) starts the program in the interpreter, Build (F7) makes a native program.
Option Explicit

Private Sub Form_Load()
    greeting.Text = "Hello! Type your name and press Greet."
End Sub

Private Sub greet_Click()
    greeting.Text = "Hello, " & who.Text & "!"
End Sub

Private Sub who_Change()
    greet.Enabled = Len(who.Text) > 0
End Sub

Private Sub quit_Click()
    Unload Me
End Sub
