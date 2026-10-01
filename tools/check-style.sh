#!/bin/sh
# Fails on CR characters or trailing blanks in .c .cpp .sh .md .py .json files.
cd "$(dirname "$0")/.." || exit 1
bad=0
for f in $(git ls-files -co --exclude-standard -- '*.c' '*.cpp' '*.sh' '*.md' '*.py' '*.json'); do
	[ -f "$f" ] || continue
	if grep -q "$(printf '\r')" "$f"; then echo "CR in $f"; bad=1; fi
	if grep -nE '[[:blank:]]+$' "$f" >/dev/null; then echo "trailing blank in $f"; grep -nE '[[:blank:]]+$' "$f" | head -3; bad=1; fi
done
[ "$bad" = 0 ] && echo "style ok"
exit $bad
