import os
import sys

from .cli import main

code = main()
sys.stdout.flush()
sys.stderr.flush()
os._exit(code)  # in-flight network threads must not delay the exit after the manifest is written
