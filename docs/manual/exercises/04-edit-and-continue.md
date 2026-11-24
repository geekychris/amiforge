# Exercise 4: Edit and Continue

You change a function while the program that uses it is stopped in the debugger, apply the
change with **Ctrl-F9**, and watch the program carry on with the new code and its old data.

The finished project is in `examples/manual/ex04-compute/`.

## 1. The program

Make a project with this program as `compute.c`:

<!-- project: examples/manual/ex04-compute -->
```c
#include <stdio.h>
#include <proto/dos.h>

int counter = 0;

int compute(int x)
{
    counter++;
    return x * 2;
}

int main(void)
{
    for (int i = 1; i <= 10; i++) {
        int r = compute(i);
        printf("call %d: compute(%d) = %d, counter = %d\n", i, i, r, counter);
        IDOS->Delay(25);      /* half a second, so there's time to stop it */
    }
    return 0;
}
```

Run it once with **Ctrl-F5**: ten lines, `compute(1) = 2` up to `compute(10) = 20`.

## 2. Stop inside compute()

Put a breakpoint (**F9**) on `return x * 2;` and press **F5**. The program stops in the first
call, with `x` at 1 in the Locals.

## 3. Change it and apply

Change the line to `return x * 2 + 1000;` and press **Ctrl-F9** (or **Debug > Apply Code
Changes**). The Build tab shows the file being compiled and then:

```text
Applied the changes to compute.c: compute(), main(). compute(), main() are running: the
current call finishes in the old code, the next call uses the new code.
```

Every function in the file is replaced, so `main()` is listed too, although only `compute()`
changed. Both are in the middle of a call: `main()` stays in its loop in the old code, and
that old code's next call to `compute()` goes to the new one.

Remove the breakpoint (**F9** on the line) and press **F5** to carry on. The console shows:

```output
call 1: compute(1) = 2, counter = 1
call 2: compute(2) = 1004, counter = 2
call 3: compute(3) = 1006, counter = 3
```

and so on. The call you were stopped in finished in the old code; every later call used the
new one; and `counter`, a global, kept its value: the program's data wasn't touched.

## 4. What can't be changed

While the program runs again, stop it (**Alt-F5**, or a breakpoint) and add a new global
variable, `int extra = 0;`, then press **Ctrl-F9**. The Build tab says the change can't be
applied, because the program has no room for a new variable, and nothing is changed. Restart
the program (**Shift-Alt-F5**) to use it.

## Try this

- Add a new function and call it from `compute()`: new functions can be applied.
- Change `main()`'s loop while stopped in it. When does the change take effect?
- Apply a change twice to the same function.
