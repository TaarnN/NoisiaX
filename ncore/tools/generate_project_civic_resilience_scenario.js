#!/usr/bin/env node

const fs = require("fs");
const path = require("path");

const repoRoot = path.resolve(__dirname, "..");
const scenariosDir = path.join(repoRoot, "scenarios");

const DURATION = 45.0;
const MAX_EVENTS = 12000;

function q(value) {
  return JSON.stringify(value);
}

function fixed(value, digits = 3) {
  return Number(value).toFixed(digits);
}

function residentId(i) {
  return `r${String(i).padStart(3, "0")}`;
}

function facilityId(i) {
  return `facility_${String(i).padStart(2, "0")}`;
}

function districtForResident(i) {
  return Math.floor((i - 1) / 10) + 1;
}

function roleFor(i) {
  const roles = [
    "nurse",
    "teacher",
    "elder",
    "student",
    "vendor",
    "utility_worker",
    "parent",
    "farmer",
    "driver",
    "volunteer",
  ];
  return roles[(i - 1) % roles.length];
}

function ageFor(i, role) {
  if (role === "student") return 12 + ((i * 3) % 7);
  if (role === "elder") return 63 + ((i * 5) % 22);
  if (role === "teacher" || role === "nurse") return 27 + ((i * 4) % 30);
  return 21 + ((i * 7) % 43);
}

function incomeFor(i, role) {
  const base = {
    nurse: 860,
    teacher: 780,
    elder: 360,
    student: 90,
    vendor: 680,
    utility_worker: 820,
    parent: 620,
    farmer: 540,
    driver: 600,
    volunteer: 520,
  }[role];
  return Math.max(80, base + ((i * 37) % 220) - 95);
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function worldBlock(indent = "    ") {
  return [
    `${indent}world:`,
    `${indent}  duration: ${fixed(DURATION, 1)}`,
    `${indent}  time_unit: "days"`,
    `${indent}  max_event_count: ${MAX_EVENTS}`,
    `${indent}  tick_interval: 1.0`,
  ];
}

function writeFile(name, lines) {
  fs.writeFileSync(path.join(scenariosDir, name), `${lines.join("\n")}\n`);
}

function baseHeader(id, schemaVersion, goal, assumptions) {
  const lines = [
    `scenario_id: ${q(id)}`,
    `schema_version: ${q(schemaVersion)}`,
    `master_seed: 20260506`,
    `goal_statement: |`,
    `  ${goal}`,
    `assumptions:`,
  ];
  for (const assumption of assumptions) {
    lines.push(
      `  - assumption_id: ${q(assumption.id)}`,
      `    category: ${q(assumption.category)}`,
      `    description: ${q(assumption.description)}`,
      `    rationale: ${q(assumption.rationale)}`,
      `    confidence_level: REJECT`,
    );
  }
  return lines;
}

function generatePopulationFragment() {
  const lines = baseHeader(
    "project_civic_resilience_population",
    "1.0.0",
    "Composable population module for the civic resilience project scenario.",
    [
      {
        id: "assume_population_panel",
        category: "project_scope",
        description: "Sixty residents across six districts provide enough heterogeneous state for non-trivial cascade behavior.",
        rationale: "The parent v5 scenario imports and namespaces this module through v4 composition.",
      },
    ],
  );

  lines.push("", "typed_layer:", ...worldBlock("  "));
  lines.push(
    "",
    "  component_types:",
    "    - component_type_id: \"ResidentProfile\"",
    "      fields:",
    "        household: INTEGER",
    "        district: INTEGER",
    "        age: INTEGER",
    "        role: STRING",
    "        income: FLOAT",
    "        care_load: FLOAT",
    "        mobility: FLOAT",
    "        preparedness: FLOAT",
    "    - component_type_id: \"CivicState\"",
    "      fields:",
    "        safety: FLOAT",
    "        power: FLOAT",
    "        water: FLOAT",
    "        food: FLOAT",
    "        medical_access: FLOAT",
    "        trust: FLOAT",
    "        rumor_belief: FLOAT",
    "        stress: FLOAT",
    "        volunteer_capacity: FLOAT",
    "        price_pressure: FLOAT",
    "        resilience_score: FLOAT",
    "",
    "  entity_types:",
    "    - entity_type_id: \"resident\"",
    "      components: [\"ResidentProfile\", \"CivicState\"]",
    "",
    "  relation_types: []",
    "  event_types: []",
    "",
    "  entities:",
  );

  for (let i = 1; i <= 60; i += 1) {
    const district = districtForResident(i);
    const role = roleFor(i);
    const income = incomeFor(i, role);
    const careLoad = clamp(0.18 + ((i * 11) % 7) * 0.07 + (role === "elder" ? 0.2 : 0), 0.05, 0.9);
    const mobility = clamp(0.92 - (role === "elder" ? 0.38 : 0) - (role === "student" ? 0.18 : 0) + ((i * 5) % 9) * 0.015, 0.18, 0.98);
    const preparedness = clamp(0.32 + district * 0.035 + ((i * 17) % 13) * 0.018 + (role === "volunteer" ? 0.16 : 0), 0.12, 0.9);
    const trust = clamp(0.48 + ((i * 19) % 17) * 0.017 - district * 0.018, 0.22, 0.84);
    const rumor = clamp(0.12 + ((i * 23) % 19) * 0.012 + (role === "elder" ? 0.04 : 0), 0.04, 0.48);
    const stress = clamp(0.22 + district * 0.025 + careLoad * 0.12 - preparedness * 0.08, 0.08, 0.62);
    const volunteerCapacity = clamp(0.16 + preparedness * 0.52 + (role === "volunteer" || role === "utility_worker" ? 0.22 : 0), 0.05, 0.92);
    const food = clamp(0.72 + ((i * 7) % 9) * 0.018 - district * 0.015, 0.45, 0.95);
    const water = clamp(0.78 + ((i * 3) % 8) * 0.015 - district * 0.012, 0.48, 0.96);
    const power = clamp(0.82 - district * 0.018 + ((i * 5) % 11) * 0.012, 0.52, 0.96);
    const medicalAccess = clamp(0.55 + (role === "nurse" ? 0.14 : 0) - district * 0.018 + ((i * 13) % 10) * 0.018, 0.28, 0.9);
    const resilience = clamp((food + water + power + medicalAccess + trust + volunteerCapacity + preparedness) / 7 - stress * 0.25 - rumor * 0.12, 0.05, 0.92);

    lines.push(
      `    - entity_id: ${q(residentId(i))}`,
      `      entity_type: "resident"`,
      `      components:`,
      `        ResidentProfile:`,
      `          household: ${Math.ceil(i / 3)}`,
      `          district: ${district}`,
      `          age: ${ageFor(i, role)}`,
      `          role: ${q(role)}`,
      `          income: ${fixed(income, 1)}`,
      `          care_load: ${fixed(careLoad)}`,
      `          mobility: ${fixed(mobility)}`,
      `          preparedness: ${fixed(preparedness)}`,
      `        CivicState:`,
      `          safety: ${fixed(clamp(0.72 - stress * 0.2 + preparedness * 0.08, 0.3, 0.94))}`,
      `          power: ${fixed(power)}`,
      `          water: ${fixed(water)}`,
      `          food: ${fixed(food)}`,
      `          medical_access: ${fixed(medicalAccess)}`,
      `          trust: ${fixed(trust)}`,
      `          rumor_belief: ${fixed(rumor)}`,
      `          stress: ${fixed(stress)}`,
      `          volunteer_capacity: ${fixed(volunteerCapacity)}`,
      `          price_pressure: ${fixed(0.18 + district * 0.035 + ((i * 29) % 7) * 0.012)}`,
      `          resilience_score: ${fixed(resilience)}`,
    );
  }

  lines.push("", "  relations: []", "  initial_events: []", "  systems: []");
  writeFile("project_civic_resilience_population.yaml", lines);
}

function generateInfrastructureFragment() {
  const lines = baseHeader(
    "project_civic_resilience_infrastructure",
    "1.0.0",
    "Composable critical-infrastructure module for the civic resilience project scenario.",
    [
      {
        id: "assume_infrastructure_panel",
        category: "project_scope",
        description: "Twelve district facilities model power, water, health, shelter, and logistics stress.",
        rationale: "The parent scenario links these facilities to residents and command cells.",
      },
    ],
  );

  lines.push("", "typed_layer:", ...worldBlock("  "));
  lines.push(
    "",
    "  component_types:",
    "    - component_type_id: \"FacilityStatus\"",
    "      fields:",
    "        district: INTEGER",
    "        kind: STRING",
    "        capacity: FLOAT",
    "        reliability: FLOAT",
    "        load: FLOAT",
    "        backlog: FLOAT",
    "        stock: FLOAT",
    "        damage: FLOAT",
    "        price_index: FLOAT",
    "",
    "  entity_types:",
    "    - entity_type_id: \"facility\"",
    "      components: [\"FacilityStatus\"]",
    "",
    "  relation_types: []",
    "  event_types: []",
    "",
    "  entities:",
  );

  const kinds = ["substation", "water_pump", "clinic", "shelter", "logistics_depot", "cell_tower"];
  for (let i = 1; i <= 12; i += 1) {
    const district = Math.floor((i - 1) / 2) + 1;
    const kind = kinds[(i - 1) % kinds.length];
    const capacity = 92 + district * 11 + ((i * 17) % 24);
    const reliability = clamp(0.91 - district * 0.028 - (i % 2) * 0.025, 0.58, 0.95);
    const stock = clamp(0.72 - district * 0.026 + ((i * 7) % 8) * 0.025, 0.38, 0.92);
    lines.push(
      `    - entity_id: ${q(facilityId(i))}`,
      `      entity_type: "facility"`,
      `      components:`,
      `        FacilityStatus:`,
      `          district: ${district}`,
      `          kind: ${q(kind)}`,
      `          capacity: ${fixed(capacity, 1)}`,
      `          reliability: ${fixed(reliability)}`,
      `          load: ${fixed(6 + district * 2 + (i % 4), 1)}`,
      `          backlog: ${fixed(0.45 + district * 0.12 + (i % 3) * 0.09)}`,
      `          stock: ${fixed(stock)}`,
      `          damage: ${fixed(0.05 + district * 0.015 + (i % 2) * 0.018)}`,
      `          price_index: ${fixed(0.20 + district * 0.045 + (i % 3) * 0.030)}`,
    );
  }

  lines.push("", "  relations: []", "  initial_events: []", "  systems: []");
  writeFile("project_civic_resilience_infrastructure.yaml", lines);
}

function generateGovernanceFragment() {
  const lines = baseHeader(
    "project_civic_resilience_governance",
    "1.0.0",
    "Composable governance module for the civic resilience project scenario.",
    [
      {
        id: "assume_governance_panel",
        category: "project_scope",
        description: "District command cells track public messaging, field teams, budgets, and legitimacy.",
        rationale: "The parent scenario uses relation systems to broadcast policy effects.",
      },
    ],
  );

  lines.push("", "typed_layer:", ...worldBlock("  "));
  lines.push(
    "",
    "  component_types:",
    "    - component_type_id: \"CommandState\"",
    "      fields:",
    "        district: INTEGER",
    "        coordination: FLOAT",
    "        budget: FLOAT",
    "        field_teams: INTEGER",
    "        public_messaging: FLOAT",
    "        legitimacy: FLOAT",
    "",
    "  entity_types:",
    "    - entity_type_id: \"command_cell\"",
    "      components: [\"CommandState\"]",
    "",
    "  relation_types: []",
    "  event_types: []",
    "",
    "  entities:",
  );

  for (let district = 1; district <= 6; district += 1) {
    lines.push(
      `    - entity_id: ${q(`command_d${district}`)}`,
      `      entity_type: "command_cell"`,
      `      components:`,
      `        CommandState:`,
      `          district: ${district}`,
      `          coordination: ${fixed(0.54 + district * 0.035)}`,
      `          budget: ${fixed(1280 - district * 70, 1)}`,
      `          field_teams: ${3 + (district % 3)}`,
      `          public_messaging: ${fixed(0.46 + district * 0.025)}`,
      `          legitimacy: ${fixed(0.57 + district * 0.018)}`,
    );
  }

  lines.push("", "  relations: []", "  initial_events: []", "  systems: []");
  writeFile("project_civic_resilience_governance.yaml", lines);
}

function relationBlock(lines, relationType, source, target, payload) {
  lines.push(
    `    - relation_type: ${q(relationType)}`,
    `      source: ${q(source)}`,
    `      target: ${q(target)}`,
    `      payload:`,
  );
  for (const [key, value] of Object.entries(payload)) {
    if (typeof value === "string") {
      lines.push(`        ${key}: ${q(value)}`);
    } else {
      lines.push(`        ${key}: ${fixed(value)}`);
    }
  }
}

function eventBlock(lines, eventType, timestamp, priority, handle, payload) {
  lines.push(
    `    - event_type: ${q(eventType)}`,
    `      timestamp: ${fixed(timestamp, 1)}`,
    `      priority: ${priority}`,
    `      event_handle: ${q(handle)}`,
    `      payload:`,
  );
  for (const [key, value] of Object.entries(payload)) {
    if (Number.isInteger(value)) {
      lines.push(`        ${key}: ${value}`);
    } else {
      lines.push(`        ${key}: ${fixed(value)}`);
    }
  }
}

function writeExpr(lines, target, expr, when) {
  lines.push(`        - target: ${q(target)}`);
  if (when) lines.push(`          when: ${q(when)}`);
  lines.push(`          expr: ${q(expr)}`);
}

function generateBaseScenario() {
  const lines = baseHeader(
    "project_civic_resilience_v5",
    "5.0.0",
    "A 45-day district-level disaster resilience simulation combining v3 typed systems, v4 scenario composition, v4 experiment hooks, and v5 extension symbols.",
    [
      {
        id: "assume_project_scale",
        category: "scenario_design",
        description: "The scenario intentionally uses a medium-large deterministic model: 78 entities, hundreds of relations, 45 ticks, and scheduled shocks.",
        rationale: "This demonstrates NoisiaX beyond unit fixtures while keeping the output inspectable.",
      },
      {
        id: "assume_composable_modules",
        category: "composition",
        description: "Population, infrastructure, and governance are separate importable modules.",
        rationale: "v4 composition keeps scenario authoring modular and testable.",
      },
      {
        id: "assume_extension_pricing",
        category: "extension",
        description: "The acme.market and civic.resilience v5 extensions are used as authoring transforms, expression functions, propagation symbols, and experiment metrics.",
        rationale: "This exercises extension discovery, symbol registration, lowering, typed expression dispatch, and experiment metrics.",
      },
    ],
  );

  lines.push(
    "",
    "imports:",
    "  - path: \"project_civic_resilience_population.yaml\"",
    "    namespace: \"civic\"",
    "  - path: \"project_civic_resilience_infrastructure.yaml\"",
    "    namespace: \"infra\"",
    "  - path: \"project_civic_resilience_governance.yaml\"",
    "    namespace: \"gov\"",
    "",
    "extensions:",
    "  - id: \"acme.market\"",
    "    version: \"1.2.0\"",
    "    compatibility: \">=1.2,<2.0\"",
    "  - id: \"civic.resilience\"",
    "    version: \"1.0.0\"",
    "    compatibility: \">=1.0,<2.0\"",
    "",
    "acme.market:",
    "  demo:",
    "    base: 18.5",
    "    multiplier: 1.6",
    "",
    "civic.resilience:",
    "  control_index:",
    "    hazard_baseline: 0.44",
    "    resource_readiness: 0.71",
    "    governance_weight: 1.35",
    "",
    "typed_layer:",
    ...worldBlock("  "),
    "",
    "  component_types: []",
    "  entity_types: []",
    "  entities: []",
    "",
    "  relation_types:",
    "    - relation_type_id: \"service_link\"",
    "      directed: true",
    "      max_total: 180",
    "      payload_fields:",
    "        distance_km: FLOAT",
    "        essentiality: FLOAT",
    "        demand_weight: FLOAT",
    "    - relation_type_id: \"social_signal\"",
    "      directed: true",
    "      max_per_entity: 12",
    "      max_total: 260",
    "      payload_fields:",
    "        tie_strength: FLOAT",
    "        rumor_channel: FLOAT",
    "    - relation_type_id: \"volunteer_support\"",
    "      directed: true",
    "      max_per_entity: 80",
    "      max_total: 500",
    "      payload_fields:",
    "        aid: FLOAT",
    "        created_day: FLOAT",
    "    - relation_type_id: \"resident_policy_link\"",
    "      directed: true",
    "      max_total: 120",
    "      payload_fields:",
    "        coverage: FLOAT",
    "    - relation_type_id: \"facility_policy_link\"",
    "      directed: true",
    "      max_total: 40",
    "      payload_fields:",
    "        priority: FLOAT",
    "",
    "  event_types:",
    "    - event_type_id: \"tick\"",
    "      payload_fields:",
    "        dt: FLOAT",
    "    - event_type_id: \"storm_front\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        severity: FLOAT",
    "    - event_type_id: \"supply_drop\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        stock_boost: FLOAT",
    "        price_relief: FLOAT",
    "    - event_type_id: \"repair_wave\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        crew_power: FLOAT",
    "    - event_type_id: \"rumor_spike\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        intensity: FLOAT",
    "    - event_type_id: \"evacuation_order\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        urgency: FLOAT",
    "    - event_type_id: \"coordination_day\"",
    "      payload_fields:",
    "        public_confidence: FLOAT",
    "        repair_focus: FLOAT",
    "    - event_type_id: \"service_slowdown\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        severity: FLOAT",
    "    - event_type_id: \"mutual_aid_alert\"",
    "      payload_fields:",
    "        district: INTEGER",
    "        urgency: FLOAT",
    "",
    "  relations:",
  );

  for (let i = 1; i <= 60; i += 1) {
    const district = districtForResident(i);
    const primaryFacility = facilityId((district - 1) * 2 + 1 + (i % 2));
    const role = roleFor(i);
    relationBlock(lines, "service_link", `infra__${primaryFacility}`, `civic__${residentId(i)}`, {
      distance_km: 0.35 + ((i * 11) % 19) * 0.11,
      essentiality: role === "nurse" || role === "elder" || role === "student" ? 0.88 : 0.62 + (i % 5) * 0.045,
      demand_weight: 0.65 + ((i * 7) % 13) * 0.045,
    });
  }

  for (let i = 1; i <= 60; i += 1) {
    const offsets = [1, 5, 13];
    for (const offset of offsets) {
      const target = ((i + offset - 1) % 60) + 1;
      relationBlock(lines, "social_signal", `civic__${residentId(i)}`, `civic__${residentId(target)}`, {
        tie_strength: 0.28 + ((i * offset + target) % 17) * 0.035,
        rumor_channel: 0.16 + ((i + target * 3) % 11) * 0.025,
      });
    }
  }

  for (let i = 1; i <= 60; i += 1) {
    const district = districtForResident(i);
    relationBlock(lines, "resident_policy_link", `gov__command_d${district}`, `civic__${residentId(i)}`, {
      coverage: 0.72 + ((i * 3) % 9) * 0.026,
    });
  }

  for (let i = 1; i <= 12; i += 1) {
    const district = Math.floor((i - 1) / 2) + 1;
    relationBlock(lines, "facility_policy_link", `gov__command_d${district}`, `infra__${facilityId(i)}`, {
      priority: 0.66 + (i % 5) * 0.055,
    });
  }

  lines.push("", "  initial_events:");
  eventBlock(lines, "coordination_day", 2, 5, "coordination_02", { public_confidence: 0.52, repair_focus: 0.40 });
  eventBlock(lines, "storm_front", 3, 10, "storm_d1_03", { district: 1, severity: 0.62 });
  eventBlock(lines, "supply_drop", 4, 7, "supply_d1_04", { district: 1, stock_boost: 0.36, price_relief: 0.22 });
  eventBlock(lines, "repair_wave", 5, 6, "repair_d1_05", { district: 1, crew_power: 0.44 });
  eventBlock(lines, "rumor_spike", 6, 8, "rumor_d2_06", { district: 2, intensity: 0.55 });
  eventBlock(lines, "storm_front", 7, 10, "storm_d3_07", { district: 3, severity: 0.86 });
  eventBlock(lines, "evacuation_order", 8, 9, "evac_d3_08", { district: 3, urgency: 0.72 });
  eventBlock(lines, "coordination_day", 9, 5, "coordination_09", { public_confidence: 0.60, repair_focus: 0.55 });
  eventBlock(lines, "supply_drop", 11, 7, "supply_d3_11", { district: 3, stock_boost: 0.48, price_relief: 0.30 });
  eventBlock(lines, "storm_front", 12, 10, "storm_d5_12", { district: 5, severity: 0.74 });
  eventBlock(lines, "repair_wave", 14, 6, "repair_d3_14", { district: 3, crew_power: 0.68 });
  eventBlock(lines, "rumor_spike", 16, 8, "rumor_d5_16", { district: 5, intensity: 0.68 });
  eventBlock(lines, "coordination_day", 17, 5, "coordination_17", { public_confidence: 0.64, repair_focus: 0.64 });
  eventBlock(lines, "evacuation_order", 19, 9, "evac_d5_19", { district: 5, urgency: 0.62 });
  eventBlock(lines, "supply_drop", 20, 7, "supply_d5_20", { district: 5, stock_boost: 0.42, price_relief: 0.26 });
  eventBlock(lines, "repair_wave", 23, 6, "repair_d5_23", { district: 5, crew_power: 0.58 });
  eventBlock(lines, "storm_front", 25, 10, "storm_d6_25", { district: 6, severity: 0.69 });
  eventBlock(lines, "coordination_day", 26, 5, "coordination_26", { public_confidence: 0.67, repair_focus: 0.72 });
  eventBlock(lines, "rumor_spike", 28, 8, "rumor_d4_28", { district: 4, intensity: 0.61 });
  eventBlock(lines, "supply_drop", 31, 7, "supply_d6_31", { district: 6, stock_boost: 0.40, price_relief: 0.24 });
  eventBlock(lines, "repair_wave", 35, 6, "repair_d6_35", { district: 6, crew_power: 0.61 });
  eventBlock(lines, "coordination_day", 36, 5, "coordination_36", { public_confidence: 0.70, repair_focus: 0.78 });
  eventBlock(lines, "storm_front", 39, 10, "storm_d2_39", { district: 2, severity: 0.58 });
  eventBlock(lines, "coordination_day", 42, 5, "coordination_42", { public_confidence: 0.74, repair_focus: 0.82 });

  lines.push(
    "",
    "  systems:",
    "    - system_id: \"daily_resident_pressure\"",
    "      triggered_by: [\"tick\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.safety", "std::clamp(civic__CivicState.safety + 0.014 * civic__ResidentProfile.preparedness - 0.024 * civic__CivicState.stress - 0.010 * civic__CivicState.rumor_belief, 0, 1)");
  writeExpr(lines, "civic__CivicState.food", "std::clamp(civic__CivicState.food - 0.018 * event.dt + 0.009 * civic__CivicState.volunteer_capacity, 0, 1)");
  writeExpr(lines, "civic__CivicState.water", "std::clamp(civic__CivicState.water - 0.014 * event.dt + 0.004 * civic__ResidentProfile.preparedness, 0, 1)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress + 0.018 * (1 - civic__CivicState.power) + 0.014 * (1 - civic__CivicState.water) + 0.012 * civic__CivicState.rumor_belief - 0.010 * civic__CivicState.trust, 0, 1)");
  writeExpr(lines, "civic__CivicState.price_pressure", "std::clamp(civic__CivicState.price_pressure * 0.985 + civic.resilience::triage_priority(civic__CivicState.stress, civic__ResidentProfile.care_load, civic__ResidentProfile.mobility, civic__ResidentProfile.preparedness, civic__CivicState.trust) * 0.018, 0, 25)");
  writeExpr(lines, "civic__CivicState.resilience_score", "civic.resilience::resilience_index(civic__CivicState.safety, civic__CivicState.power, civic__CivicState.water, civic__CivicState.food, civic__CivicState.medical_access, civic__CivicState.trust, civic__CivicState.volunteer_capacity, civic__CivicState.stress, civic__CivicState.rumor_belief)");

  lines.push(
    "",
    "    - system_id: \"facility_daily_drift\"",
    "      triggered_by: [\"tick\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"infra__facility\"",
    "      writes:",
  );
  writeExpr(lines, "infra__FacilityStatus.reliability", "std::clamp(infra__FacilityStatus.reliability - 0.006 * infra__FacilityStatus.damage - 0.004 * infra__FacilityStatus.load / infra__FacilityStatus.capacity + 0.002 * infra__FacilityStatus.stock, 0, 1)");
  writeExpr(lines, "infra__FacilityStatus.backlog", "std::clamp(infra__FacilityStatus.backlog + 0.018 * infra__FacilityStatus.damage + 0.010 * infra__FacilityStatus.load / infra__FacilityStatus.capacity - 0.006 * infra__FacilityStatus.stock, 0, 100)");
  writeExpr(lines, "infra__FacilityStatus.load", "std::clamp(infra__FacilityStatus.load * 0.65, 0, infra__FacilityStatus.capacity * 2)");
  writeExpr(lines, "infra__FacilityStatus.price_index", "std::clamp(infra__FacilityStatus.price_index * 0.92 + civic.resilience::scarcity_pressure(infra__FacilityStatus.stock, infra__FacilityStatus.load, infra__FacilityStatus.capacity, infra__FacilityStatus.damage, infra__FacilityStatus.reliability) + acme.market::bid_price_v2(infra__FacilityStatus.price_index) * 0.015, 0, 20)");

  lines.push(
    "",
    "    - system_id: \"service_delivery\"",
    "      triggered_by: [\"tick\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"service_link\"",
    "      writes:",
  );
  writeExpr(lines, "self.infra__FacilityStatus.load", "std::clamp(self.infra__FacilityStatus.load + relation.demand_weight * (1 + other.civic__CivicState.stress), 0, self.infra__FacilityStatus.capacity * 2)");
  writeExpr(lines, "other.civic__CivicState.power", "std::clamp(other.civic__CivicState.power + 0.030 * self.infra__FacilityStatus.reliability - 0.050 * self.infra__FacilityStatus.damage - 0.010 * self.infra__FacilityStatus.load / self.infra__FacilityStatus.capacity, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.water", "std::clamp(other.civic__CivicState.water + 0.022 * self.infra__FacilityStatus.stock + 0.018 * self.infra__FacilityStatus.reliability - 0.012 * relation.distance_km, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.medical_access", "std::clamp(other.civic__CivicState.medical_access + 0.018 * relation.essentiality * self.infra__FacilityStatus.reliability - 0.020 * self.infra__FacilityStatus.backlog, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.price_pressure", "std::clamp(other.civic__CivicState.price_pressure + 0.080 * self.infra__FacilityStatus.price_index + 0.030 * civic.resilience::scarcity_pressure(self.infra__FacilityStatus.stock, self.infra__FacilityStatus.load, self.infra__FacilityStatus.capacity, self.infra__FacilityStatus.damage, self.infra__FacilityStatus.reliability), 0, 25)");

  lines.push(
    "",
    "    - system_id: \"social_signal_diffusion\"",
    "      triggered_by: [\"tick\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"social_signal\"",
    "      where: \"relation.tie_strength > 0.30\"",
    "      writes:",
  );
  writeExpr(lines, "other.civic__CivicState.rumor_belief", "std::clamp(other.civic__CivicState.rumor_belief + relation.rumor_channel * (self.civic__CivicState.rumor_belief - other.civic__CivicState.trust) * 0.060, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.trust", "std::clamp(other.civic__CivicState.trust + relation.tie_strength * (self.civic__CivicState.trust - other.civic__CivicState.rumor_belief) * 0.035, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.volunteer_capacity", "std::clamp(other.civic__CivicState.volunteer_capacity + relation.tie_strength * self.civic__CivicState.volunteer_capacity * 0.010 - 0.005 * other.civic__CivicState.stress, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"bootstrap_mutual_aid_after_storm\"",
    "      triggered_by: [\"storm_front\"]",
    "      kind: \"pair\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == other.civic__ResidentProfile.district && civic__CivicState.volunteer_capacity > 0.45 && other.civic__CivicState.stress > 0.32\"",
    "      create_relations:",
    "        - relation_type: \"volunteer_support\"",
    "          source: \"self\"",
    "          target: \"other\"",
    "          expires_after: \"7 + rng(\\\"aid_days\\\", 0, 3)\"",
    "          when: \"rng(\\\"aid_link\\\", 0, 1) < 0.055 + civic__CivicState.trust * 0.035\"",
    "          payload:",
    "            aid: \"std::clamp(civic__CivicState.volunteer_capacity * (0.7 + rng(\\\"aid_strength\\\", 0, 1) * 0.3), 0.05, 1)\"",
    "            created_day: \"event.timestamp\"",
    "      emit_events:",
    "        - event_type: \"mutual_aid_alert\"",
    "          timestamp: \"event.timestamp\"",
    "          priority: 1",
    "          when: \"rng(\\\"aid_alert\\\", 0, 1) < 0.08\"",
    "          payload:",
    "            district: \"civic__ResidentProfile.district\"",
    "            urgency: \"event.severity\"",
    "",
    "    - system_id: \"apply_mutual_aid\"",
    "      triggered_by: [\"tick\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"volunteer_support\"",
    "      where: \"relation.expires_at > event.timestamp\"",
    "      writes:",
  );
  writeExpr(lines, "other.civic__CivicState.food", "std::clamp(other.civic__CivicState.food + relation.aid * 0.040, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.water", "std::clamp(other.civic__CivicState.water + relation.aid * 0.030, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.stress", "std::clamp(other.civic__CivicState.stress - relation.aid * 0.025, 0, 1)");
  writeExpr(lines, "self.civic__CivicState.volunteer_capacity", "std::clamp(self.civic__CivicState.volunteer_capacity - relation.aid * 0.012, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"storm_damages_facilities\"",
    "      triggered_by: [\"storm_front\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"infra__facility\"",
    "      where: \"infra__FacilityStatus.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "infra__FacilityStatus.damage", "std::clamp(infra__FacilityStatus.damage + event.severity * (0.18 + rng(\"facility_damage\", 0, 1) * 0.12), 0, 1)");
  writeExpr(lines, "infra__FacilityStatus.reliability", "std::clamp(infra__FacilityStatus.reliability - event.severity * (0.12 + rng(\"facility_reliability\", 0, 1) * 0.08), 0, 1)");
  writeExpr(lines, "infra__FacilityStatus.backlog", "std::clamp(infra__FacilityStatus.backlog + event.severity * 1.8, 0, 100)");
  writeExpr(lines, "infra__FacilityStatus.stock", "std::clamp(infra__FacilityStatus.stock - event.severity * 0.20, 0, 1)");
  lines.push(
    "      emit_events:",
    "        - event_type: \"service_slowdown\"",
    "          timestamp: \"event.timestamp + 0.5\"",
    "          priority: 2",
    "          when: \"infra__FacilityStatus.reliability < 0.78 || rng(\\\"slowdown_emit\\\", 0, 1) < event.severity * 0.35\"",
    "          payload:",
    "            district: \"infra__FacilityStatus.district\"",
    "            severity: \"std::clamp(event.severity + infra__FacilityStatus.damage, 0, 1)\"",
    "",
    "    - system_id: \"storm_impacts_residents\"",
    "      triggered_by: [\"storm_front\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.safety", "std::clamp(civic__CivicState.safety - event.severity * (0.16 + (1 - civic__ResidentProfile.preparedness) * 0.14), 0, 1)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress + event.severity * (0.18 + civic__ResidentProfile.care_load * 0.10), 0, 1)");
  writeExpr(lines, "civic__CivicState.power", "std::clamp(civic__CivicState.power - event.severity * 0.22, 0, 1)");
  writeExpr(lines, "civic__CivicState.water", "std::clamp(civic__CivicState.water - event.severity * 0.12, 0, 1)");
  writeExpr(lines, "civic__CivicState.rumor_belief", "std::clamp(civic__CivicState.rumor_belief + event.severity * (0.08 + rng(\"storm_rumor\", 0, 1) * 0.06), 0, 1)");
  lines.push(
    "      emit_events:",
    "        - event_type: \"mutual_aid_alert\"",
    "          timestamp: \"event.timestamp + 0.25\"",
    "          priority: 1",
    "          when: \"civic__CivicState.stress > 0.62 && rng(\\\"resident_alert\\\", 0, 1) < 0.18\"",
    "          payload:",
    "            district: \"civic__ResidentProfile.district\"",
    "            urgency: \"civic__CivicState.stress\"",
    "",
    "    - system_id: \"service_slowdown_impact\"",
    "      triggered_by: [\"service_slowdown\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"service_link\"",
    "      where: \"self.infra__FacilityStatus.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "other.civic__CivicState.power", "std::clamp(other.civic__CivicState.power - event.severity * 0.14 * relation.demand_weight, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.water", "std::clamp(other.civic__CivicState.water - event.severity * 0.10 * relation.demand_weight, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.medical_access", "std::clamp(other.civic__CivicState.medical_access - event.severity * 0.08 * relation.essentiality, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.stress", "std::clamp(other.civic__CivicState.stress + event.severity * 0.08, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"repair_wave_facilities\"",
    "      triggered_by: [\"repair_wave\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"infra__facility\"",
    "      where: \"infra__FacilityStatus.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "infra__FacilityStatus.reliability", "std::clamp(infra__FacilityStatus.reliability + event.crew_power * 0.22, 0, 1)");
  writeExpr(lines, "infra__FacilityStatus.damage", "std::clamp(infra__FacilityStatus.damage - event.crew_power * 0.20, 0, 1)");
  writeExpr(lines, "infra__FacilityStatus.backlog", "std::clamp(infra__FacilityStatus.backlog - event.crew_power * 1.4, 0, 100)");
  writeExpr(lines, "infra__FacilityStatus.stock", "std::clamp(infra__FacilityStatus.stock + event.crew_power * 0.10, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"supply_drop_residents\"",
    "      triggered_by: [\"supply_drop\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.food", "std::clamp(civic__CivicState.food + event.stock_boost * (0.8 + civic__ResidentProfile.care_load * 0.2), 0, 1)");
  writeExpr(lines, "civic__CivicState.water", "std::clamp(civic__CivicState.water + event.stock_boost * 0.35, 0, 1)");
  writeExpr(lines, "civic__CivicState.price_pressure", "std::clamp(civic__CivicState.price_pressure - event.price_relief, 0, 25)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress - event.stock_boost * 0.09, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"rumor_spike_residents\"",
    "      triggered_by: [\"rumor_spike\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.rumor_belief", "std::clamp(civic__CivicState.rumor_belief + event.intensity * (0.16 + rng(\"rumor_accept\", 0, 1) * 0.12) - civic__CivicState.trust * 0.06, 0, 1)");
  writeExpr(lines, "civic__CivicState.trust", "std::clamp(civic__CivicState.trust - event.intensity * 0.08 + civic__ResidentProfile.preparedness * 0.03, 0, 1)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress + event.intensity * civic__CivicState.rumor_belief * 0.10, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"evacuation_residents\"",
    "      triggered_by: [\"evacuation_order\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.safety", "std::clamp(civic__CivicState.safety + event.urgency * civic__ResidentProfile.mobility * 0.20 - civic__ResidentProfile.care_load * 0.04, 0, 1)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress + event.urgency * (1 - civic__ResidentProfile.mobility) * 0.14 - civic__CivicState.trust * 0.04, 0, 1)");
  writeExpr(lines, "civic__CivicState.food", "std::clamp(civic__CivicState.food - event.urgency * 0.05, 0, 1)");

  lines.push(
    "",
    "    - system_id: \"coordination_policy_updates\"",
    "      triggered_by: [\"coordination_day\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"gov__command_cell\"",
    "      writes:",
  );
  writeExpr(lines, "gov__CommandState.coordination", "std::clamp(gov__CommandState.coordination + event.public_confidence * 0.04 + rng(\"coordination_gain\", 0, 1) * 0.02, 0, 1)");
  writeExpr(lines, "gov__CommandState.public_messaging", "std::clamp(gov__CommandState.public_messaging + event.public_confidence * 0.03, 0, 1)");
  writeExpr(lines, "gov__CommandState.legitimacy", "std::clamp(gov__CommandState.legitimacy + event.public_confidence * 0.025 - 0.006 * event.repair_focus, 0, 1)");
  writeExpr(lines, "gov__CommandState.budget", "std::clamp(gov__CommandState.budget - event.repair_focus * 11.0, 0, 5000)");

  lines.push(
    "",
    "    - system_id: \"resident_policy_effects\"",
    "      triggered_by: [\"coordination_day\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"resident_policy_link\"",
    "      writes:",
  );
  writeExpr(lines, "other.civic__ResidentProfile.preparedness", "std::clamp(other.civic__ResidentProfile.preparedness + relation.coverage * self.gov__CommandState.public_messaging * 0.018, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.trust", "std::clamp(other.civic__CivicState.trust + relation.coverage * self.gov__CommandState.legitimacy * 0.020, 0, 1)");
  writeExpr(lines, "other.civic__CivicState.stress", "std::clamp(other.civic__CivicState.stress - relation.coverage * self.gov__CommandState.coordination * 0.012, 0, 1)");
  writeExpr(lines, "self.gov__CommandState.budget", "std::clamp(self.gov__CommandState.budget - relation.coverage * 0.65, 0, 5000)");

  lines.push(
    "",
    "    - system_id: \"facility_policy_effects\"",
    "      triggered_by: [\"repair_wave\"]",
    "      kind: \"per_relation\"",
    "      relation_type: \"facility_policy_link\"",
    "      where: \"other.infra__FacilityStatus.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "other.infra__FacilityStatus.reliability", "std::clamp(other.infra__FacilityStatus.reliability + relation.priority * self.gov__CommandState.coordination * event.crew_power * 0.08, 0, 1)");
  writeExpr(lines, "other.infra__FacilityStatus.backlog", "std::clamp(other.infra__FacilityStatus.backlog - relation.priority * self.gov__CommandState.field_teams * 0.20, 0, 100)");
  writeExpr(lines, "self.gov__CommandState.budget", "std::clamp(self.gov__CommandState.budget - relation.priority * event.crew_power * 2.8, 0, 5000)");

  lines.push(
    "",
    "    - system_id: \"mutual_aid_alert_resident_response\"",
    "      triggered_by: [\"mutual_aid_alert\"]",
    "      kind: \"per_entity\"",
    "      entity_type: \"civic__resident\"",
    "      where: \"civic__ResidentProfile.district == event.district\"",
    "      writes:",
  );
  writeExpr(lines, "civic__CivicState.volunteer_capacity", "std::clamp(civic__CivicState.volunteer_capacity + event.urgency * civic__CivicState.trust * 0.030 - civic__CivicState.stress * 0.010, 0, 1)");
  writeExpr(lines, "civic__CivicState.trust", "std::clamp(civic__CivicState.trust + event.urgency * 0.010, 0, 1)");
  writeExpr(lines, "civic__CivicState.stress", "std::clamp(civic__CivicState.stress - event.urgency * civic__CivicState.volunteer_capacity * 0.018, 0, 1)");

  writeFile("project_civic_resilience_v5.yaml", lines);
}

function overrideBlock(lines, overrideId, entityId, component, field, value) {
  lines.push(
    `      - override_id: ${q(overrideId)}`,
    `        op: "replace"`,
    `        target:`,
    `          typed_field:`,
    `            entity_id: ${q(entityId)}`,
    `            component_type_id: ${q(component)}`,
    `            field_name: ${q(field)}`,
    `        value: ${typeof value === "number" ? fixed(value) : q(value)}`,
  );
}

function generateExperiment() {
  const lines = [
    `experiment_id: "project_civic_resilience_v4_v5_experiment"`,
    `base_scenario: "project_civic_resilience_v5.yaml"`,
    `fail_fast: false`,
    `write_run_details: true`,
    ``,
    `seed_plan:`,
    `  seeds: [20260506, 20260507, 20260508, 20260509]`,
    ``,
    `overlays:`,
    `  - overlay_id: "sample_r017_initial_stress"`,
    `    sampler: "uniform_float"`,
    `    target:`,
    `      typed_field:`,
    `        entity_id: "civic__r017"`,
    `        component_type_id: "civic__CivicState"`,
    `        field_name: "stress"`,
    `    params: { min: 0.18, max: 0.72 }`,
    `  - overlay_id: "sample_facility_05_reliability"`,
    `    sampler: "uniform_float"`,
    `    target:`,
    `      typed_field:`,
    `        entity_id: "infra__facility_05"`,
    `        component_type_id: "infra__FacilityStatus"`,
    `        field_name: "reliability"`,
    `    params: { min: 0.52, max: 0.93 }`,
    ``,
    `variants:`,
    `  - variant_id: "baseline"`,
    `  - variant_id: "prepositioned_supplies"`,
    `    overrides:`,
  ];

  overrideBlock(lines, "boost_facility_06_stock", "infra__facility_06", "infra__FacilityStatus", "stock", 0.95);
  overrideBlock(lines, "boost_r024_food", "civic__r024", "civic__CivicState", "food", 0.96);
  overrideBlock(lines, "boost_r041_water", "civic__r041", "civic__CivicState", "water", 0.94);

  lines.push(`  - variant_id: "high_trust_drill"`, `    overrides:`);
  overrideBlock(lines, "trust_r001_drill", "civic__r001", "civic__CivicState", "trust", 0.88);
  overrideBlock(lines, "trust_r030_drill", "civic__r030", "civic__CivicState", "trust", 0.86);
  overrideBlock(lines, "gov_d3_legitimacy_drill", "gov__command_d3", "gov__CommandState", "legitimacy", 0.84);

  lines.push(`  - variant_id: "fragile_grid"`, `    overrides:`);
  overrideBlock(lines, "low_facility_09_reliability", "infra__facility_09", "infra__FacilityStatus", "reliability", 0.48);
  overrideBlock(lines, "low_facility_10_stock", "infra__facility_10", "infra__FacilityStatus", "stock", 0.34);
  overrideBlock(lines, "stress_r050_fragile", "civic__r050", "civic__CivicState", "stress", 0.69);

  lines.push(
    ``,
    `metrics:`,
    `  - metric_id: "r001_resilience"`,
    `    kind: "typed_field_final"`,
    `    typed_field:`,
    `      entity_id: "civic__r001"`,
    `      component_type_id: "civic__CivicState"`,
    `      field_name: "resilience_score"`,
    `  - metric_id: "r024_resilience"`,
    `    kind: "typed_field_final"`,
    `    typed_field:`,
    `      entity_id: "civic__r024"`,
    `      component_type_id: "civic__CivicState"`,
    `      field_name: "resilience_score"`,
    `  - metric_id: "r060_resilience"`,
    `    kind: "typed_field_final"`,
    `    typed_field:`,
    `      entity_id: "civic__r060"`,
    `      component_type_id: "civic__CivicState"`,
    `      field_name: "resilience_score"`,
    `  - metric_id: "facility_09_backlog"`,
    `    kind: "typed_field_final"`,
    `    typed_field:`,
    `      entity_id: "infra__facility_09"`,
    `      component_type_id: "infra__FacilityStatus"`,
    `      field_name: "backlog"`,
    `  - metric_id: "gov_d3_budget"`,
    `    kind: "typed_field_final"`,
    `    typed_field:`,
    `      entity_id: "gov__command_d3"`,
    `      component_type_id: "gov__CommandState"`,
    `      field_name: "budget"`,
    `  - metric_id: "scaled_r001_resilience"`,
    `    kind: "acme.market::scaled_typed_field_final"`,
    `    config:`,
    `      entity_id: "civic__r001"`,
    `      component_type_id: "civic__CivicState"`,
    `      field_name: "resilience_score"`,
    `      multiplier: 100`,
    `  - metric_id: "avg_population_resilience_pct"`,
    `    kind: "civic.resilience::population_average_field"`,
    `    config:`,
    `      entity_id_prefix: "civic__r"`,
    `      component_type_id: "civic__CivicState"`,
    `      field_name: "resilience_score"`,
    `      multiplier: 100`,
  );

  writeFile("project_civic_resilience_experiment_v4_v5.yaml", lines);
}

generatePopulationFragment();
generateInfrastructureFragment();
generateGovernanceFragment();
generateBaseScenario();
generateExperiment();

console.log("Generated civic resilience scenario suite in scenarios/.");
