-- update_edge_transfer_ref.lua
--
-- Replaces the reference and target P1 / P2 (gantry-main XYZ) of an
-- Edge Transfer training set by pulling them from saved named positions
-- on the set's measure device (gantry-main). The next retrace run will
-- auto-nav to these new positions.
--
-- Load the "Stage main position" product before running so the set and
-- the named positions resolve correctly.

local SET_NAME = "MOE Set up 260422"

-- Reference edge — pulled from these named positions.
local P1_NAMED = "SEE IR SIPIC3 A1"
local P2_NAMED = "SEE IR SIPIC3 A2"

-- Target edge — pulled from these named positions.
local T1_NAMED = "Target MOE A1"
local T2_NAMED = "Target MOE A2"

-- ---------------------------------------------------------------------------

log.info("Updating '%s' edge points from named positions...", SET_NAME)
log.info("  Ref P1 <- '%s'", P1_NAMED)
log.info("  Ref P2 <- '%s'", P2_NAMED)
log.info("  Tgt P1 <- '%s'", T1_NAMED)
log.info("  Tgt P2 <- '%s'", T2_NAMED)

local ok = _dispatch("edgetransfer", "UpdateEdgePoints", {
    training_set_name = SET_NAME,
    p1_named          = P1_NAMED,
    p2_named          = P2_NAMED,
    t1_named          = T1_NAMED,
    t2_named          = T2_NAMED,
})
if not ok then
    log.error("UpdateEdgePoints failed for '%s'", SET_NAME)
    error("UpdateEdgePoints failed")
end

log.info("Saved. Run retrace on '%s' to navigate to the new edge points.", SET_NAME)
