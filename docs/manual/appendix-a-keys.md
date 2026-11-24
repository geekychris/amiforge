# Appendix A: The keys

The keys AmiForge handles itself, and the menus' Amiga-key shortcuts. Both tables are made from
AmiForge's source, so they always match the program; **Help > Keyboard Shortcuts...** shows a
summary in AmiForge.

- A combination with no command of its own (Ctrl-F7, say) does what the key alone does.
- F11, F12 and Break are on PC keyboards (QEMU passes them on); on an Amiga keyboard use the
  menus, or **Help** for F1.
- In the editor, the usual editing keys work as well: the cursor keys with Shift to select,
  Ctrl-Space for completion, Tab and Shift-Tab to indent a selection, Esc to go back to one
  cursor or end a snippet, Alt-click to add a cursor, and Ctrl-click to open an include file or
  go to a definition.
- The right mouse button over the editor shows the menu bar; with Shift or Ctrl held it opens
  the editor's context menu.

## Function keys

<!-- generated below by tools/manual-ref.py: don't edit what follows -->

| Keys | What they do |
|---|---|
| F1 | AutoDoc for the word at the cursor |
| Help | AutoDoc for the word at the cursor |
| F2 | Rename the symbol everywhere |
| F3 | Find next |
| Shift-F3 | Find previous |
| F4 | Next error |
| Shift-F4 | Previous error |
| F5 | Start debugging / continue |
| Shift-F5 | Stop debugging |
| Ctrl-F5 | Build and run |
| Shift-Ctrl-F5 | Stop the program Run started |
| Alt-F5 | Pause the program being debugged |
| Shift-Alt-F5 | Restart debugging (builds first) |
| Break | Pause the program being debugged |
| F6 | Build and run |
| F7 | Build |
| Shift-F7 | Profile the program |
| Alt-F7 | Boing BASIC: switch between a form and its code |
| F8 | Step one instruction |
| F9 | Set / clear a breakpoint on the line |
| Shift-F9 | Watch the word or selection at the cursor |
| Ctrl-F9 | Apply code changes (Edit and Continue) |
| Alt-F9 | Enable / disable the breakpoint on the line |
| Shift-Ctrl-F9 | Breakpoint condition for the line |
| F10 | Step over |
| Ctrl-F10 | Run to the cursor |
| Shift-Ctrl-F10 | Set the next statement to the cursor's line |
| Alt-F10 | Run to a function |
| F11 | Step into |
| Shift-F11 | Step out |
| F12 | Go to the definition |
| Shift-F12 | Find all references |

## Menu shortcuts

The right Amiga key with a letter chooses a menu item. Some letters do more than one
thing: Intuition takes the first item with that letter.

| Keys | Menu item |
|---|---|
| Amiga-P | Project > Open Project... |
| Amiga-U | Project > Browse Projects... |
| Amiga-? | Project > About AmiForge... |
| Amiga-Q | Project > Quit |
| Amiga-N | File > New |
| Amiga-O | File > Open... |
| Amiga-S | File > Save |
| Amiga-A | File > Save As... |
| Amiga-W | File > Close |
| Amiga-Z | Edit > Undo |
| Amiga-Y | Edit > Redo |
| Amiga-X | Edit > Cut |
| Amiga-C | Edit > Copy |
| Amiga-V | Edit > Paste |
| Amiga-F | Edit > Find... |
| Amiga-G | Edit > Find Next  (F3) |
| Amiga-R | Edit > Replace... |
| Amiga-H | Edit > Find in Files... |
| Amiga-L | Edit > Go to Line... |
| Amiga-D | Edit > Open Include File  (Ctrl-Click) |
| Amiga-J | Edit > Go to Definition  (F12, Ctrl-Click) |
| Amiga-T | Edit > Add Next Occurrence |
| Amiga-/ | Edit > Comment/Uncomment Lines |
| Amiga-] | Edit > Indent |
| Amiga-[ | Edit > Unindent |
| Amiga-+ | View > Bigger Text |
| Amiga-- | View > Smaller Text |
| Amiga-0 | View > Normal Text Size |
| Amiga-M | View > Markdown Preview |
| Amiga-K | Build > Compile File |
| Amiga-B | Build > Build  (F7) |
| Amiga-E | Build > Run  (Ctrl F5) |
| Amiga-D | Debug > Start / Continue  (F5) |
| Amiga-I | Debug > Profile  (Shift F7) |
