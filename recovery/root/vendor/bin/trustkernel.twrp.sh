#!/system/bin/sh

# Trustkernel TA setup for TWRP recovery
# Copies TAs to all locations where TKCore might look
# Fixed version that copies FROM recovery ramdisk TO expected locations

TA_SOURCE="/system/app/t6"
SYSTEM_TA="/system/app/t6"
DATA_TA="/data/tee/t6"
PERSIST_TA="/mnt/vendor/persist/t6"
PERSIST_TA_TWRP="/mnt/vendor/persist/t6_twrp"
PROTECT_TEE="/mnt/vendor/protect_f/tee"
PROTECT_TEE_TWRP="/mnt/vendor/protect_f/tee_twrp"
PROTECT_TEE_SHORT="/protect_f/tee"

log -p i -t trustkernel_twrp "=== Trustkernel TA Setup Started ==="

# Function to safely copy TAs
copy_tas() {
    local src=$1
    local dst=$2
    local name=$3

    if [ ! -d "$src" ]; then
        log -p w -t trustkernel_twrp "Source not found: $src"
        return 1
    fi

    # Create destination
    mkdir -p "$dst" 2>/dev/null

    # Copy TA files
    if ls "$src"/*.ta >/dev/null 2>&1; then
        cp -f "$src"/*.ta "$dst/" 2>/dev/null
        chmod 644 "$dst"/*.ta 2>/dev/null
        local count=$(ls "$dst"/*.ta 2>/dev/null | wc -l)
        log -p i -t trustkernel_twrp "Copied $count TAs to $name"
        return 0
    else
        log -p e -t trustkernel_twrp "No TAs in $src"
        return 1
    fi
}

# Verify source TAs exist
if [ ! -d "$TA_SOURCE" ]; then
    log -p e -t trustkernel_twrp "CRITICAL: Source TAs not found in $TA_SOURCE"
    exit 1
fi

ta_count=$(ls "$TA_SOURCE"/*.ta 2>/dev/null | wc -l)
log -p i -t trustkernel_twrp "Found $ta_count TAs in recovery ramdisk"

# Copy to legacy locations for compatibility with old script expectations
# Try to use existing TAs from persist if they exist (from stock ROM)
if [ -d "$PERSIST_TA" ] && [ "$(ls -A $PERSIST_TA/*.ta 2>/dev/null | wc -l)" -gt 0 ]; then
    log -p i -t trustkernel_twrp "Using existing TAs from persist"
    mkdir -p "$PERSIST_TA_TWRP"
    rm -rf "$PERSIST_TA_TWRP"/*
    cp -rfp "$PERSIST_TA"/* "$PERSIST_TA_TWRP/" 2>/dev/null
else
    log -p w -t trustkernel_twrp "No TAs in persist, copying from ramdisk"
    copy_tas "$TA_SOURCE" "$PERSIST_TA" "persist/t6"
    copy_tas "$TA_SOURCE" "$PERSIST_TA_TWRP" "persist/t6_twrp"
fi

# Same for protect partition
if [ -d "$PROTECT_TEE" ] && [ "$(ls -A $PROTECT_TEE/*.ta 2>/dev/null | wc -l)" -gt 0 ]; then
    log -p i -t trustkernel_twrp "Using existing TAs from protect_f"
    mkdir -p "$PROTECT_TEE_TWRP"
    rm -rf "$PROTECT_TEE_TWRP"/*
    cp -rfp "$PROTECT_TEE"/* "$PROTECT_TEE_TWRP/" 2>/dev/null
else
    log -p w -t trustkernel_twrp "No TAs in protect_f, copying from ramdisk"
    copy_tas "$TA_SOURCE" "$PROTECT_TEE" "protect_f/tee"
    copy_tas "$TA_SOURCE" "$PROTECT_TEE_TWRP" "protect_f/tee_twrp"
fi

# Copy to /protect_f/tee (without /mnt/vendor prefix)
copy_tas "$TA_SOURCE" "$PROTECT_TEE_SHORT" "protect_f_short/tee"

# Verify PRIMARY location has TAs (already copied by init script)
if [ -d "$SYSTEM_TA" ]; then
    sys_count=$(ls "$SYSTEM_TA"/*.ta 2>/dev/null | wc -l)
    log -p i -t trustkernel_twrp "PRIMARY location has $sys_count TAs"
else
    log -p e -t trustkernel_twrp "WARNING: PRIMARY location missing!"
fi

# Verify SECONDARY location has TAs (already copied by init script)
if [ -d "$DATA_TA" ]; then
    data_count=$(ls "$DATA_TA"/*.ta 2>/dev/null | wc -l)
    log -p i -t trustkernel_twrp "SECONDARY location has $data_count TAs"
else
    log -p e -t trustkernel_twrp "WARNING: SECONDARY location missing!"
fi

# Final verification - list all TA locations
log -p i -t trustkernel_twrp "=== TA Location Summary ==="
for ta_dir in "$SYSTEM_TA" "$DATA_TA" "$PERSIST_TA" "$PERSIST_TA_TWRP" "$PROTECT_TEE" "$PROTECT_TEE_TWRP" "$PROTECT_TEE_SHORT"; do
    if [ -d "$ta_dir" ]; then
        count=$(ls "$ta_dir"/*.ta 2>/dev/null | wc -l)
        log -p i -t trustkernel_twrp "  $ta_dir: $count TAs"
    fi
done

log -p i -t trustkernel_twrp "=== Trustkernel TA Setup Complete ==="
exit 0
