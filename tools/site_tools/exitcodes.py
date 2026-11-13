"""Process exit codes shared by the tools (docs/PRD.md §5.4). The PHP worker maps them to job states."""

OK = 0
PARTIAL = 1
INVALID_INPUT = 2
FAILED = 3  # unreachable or nothing useful collected
BLOCKED = 4  # refused by the network policy (SSRF guard)
CANCELLED = 130
