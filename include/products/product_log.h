// product_log.h - verbosity gate for the product position/JSON modules
#pragma once

namespace ProductLog {

// Per-item logging gate for product loading.
//
// Loading a product emits two console lines PER POSITION - one from
// ProductJsonLoader and one from ProductPositionManager. A 61-position
// product is therefore 122 flushed lines, and RecipePage::showEvent used to
// reload on every activation of the page. At roughly 0.4 ms per flushed line
// that is ~50 ms of console I/O, but the real cost is that it buries every
// other message in the log.
//
// Per-load summary lines (how many positions, which file, warnings, errors)
// are NOT gated by this - they stay visible. Only the per-position detail is.
//
// Flip to true and rebuild when debugging product JSON parsing.
constexpr bool kVerbosePositions = false;

}  // namespace ProductLog
