# Scan cycle time: settle-verified motion + fresh-sample gating

Port of the cycle-time work qualified on the sibling **FireDog** project
(`docs/keithley-polling-design.md`, `docs/pi-hexapod-motion-design.md`,
`docs/xcan-scan-design.md` in that repo). FireDog measured 31.6 s → 18.2 s on a
coarse XCAN preset, 114 → 91 samples.

The change spans four layers, and it only works as a whole — each layer removes
the reason the layer above it had to wait.

```
DataStore            monotonic publish counter per channel
   ▲                 (proves a sample is new)
PIController         WaitForMotionCompletion = IsMoving false AND qONT true
   ▲                 (proves the mechanics are settled)
Keithley6482Adapter  fast mode: no filter, no auto-zero, no display
   ▲                 (makes samples arrive quickly enough to gate on)
CoarseToFineStrategy fresh-sample gate + structural trims
```

---

## 1. Why the old wait was wrong

The original scan did, after every move:

```
MoveRelative(axis, step)
SleepInterruptible(motionSettleTimeMs)   // 400 ms
value = GetMeasurement()                 // whatever is in DataStore right now
```

That one delay was paying for **two different things at once** — mechanical
settle *and* measurement freshness — blind, with no evidence that either had
happened. At ~90–115 samples per scan it dominated the cycle time.

Splitting the two:

| What | Was | Now |
|---|---|---|
| Mechanical settle | 400 ms blind sleep | **zero** — the move does not return until the controller reports on-target |
| Measurement freshness | same 400 ms sleep | wait for N new publishes on the channel |

## 2. Mechanical settle is now zero

`PIController::WaitForMotionCompletion` is two-stage
(`include/motions/PI/pi_controller.cpp`):

1. **Stage 1 — profile done.** `PI_IsMoving` false, bounded by `timeoutSeconds`.
2. **Stage 2 — settled.** `PI_qONT` true, bounded by `m_settleTimeoutMs` (5 s).

Both are required because they mean different things. `PI_IsMoving` going false
only says the trajectory generator finished; the platform can still be ringing.
`PI_qONT` is the controller's own settling logic — position inside the settling
window, for the settling time (SPA-tunable on the device). Measuring on
`IsMoving` alone samples the ringing.

Every blocking move funnels through this one function, so all of
`MoveToPosition`, `MoveRelative`, `HomeAxis`, `MoveToPositionAll`,
`MoveToPositionMultiAxis` and `MoveRelativeMultiAxis` got the fix at once.

**Contract for callers:** when a move returns success, the mechanics are
settled. Do not add a settle delay on top of it.

`SetSystemVelocity` also gained a retry on GCS2 error **1005**
(`CONTROLLER_BUSY`) — 100 ms steps up to 2 s — because the controller rejects a
velocity change while a profile is still executing, which is easy to hit right
after a move. Scans should still set velocity once up front.

`SetSettleTimeoutMs(0)` disables stage 2 and restores the old behaviour.

## 3. What is actually still stale: the instrument sample

The picoammeter integrates continuously. The sample sitting in `DataStore` when
a move finishes may have been integrating **while the stage was travelling** —
a physically mixed sample, not a measurement of the new position.

That is a **sample-count** problem, not a time problem, so it is solved with
evidence rather than a delay.

`DataStore` now keeps a monotonic per-channel publish counter
(`include/data/DataStore.h`):

```cpp
uint64_t GetUpdateCount(const std::string& channelId) const;
bool TryGetValue(const std::string& channelId, float& outValue, uint64_t& outCount) const;
```

Value and counter are read under the same lock, so `outCount` is exactly the
publish that produced `outValue`. The counter is **never reset** —
`ClearChannel`/`ClearAll` deliberately leave it alone so it cannot appear to run
backwards underneath a gate that is already waiting on it.

`ScanContext::GetFreshMeasurement()` gates on it:

```
n0 = publish counter now
if settleMinMs > 0: sleep(settleMinMs)        // precautionary floor
wait until counter >= n0 + freshSamples       // 10 ms poll, bounded
  or freshSampleTimeoutMs elapses             // ceiling -> warn once, use latest
return latest value
```

`freshSamples = 2` because publish #1 may straddle the move; **publish #2 is
guaranteed fully post-move**. The wait self-scales with the instrument: it never
costs more than the signal needs, and never less than one complete integration.

`freshSampleTimeoutMs` is a **ceiling, not a delay**. If the stream stalls the
gate degrades to "use the latest value" — exactly the old behaviour — and logs
one warning.

> **Threading note.** SMU samples reach `DataStore` through a *queued* Qt
> connection (`Application.cpp`, `SMUPollingManager::measurementReady`), so they
> are delivered on the Qt main thread. The scan runs on
> `AdaptiveScanController::m_scanThread`, so the main loop stays free to deliver
> them while the gate waits. If the UI thread is ever blocked hard during a
> scan, the gate will hit its ceiling every sample and warn.

## 4. Instrument side

`Keithley6482Adapter::ConfigureMeasurementModeInternal` FAST mode — the mode
alignment scans run on — now switches off the three per-read costs that are not
integration time: the repeating filter (`:AVER:STAT OFF`), auto-zero
(`:SYST:AZER OFF`) and front-panel refresh (`:DISP:ENAB OFF`). NPLC alone
governs the rate. Auto-range is left on and is now the dominant per-read cost;
a fixed range removes the range-hunt jitter when the signal band is known.

Two correctness fixes went in alongside:

* **`:INIT:CONT ON` removed from ULTRA_FAST.** Every read path uses `:READ?`,
  which is INIT+FETCh in one transaction. With continuous init already armed the
  6482 answers `:READ?` with error **−213 "Init ignored"** and the stream stalls
  at 0 Hz. Do not put it back.
* **`:FORM:ELEM` is `CURR1,CURR2` everywhere.** Connect and ACCURATE mode were
  setting `:FORM:ELEM READ` (a single value) while every parse path splits on
  commas, which starves channel 2. It is also no longer re-sent per read in
  `ReadBothChannels` — that cost one extra VISA transaction per sample.

`:DISP:ENAB ON` is restored on disconnect.

## 5. Structural trims in the walk

`CoarseToFineStrategy` (`include/motions/scanning/`):

| Trim | Parameter | Rule |
|---|---|---|
| Skip opposite | `skipOppositeDirection` | a first direction that walked uphill already **brackets** the peak, so the other way can only walk downhill — axis done |
| Direction memory | `directionMemory` | each axis starts in the direction that last improved it |
| Reuse sample | `reuseSample` | a point measured a moment ago, with no move since, is not measured again |
| Early exit | `exitThreshold` | first sample ≥ target ends the scan and **stays put** — no walk-back, no return to global peak |
| Trace | `traceEnabled` | one CSV line per sample, tagged `xcan,` |

The two fixed inter-pass / inter-axis `SleepInterruptible(100 ms)` calls are
gone — there is nothing left to wait for once moves are settle-verified.

### Adaptive step ladder

Signal level is a proxy for distance from the peak: for a Gaussian coupling
profile, distance ∝ √(log(peak/value)). So **log** signal zones map onto step
sizes. `[zoneLow, zoneHigh]` is split into `stepSizes.size()` equal log
sections; zone *i* owns `stepSizes[i]`, coarsest for the lowest zone. A pass
whose step is coarser than the current zone's is skipped.

The reference is `max(current sample, global peak)` — the best *reachable*
signal, deliberately not one raw sample, so a single noisy reading cannot bounce
the ladder back to coarse.

Enable with `ScanParameters::adaptiveLadder`, or by using
`ScanStrategyFactory::CreateAdaptiveStep()` (which is what `AdaptiveStepStrategy`
now is — it used to be a `return false` stub).

> **Tuning rule, learned on FireDog hardware:** `zoneLow` must equal the level
> the scan is **handed off** at, never below it. Set it too low and the first
> zone boundary lands under the handoff level, the coarse pass is skipped, and
> the fine walk gets captured by a micro-lobe. **The coarse pass is the
> side-lobe immunity — adaptive must not gate it off below the handoff level.**

---

## 6. Tuning and verification

Every trim is individually switchable, so they can be A/B'd one at a time on
hardware. Defaults are set in `ScanExecutor::buildScanParameters`.

| Parameter | Default | Restore old behaviour |
|---|---|---|
| `freshSamples` | 2 | `0` → blind `motionSettleTimeMs` sleep |
| `settleMinMs` | 0 | — (FireDog production value is 50) |
| `freshSampleTimeoutMs` | 300 | — (ceiling only) |
| `skipOppositeDirection` | true | `false` |
| `directionMemory` | true | `false` |
| `reuseSample` | true | `false` |
| `adaptiveLadder` | false | — |
| `exitThreshold` | 0.0 (off) | — |
| `PIController::SetSettleTimeoutMs` | 5000 | `0` → no qONT stage |

Two values were deliberately **left at bobcat's existing settings** because the
right value is a property of this machine, not FireDog's:

* **`significantDecreaseThreshold` = 0.05.** FireDog raised this to 0.15 after
  finding that noise dips reached ~6 % while true post-peak drops were 50 %+, so
  5 % was ending productive walks. Measure this machine's noise-dip depth before
  changing it.
* **`settleMinMs` = 0.** The fresh-sample gate proves the *instrument*
  integration is post-move, but it cannot see the analogue chain in front of it
  (photodiode + TIA), where a real electrical time constant can outlive the
  mechanical settle. FireDog runs 50 ms as a precaution, costing ~+0.5 s per
  scan. Raise it if fine passes look noisy.

### What to check on hardware

1. **qONT does not time out.** A `"timeout waiting for motion settle (qONT)"`
   warning means the settling window/time on the controller (SPA) is tighter
   than the platform can hold — tune the controller, do not remove the stage.
2. **The fresh-sample gate is not hitting its ceiling.** One
   `"Fresh-sample gate timed out"` warning per scan means the stream is slower
   than expected or stalled; check the Keithley poll rate first.
3. **Scan time and sample count**, against the pre-change baseline.
4. **Side lobes.** FireDog saw ~20 % of coarse runs settle on a repeatable side
   lobe; the mitigation is a recipe retry guard (loop while the result is below
   a floor, max 3 iterations, moving back to the start inside the loop), not
   settle tuning — the stall rate there was settle-independent.
