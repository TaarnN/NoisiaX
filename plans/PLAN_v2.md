# NoisiaX Plan v2: Deterministic Agent Simulation and Seeded Butterfly Effects

## Summary
- Target v2 is the next step from the current v1 core toward the user’s intended simulation: a small real-world market with 20 shops, 10 people, walking, talking, observing, buying, seeded randomness, and causal ripple effects.
- v2 will keep the principle from [noisiax_design_full.md](/Users/taarn/Documents/PGM/Prog-Languages/NoisiaX/noisiax_design_full.md): deterministic core first, stochastic behavior as an explicit layer, static backbone plus bounded dynamic overlay, and push invalidation / pull recompute where useful.
- The user’s seed concept will be adopted as: one `master_seed` controls every random decision and every accidental branch, while the trace records exactly which seeded draw caused each outcome. The seed is not embedded inside each parameter; it is a centralized deterministic source used by decisions, events, and influence propagation.
- v2 will not aim for millions of people or a full city. It will aim for a high-quality small-world agent simulation that can explain: “A saw B buy pork skewer, A’s desire changed, seeded association drift made A consider boiled corn, and A bought it because price/distance/queue/random draw favored it.”

## Key Changes

### 1. Detailed Run Output and Trace
- Add a new detailed result path while keeping existing `run_scenario(...) -> ScenarioReport` backward-compatible.
- Add public API:
  - `RunOptions { max_time, max_events, trace_level, include_final_state, include_causal_graph }`
  - `RunResult { ScenarioReport report, FinalStateSnapshot final_state, EventTrace[] events, DecisionTrace[] decisions, StateChangeTrace[] state_changes }`
  - `run_scenario_detailed(filepath, RunOptions)` and `run_scenario_detailed(CompiledScenario, RunOptions)`
- CLI additions:
  - `noisiax run <scenario.yaml> --trace events --output result.json`
  - `noisiax run <scenario.yaml> --trace full --output result.json`
- Trace levels:
  - `none`: current lightweight behavior
  - `events`: event order, event payload, time, affected actor/shop/item
  - `decisions`: event trace plus decision candidates, weights, selected option, seed draw
  - `full`: event trace, decisions, state changes, causal links, final world snapshot
- Final output must include all important scenario results: each person’s path, purchases, money spent, time walking, time talking, time queueing, observed purchases, influence received, and causal parent event IDs.

### 2. Deterministic Randomness Layer
- Implement a custom deterministic RNG, not `std::uniform_*_distribution`, to avoid platform-dependent behavior.
- Use a counter-based `SplitMix64`-style generator:
  - Inputs: `master_seed`, `scenario_id`, `stream_key`, `draw_index`
  - Outputs: stable `u64`, stable `double in [0,1)`, stable integer ranges
- Add named random streams:
  - `movement:<agent_id>`
  - `decision:<agent_id>`
  - `social:<observer_id>:<source_id>`
  - `shop_service:<shop_id>`
  - `conversation:<agent_a>:<agent_b>`
- Every random draw must be traceable with:
  - stream key
  - draw index
  - raw `u64`
  - normalized value
  - final interpreted result
- Seed semantics:
  - Same scenario + same seed + same event order = identical trace and final state.
  - Different seed = different accidental branches, while rules and constraints remain the same.
  - Butterfly effect emerges because one seeded branch can schedule new events, which expose other agents, which create further decisions.

### 3. Agent Layer for Real-World Small Simulations
- Add an optional v2 schema section: `agent_layer`.
- `agent_layer` contains:
  - `world`: duration, time unit, map bounds, default walking speed, max event count
  - `locations`: typed places with x/y coordinates
  - `items`: purchasable things, tags, base appeal, category
  - `shops`: location, inventory, price, stock, service time, queue capacity
  - `agents`: start location, budget, movement speed, traits, preferences, memory
  - `policies`: named behavior policies for movement, observation, conversation, and purchase
- Keep v1 `entities`, `variables`, `dependency_edges`, `constraints`, `events`, and `evaluation_criteria`; v2 agent data compiles into specialized runtime structures.
- Compile IDs into integer handles for performance. No string lookups in the hot simulation loop.
- Store agent runtime state in SoA-style buffers:
  - location index
  - destination index
  - budget
  - hunger/desire
  - current intent item index
  - remaining conversation time
  - walking/talking/queueing/buying counters
  - memory slots for recent observations and social influence

### 4. Dynamic Event Runtime
- Replace the current event callback shape with a `RuntimeContext` that can schedule more events during event handling.
- Supported v2 event types:
  - `SIM_START`
  - `MOVE_START`
  - `MOVE_ARRIVE`
  - `OBSERVE_PURCHASE`
  - `TALK_START`
  - `TALK_END`
  - `DECIDE_PURCHASE`
  - `QUEUE_ENTER`
  - `PURCHASE`
  - `INFLUENCE_DECAY`
  - `SIM_END`
- Event ordering remains deterministic:
  - ascending timestamp
  - descending priority
  - lexicographic event handle
  - deterministic generated ordinal for dynamic children
- Add hard safety limits:
  - `max_events` default: `100000`
  - `max_time` default: scenario duration
  - fail with a clear report if event generation exceeds limits
- Dynamic overlay rule:
  - v2 does not allow arbitrary runtime graph mutation yet.
  - It allows bounded temporary influence edges through event-created memory records, such as “B’s pork skewer purchase influenced A for 5 minutes.”
  - This satisfies the user’s market butterfly-effect goal without opening unbounded dynamic graph complexity.

### 5. Decision and Influence Model
- Implement a deterministic rule-based decision engine.
- A purchase decision evaluates candidate items using this fixed formula:
  - `weight = max(0.001, base_appeal * preference + hunger_bonus + social_signal + tag_association + random_jitter - price_penalty - distance_penalty - queue_penalty)`
- Select item by deterministic weighted choice using the agent’s decision RNG stream.
- Social influence:
  - Seeing another person buy an item creates an observation memory.
  - Exact-item influence increases desire for that item.
  - Tag association can shift desire to related items.
  - Random association drift can produce accidental alternatives, such as seeing pork skewer but considering boiled corn because both are “street_food” or “handheld_snack.”
- Conversation:
  - Talking consumes time.
  - Conversation can transmit a recent purchase or preference signal.
  - Conversation duration is deterministic but seed-influenced.
- Movement:
  - Travel time = distance / walking speed + deterministic congestion jitter.
  - Agents choose shops based on item availability, distance, price, queue, and current intent.
- Every decision must produce a `DecisionTrace` explaining candidates, weights, random draw, selected result, and causal parent events.

### 6. Validation and Compilation
- Extend validation for `agent_layer`:
  - all referenced locations, shops, items, agents, and policies must exist
  - prices, stock, service times, speeds, budgets, and durations must be non-negative
  - every shop must have a valid location
  - every inventory item must reference a valid item
  - every agent must start at a valid location
  - all policy IDs must resolve
  - `max_events` and `duration` must be finite and positive
- Compile `agent_layer` into:
  - ID-to-index maps
  - item tag tables
  - shop inventory tables
  - agent preference matrices
  - initial event queue
  - deterministic RNG stream descriptors
- Keep v1 scenarios valid. A scenario without `agent_layer` runs through the existing v1 path.

### 7. Market Scenario Acceptance Target
- Add a canonical v2 scenario: `market_butterfly_v2.yaml`.
- Scenario content:
  - 20 shops
  - 10 people
  - at least 12 items, including `pork_skewer` and `boiled_corn`
  - market map with shop coordinates
  - agents with different budgets, walking speeds, preferences, hunger levels, and social susceptibility
  - initial events that place people into the market and start movement/decision cycles
- Expected story capability:
  - B buys pork skewer.
  - A observes B.
  - A’s desire for pork skewer or related street food changes.
  - Seeded random association may make A choose boiled corn instead.
  - A’s purchase can influence C later.
  - The output JSON can explain the complete causal chain.

## Test Plan
- RNG tests:
  - same seed and same stream key produce identical draws
  - different stream keys produce independent deterministic draws
  - weighted choice is stable and reproducible
- Scheduler tests:
  - static and dynamically generated events sort deterministically
  - child events get stable handles
  - `max_events` stops runaway simulations cleanly
- Agent runtime tests:
  - movement time is computed from distance and speed
  - purchase reduces budget and stock
  - unavailable or unaffordable items are rejected
  - queue and conversation time are counted correctly
- Decision tests:
  - observing a purchase changes later candidate weights
  - association drift can select a related item with a known seed
  - full decision trace records all weights and random draws
- Integration tests:
  - run `market_butterfly_v2.yaml` twice with the same seed and assert identical fingerprint, event trace, and final state
  - change only the seed and assert at least one decision path changes
  - checkpoint/resume produces the same final result as uninterrupted run
- CLI tests:
  - `validate`, `compile`, and `run --trace full --output result.json` succeed for the market scenario
  - result JSON includes agents, shops, purchases, events, decisions, causal links, and final statistics

## Assumptions and Defaults
- v2 remains C++20, single-process, CPU-only.
- YAML remains the authoring format.
- No AI model calls, internet data fetching, or real-world calibration in v2.
- The first real-world target is a small market simulation, not a large internet/social graph.
- Randomness is allowed only through the deterministic RNG layer and must always be traceable.
- Dynamic graph mutation is deferred; v2 uses bounded temporary influence records instead.
- Performance priority: compile strings to integer handles, keep runtime state in SoA buffers, make tracing optional, and avoid allocations in hot event handling.
