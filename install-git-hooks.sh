#!/bin/bash
HOOK=".git/hooks/pre-push"

cat > "$HOOK" << 'EOF'
#!/bin/bash

# Get the tag being pushed
TAGS=$(git tag --points-at HEAD)

# Exit if no tags are being pushed
if [ -z "$TAGS" ]; then
    exit 0
fi

# Extract the version from CMakeLists.txt
CMAKE_VERSION=$(grep -Po 'set\(YGGTRAY_VERSION "\K[0-9]+\.[0-9]+\.[0-9]+(?=")' CMakeLists.txt)

if [ -z "$CMAKE_VERSION" ]; then
    echo "Error: Could not find YGGTRAY_VERSION in CMakeLists.txt."
    exit 1
fi

# Verify each tag matches the version in CMakeLists.txt
for TAG in $TAGS; do
    if [ "$TAG" != "$CMAKE_VERSION" ]; then
        echo "Error: Git tag ($TAG) does not match YGGTRAY_VERSION ($CMAKE_VERSION) in CMakeLists.txt."
        exit 1
    fi
done

echo "Version check passed: Git tag matches YGGTRAY_VERSION in CMakeLists.txt."
exit 0
EOF

chmod +x "$HOOK"
echo "Pre-push hook installed successfully."

