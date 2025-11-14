#!/bin/bash
echo "Recent neowalld logs (last 50 lines):"
echo "========================================"
journalctl --user -u neowalld -n 50 --no-pager 2>/dev/null | grep -E "(duration|cycle timer|RESET|last_cycle_time)" || echo "Could not read logs. Try: journalctl --user -u neowalld -f"
