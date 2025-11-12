#!/bin/bash
# Script to fix config structure references after changing from pointer to embedded struct

set -e

echo "Fixing config structure references in NeoWall codebase..."

# Change directory to project root
cd "$(dirname "$0")/.."

# Backup files before modifying
echo "Creating backups..."
find src/neowalld -name "*.c" -exec cp {} {}.bak \;

# Fix output->config-> to output->config.
echo "Fixing output->config-> references..."
find src/neowalld -name "*.c" -exec sed -i 's/output->config->/output->config\./g' {} \;

# Fix summary_output->config-> to summary_output->config.
echo "Fixing summary_output->config-> references..."
find src/neowalld -name "*.c" -exec sed -i 's/summary_output->config->/summary_output->config\./g' {} \;

# Fix backup_output->config-> to backup_output->config.
echo "Fixing backup_output->config-> references..."
find src/neowalld -name "*.c" -exec sed -i 's/backup_output->config->/backup_output->config\./g' {} \;

# Fix restore_output->config-> to restore_output->config.
echo "Fixing restore_output->config-> references..."
find src/neowalld -name "*.c" -exec sed -i 's/restore_output->config->/restore_output->config\./g' {} \;

# Fix other common output variable names
echo "Fixing other config pointer references..."
find src/neowalld -name "*.c" -exec sed -i 's/\([a-z_]*output\)->config->/\1->config\./g' {} \;

echo ""
echo "Automated fixes complete!"
echo ""
echo "Manual fixes still needed in src/neowalld/config/config.c:"
echo "  1. Change: memcpy(&backup_configs[idx], backup_output->config, ...)"
echo "     To:     memcpy(&backup_configs[idx], &backup_output->config, ...)"
echo ""
echo "  2. Change: config_free_wallpaper(output->config)"
echo "     To:     config_free_wallpaper(&output->config)"
echo ""
echo "  3. Change: init_wallpaper_config_defaults(output->config)"
echo "     To:     init_wallpaper_config_defaults(&output->config)"
echo ""
echo "  4. Remove: if (!summary_output->config) checks (config is always valid now)"
echo ""
echo "  5. Remove: All state->config_mtime references"
echo ""
echo "  6. Remove: All state->watch_mutex, state->watch_cond references"
echo ""
echo "  7. Remove: All state->reload_requested references (use next_requested instead)"
echo ""
echo "To restore from backup if needed:"
echo "  find src/neowalld -name '*.c.bak' -exec bash -c 'mv \"\$0\" \"\${0%.bak}\"' {} \;"
echo ""
echo "To remove backups after verifying:"
echo "  find src/neowalld -name '*.c.bak' -delete"
