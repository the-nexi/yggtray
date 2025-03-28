#!/bin/sh

# Usage: update-peers.sh peer-list-file
# This script updates the peers section in yggdrasil.conf with the provided peer list

set -e  # Exit immediately if a command fails

VERBOSE_MODE=0

# Parse arguments
if [ "$1" = "--verbose" ] || [ "$1" = "-v" ]; then
    VERBOSE_MODE=1
    shift
fi

if [ -z "$1" ]; then
    echo "Usage: $0 peer-list-file" >&2
    exit 1
fi

PEERS_FILE="$1"
CONFIG_FILE="/etc/yggdrasil.conf"
BACKUP_FILE="${CONFIG_FILE}.bckp"
TEMP_FILE=$(mktemp)

if [ ! -f "$PEERS_FILE" ]; then
    echo "Error: Peer list file not found: $PEERS_FILE" >&2
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "Error: Yggdrasil config not found: $CONFIG_FILE" >&2
    exit 1
fi

# Clean up temp file on exit
trap 'rm -f "$TEMP_FILE" "$TEMP_FILE.peers"' EXIT

# Create a backup of the original config file
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Creating backup of configuration file to $BACKUP_FILE" >&2
cp "$CONFIG_FILE" "$BACKUP_FILE"

# Process and clean up the peers file - trim whitespace, ensure proper formatting
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Processing peer list from $PEERS_FILE..." >&2
{
    echo "Peers: ["
    # Process each line, trim whitespace, ensure proper format
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip empty lines
        [ -z "$line" ] && continue
        
        # Trim whitespace
        line=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
        
        # Skip if empty after trimming
        [ -z "$line" ] && continue
        
        # Output the peer on its own line without commas or extra indentation
        echo "$line"
    done < "$PEERS_FILE"
    echo "]"
} > "$TEMP_FILE.peers"

# Check if the file has any peers
if ! grep -q '[^[:space:]]' "$TEMP_FILE.peers"; then
    echo "Error: No valid peers found in input file" >&2
    exit 1
fi

# Process the config file using awk
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Reading current configuration from $CONFIG_FILE..." >&2
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Updating configuration..." >&2
awk '
  BEGIN { in_peers_section = 0; }
  
  # When we find the Peers section opening
  /^[[:space:]]*Peers[[:space:]]*:[[:space:]]*\[/ {
    in_peers_section = 1;
    system("cat \"'"$TEMP_FILE.peers"'\"");
    next;
  }
  
  # When we find the Peers section closing
  in_peers_section && /^[[:space:]]*\]/ {
    in_peers_section = 0;
    next;
  }
  
  # Print all lines that are not part of the Peers section
  !in_peers_section {
    print;
  }
' "$CONFIG_FILE" > "$TEMP_FILE"

# Verify the generated config is valid and not empty
if [ ! -s "$TEMP_FILE" ]; then
    echo "Error: Generated configuration is empty" >&2
    exit 1
fi

# Check if Peers section exists in the output
if ! grep -q "Peers:" "$TEMP_FILE"; then
    echo "Error: Peers section missing from generated configuration" >&2
    exit 1
fi

# Replace the original config with the new one
cp "$TEMP_FILE" "$CONFIG_FILE"
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Configuration file updated successfully" >&2
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Backup saved to $BACKUP_FILE" >&2

echo "Peers updated successfully in $CONFIG_FILE"
echo "Backup of original configuration saved to $BACKUP_FILE"
[ "$VERBOSE_MODE" = "1" ] && echo "Debug: Operation completed" >&2

# Explicitly exit with success code
exit 0
