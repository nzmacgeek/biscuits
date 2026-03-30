from pathlib import Path
p=Path('Makefile')
s=p.read_text()
start = s.find('# Ensure object files are rebuilt when ARCH changes')
if start==-1:
    print('marker not found')
    raise SystemExit(1)
end = s.find('$(OBJECTS): $(ARCH_STAMP)', start)
if end==-1:
    print('end marker not found')
    raise SystemExit(1)
line_end = s.find('\n', end)
block = '''# Ensure object files are rebuilt when ARCH changes: maintain a small stamp
# file `.arch_record` containing the last-built ARCH. When ARCH differs the
# stamp is updated which forces object files to be rebuilt.
ARCH_STAMP := .arch_record

.PHONY: FORCE
FORCE:

$(ARCH_STAMP): FORCE
	@printf "%s\\n" "$(ARCH)" > $(ARCH_STAMP).tmp
	@if [ -f $(ARCH_STAMP) ] && cmp -s $(ARCH_STAMP) $(ARCH_STAMP).tmp; then \\
		rm -f $(ARCH_STAMP).tmp; \\
	else \\
		mv $(ARCH_STAMP).tmp $(ARCH_STAMP); \\
	fi

$(OBJECTS): $(ARCH_STAMP)
'''
new = s[:start] + block + s[line_end+1:]
p.write_text(new)
print('patched')
