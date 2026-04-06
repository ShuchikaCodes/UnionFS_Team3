#!/bin/bash
# ─── Mini-UnionFS Test Suite ─────────────────────────────────────────────────

FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0

pass() { echo -e "  ${GREEN}✔ PASSED${NC}"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}✘ FAILED${NC}"; FAIL=$((FAIL+1)); }

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "   Mini-UnionFS Test Suite"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ─── Setup ───────────────────────────────────────────────────────────────────
rm -rf "$TEST_DIR"
mkdir -p "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"

# Seed lower with test data
echo "base_only_content"  > "$LOWER_DIR/base.txt"
echo "to_be_deleted"      > "$LOWER_DIR/delete_me.txt"
echo "lower_version"      > "$LOWER_DIR/shared.txt"
echo "upper_version"      > "$UPPER_DIR/shared.txt"   # upper wins
mkdir -p "$LOWER_DIR/lower_subdir"
echo "nested"             > "$LOWER_DIR/lower_subdir/nested.txt"

# Mount
$FUSE_BINARY "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"
sleep 1

# ─── TEST 1: Layer Visibility ─────────────────────────────────────────────────
echo -n "Test 1: Layer visibility (lower file visible in merged view)..."
if grep -q "base_only_content" "$MOUNT_DIR/base.txt" 2>/dev/null; then
    pass
else
    fail
fi

# ─── TEST 2: Copy-on-Write ────────────────────────────────────────────────────
echo -n "Test 2: Copy-on-Write (modify lower file via mount)..."
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
UPPER_HAS=$(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null || echo 0)
LOWER_HAS=$(grep -c "modified_content" "$LOWER_DIR/base.txt" 2>/dev/null || echo 0)
MOUNT_HAS=$(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null || echo 0)

if [ "$UPPER_HAS" -eq 1 ] && [ "$LOWER_HAS" -eq 0 ] && [ "$MOUNT_HAS" -eq 1 ]; then
    pass
else
    fail
fi

# ─── TEST 3: Whiteout (spec requirement) ──────────────────────────────────────
echo -n "Test 3: Whiteout — delete lower file, .wh. marker created..."
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
WH_EXISTS=0
LOWER_INTACT=0
HIDDEN_FROM_MOUNT=0
[ -f "$UPPER_DIR/.wh.delete_me.txt" ]    && WH_EXISTS=1
[ -f "$LOWER_DIR/delete_me.txt" ]        && LOWER_INTACT=1
[ ! -f "$MOUNT_DIR/delete_me.txt" ]      && HIDDEN_FROM_MOUNT=1

if [ $WH_EXISTS -eq 1 ] && [ $LOWER_INTACT -eq 1 ] && [ $HIDDEN_FROM_MOUNT -eq 1 ]; then
    pass
else
    echo -e " ${RED}✘ FAILED${NC} (wh=$WH_EXISTS lower_intact=$LOWER_INTACT hidden=$HIDDEN_FROM_MOUNT)"
    FAIL=$((FAIL+1))
fi

# ─── TEST 4: New File Creation (extension) ────────────────────────────────────
echo -n "Test 4: Create new file — appears in upper only, not lower..."
echo "brand_new" > "$MOUNT_DIR/new_file.txt" 2>/dev/null
UPPER_NEW=0
LOWER_NEW=0
[ -f "$UPPER_DIR/new_file.txt" ]    && UPPER_NEW=1
[ -f "$LOWER_DIR/new_file.txt" ]    && LOWER_NEW=1

if [ $UPPER_NEW -eq 1 ] && [ $LOWER_NEW -eq 0 ]; then
    pass
else
    fail
fi

# ─── TEST 5: Upper Layer Override (extension) ─────────────────────────────────
echo -n "Test 5: Upper layer overrides lower for same filename..."
# shared.txt exists in both; upper has "upper_version"
if grep -q "upper_version" "$MOUNT_DIR/shared.txt" 2>/dev/null; then
    pass
else
    fail
fi

# ─── TEST 6: mkdir in mount → upper only (extension) ─────────────────────────
echo -n "Test 6: mkdir via mount creates dir in upper only..."
mkdir "$MOUNT_DIR/newdir" 2>/dev/null
UPPER_DIR_EXISTS=0
LOWER_DIR_EXISTS=0
[ -d "$UPPER_DIR/newdir" ]   && UPPER_DIR_EXISTS=1
[ -d "$LOWER_DIR/newdir" ]   && LOWER_DIR_EXISTS=1

if [ $UPPER_DIR_EXISTS -eq 1 ] && [ $LOWER_DIR_EXISTS -eq 0 ]; then
    pass
else
    fail
fi

# ─── Teardown ─────────────────────────────────────────────────────────────────
fusermount -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
rm -rf "$TEST_DIR"

# ─── Results ──────────────────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "  Results:  ${GREEN}$PASS passed${NC}  /  ${RED}$FAIL failed${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

[ $FAIL -eq 0 ] && exit 0 || exit 1
