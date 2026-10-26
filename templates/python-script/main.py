"""${NAME}: a Python 3 script.

Build (F7) checks the syntax of every .py file in the project, Run (Ctrl-F5) runs this
script with the arguments from Project > Settings, and Shift-Ctrl-F5 stops it.
"""
import argparse
import sys


def greet(name, times):
    """Return the greeting for `name`, `times` times."""
    return "\n".join("Hello, %s!" % name for _ in range(times))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Says hello.")
    parser.add_argument("name", nargs="?", default="World", help="who to greet")
    parser.add_argument("-n", "--times", type=int, default=1, help="how many times")
    args = parser.parse_args(argv)
    print(greet(args.name, args.times))
    return 0


if __name__ == "__main__":
    sys.exit(main())
