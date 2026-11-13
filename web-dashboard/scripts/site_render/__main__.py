import os
import sys

from .cli import main

code = main()
sys.stdout.flush()
sys.stderr.flush()
os._exit(code)  # a browser that is shutting down must not delay the exit after the result is written
