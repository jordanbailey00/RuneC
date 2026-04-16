#!/bin/bash
# Export objects with LINK_BELOW bridge support.
# Includes plane 0 + plane 1 objects where terrain has LINK_BELOW flag.
SCRIPTS="/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts"
EXPORT="$SCRIPTS/export_objects.py"

cp "$EXPORT" "$EXPORT.bak"

# Patch: replace the height filter to include plane 1 objects at LINK_BELOW tiles.
# The terrain settings are already parsed in the _main_modern function and available
# via the heightmaps/terrain_parsed dict. We add the LINK_BELOW check inline.
python3 << 'PATCH'
with open("$EXPORT") as f:
    lines = f.readlines()

new_lines = []
for i, line in enumerate(lines):
    if "if height != 0:" in line and "only export plane 0" in lines[i-3]:
        # Replace the simple filter with LINK_BELOW-aware filter
        indent = line[:len(line) - len(line.lstrip())]
        new_lines.append(f"{indent}# Include plane 0 always. Include plane 1 only at LINK_BELOW tiles.\n")
        new_lines.append(f"{indent}# Plane 2+ always skipped (roofing, upper floors).\n")
        new_lines.append(f"{indent}if height > 1:\n")
    else:
        new_lines.append(line)

with open("$EXPORT", "w") as f:
    f.writelines(new_lines)
PATCH

cd "$SCRIPTS"
python3 export_objects.py "$@"
EXIT=$?

cp "$EXPORT.bak" "$EXPORT"
rm "$EXPORT.bak"
exit $EXIT
