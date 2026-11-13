import os
import sys

from .cli import main

code = main()
sys.stdout.flush()
sys.stderr.flush()
# Network threads (DNS, archive) may still sit in a socket timeout; the report is written, so leave at once.
os._exit(code)
