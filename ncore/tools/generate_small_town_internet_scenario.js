#!/usr/bin/env node

const fs = require("fs");
const path = require("path");

const repoRoot = path.resolve(__dirname, "..");
const outputPath = path.join(repoRoot, "scenarios", "v3_small_town_internet_100p_90d.yaml");

function q(value) {
  return JSON.stringify(value);
}

function fixed(value, digits = 3) {
  return Number(value).toFixed(digits);
}

function roleFor(i) {
  const r = i % 12;
  if (r === 1 || r === 2 || r === 9) return "student";
  if (r === 3) return "elder";
  if (r === 4 || r === 10) return "vendor";
  if (r === 5) return "remote_worker";
  if (r === 6) return "teacher";
  if (r === 7 || r === 11) return "farmer";
  return "worker";
}

function ageFor(i, role) {
  const offsets = [0, 3, 7, 11, 17, 23, 29];
  if (role === "student") return 11 + ((i + offsets[i % offsets.length]) % 8);
  if (role === "elder") return 63 + ((i * 5) % 19);
  if (role === "teacher") return 28 + ((i * 4) % 24);
  if (role === "remote_worker") return 24 + ((i * 7) % 28);
  if (role === "vendor") return 22 + ((i * 5) % 36);
  if (role === "farmer") return 25 + ((i * 3) % 40);
  return 20 + ((i * 6) % 42);
}

function incomeFor(i, role) {
  const bump = ((i * 37) % 180) - 70;
  const base = {
    student: 45,
    elder: 340,
    vendor: 830,
    remote_worker: 1380,
    teacher: 940,
    farmer: 610,
    worker: 760
  }[role];
  return Math.max(25, base + bump);
}

function skillFor(i, role, ward) {
  const base = {
    student: 0.68,
    elder: 0.22,
    vendor: 0.46,
    remote_worker: 0.78,
    teacher: 0.69,
    farmer: 0.31,
    worker: 0.43
  }[role];
  return Math.max(0.05, Math.min(0.95, base + (ward - 3) * 0.025 + ((i * 13) % 17) / 200));
}

function personId(i) {
  return `p${String(i).padStart(3, "0")}`;
}

function hubId(ward) {
  return `hub_ward_${ward}`;
}

const residents = [];
for (let i = 1; i <= 100; i += 1) {
  const id = personId(i);
  const ward = Math.floor((i - 1) / 20) + 1;
  const household = Math.ceil(i / 4);
  const role = roleFor(i);
  const age = ageFor(i, role);
  const income = incomeFor(i, role);
  const digitalSkill = skillFor(i, role, ward);
  const initiallySubscribed =
    role === "remote_worker" ||
    role === "teacher" ||
    (income > 760 && i % 3 === 0) ||
    (ward <= 2 && i % 4 === 0) ||
    (role === "student" && i % 5 === 0);
  const interest = Math.max(
    0.1,
    Math.min(0.92, (initiallySubscribed ? 0.67 : 0.25) + digitalSkill * 0.28 + income / 9000)
  );
  const deviceQuality = Math.max(0.25, Math.min(0.98, 0.42 + digitalSkill * 0.43 + ((i * 11) % 13) / 100));
  const routerHealth = initiallySubscribed
    ? Math.max(0.58, Math.min(0.98, 0.76 + ((i * 7) % 18) / 100))
    : Math.max(0.35, Math.min(0.72, 0.48 + ((i * 5) % 15) / 100));
  const monthlyCost = initiallySubscribed ? 22 + ((i * 3) % 19) : 0;
  const planSpeed = initiallySubscribed ? 18 + ((i * 9) % 42) : 0;

  residents.push({
    id,
    ward,
    household,
    role,
    age,
    income,
    digitalSkill,
    initiallySubscribed,
    interest,
    deviceQuality,
    routerHealth,
    monthlyCost,
    planSpeed
  });
}

const lines = [];
function push(line = "") {
  lines.push(line);
}

push('scenario_id: "v3_small_town_internet_100p_90d"');
push('schema_version: "1.0.0"');
push("master_seed: 20260505");
push('goal_statement: "Simulate internet adoption, infrastructure stress, misinformation, repair, education, and local economic effects across a 100-person small town over 90 days."');
push("assumptions:");
push('  - assumption_id: "assume_small_town_scale"');
push('    category: "simulation_scope"');
push('    description: "The town has 100 residents grouped into 25 households and 5 wards served by ward-level network hubs."');
push('    rationale: "This creates enough heterogeneity for chain reactions while remaining inspectable as a deterministic typed-layer fixture."');
push("    confidence_level: REJECT");
push('  - assumption_id: "assume_chain_reactions"');
push('    category: "runtime_behavior"');
push('    description: "Connectivity, trust, adoption, misinformation, service slowdowns, help tickets, and repairs can reinforce or dampen each other over time."');
push('    rationale: "The scenario is designed to exercise event chains, relations, event emission, bounded random draws, and final-state snapshots."');
push("    confidence_level: REJECT");
push("");
push("typed_layer:");
push("  world:");
push("    duration: 90.0");
push('    time_unit: "days"');
push("    max_event_count: 5000");
push("    tick_interval: 1.0");
push("");
push("  component_types:");
push('    - component_type_id: "PersonProfile"');
push("      fields:");
push("        household: INTEGER");
push("        ward: INTEGER");
push("        age: INTEGER");
push("        role: STRING");
push("        income: FLOAT");
push("        digital_skill: FLOAT");
push('    - component_type_id: "AccessState"');
push("      fields:");
push("        has_internet: BOOLEAN");
push("        device_quality: FLOAT");
push("        router_health: FLOAT");
push("        plan_speed: FLOAT");
push("        monthly_cost: FLOAT");
push('    - component_type_id: "InternetBehavior"');
push("      fields:");
push("        usage_hours: FLOAT");
push("        remote_work_hours: FLOAT");
push("        learning_hours: FLOAT");
push("        misinformation_exposure: FLOAT");
push("        trusted_info: FLOAT");
push("        social_posts: FLOAT");
push('    - component_type_id: "Wellbeing"');
push("      fields:");
push("        productivity: FLOAT");
push("        school_progress: FLOAT");
push("        health_access: FLOAT");
push("        happiness: FLOAT");
push("        savings: FLOAT");
push('    - component_type_id: "Adoption"');
push("      fields:");
push("        interest: FLOAT");
push("        subscribed: BOOLEAN");
push("        days_online: INTEGER");
push("        churn_risk: FLOAT");
push('    - component_type_id: "EventMemory"');
push("      fields:");
push("        service_disruption: FLOAT");
push("        rumor_belief: FLOAT");
push("        help_requests: INTEGER");
push('    - component_type_id: "NetworkNode"');
push("      fields:");
push("        ward: INTEGER");
push("        capacity: FLOAT");
push("        load: FLOAT");
push("        reliability: FLOAT");
push("        repair_backlog: FLOAT");
push("        outage_pressure: FLOAT");
push("");
push("  entity_types:");
push('    - entity_type_id: "resident"');
push('      components: ["PersonProfile", "AccessState", "InternetBehavior", "Wellbeing", "Adoption", "EventMemory"]');
push('    - entity_type_id: "network_hub"');
push('      components: ["NetworkNode"]');
push("");
push("  relation_types:");
push('    - relation_type_id: "served_by"');
push("      directed: true");
push("      max_total: 120");
push("      payload_fields:");
push("        distance_km: FLOAT");
push("        demand_weight: FLOAT");
push('    - relation_type_id: "friendship"');
push("      directed: true");
push("      max_per_entity: 12");
push("      max_total: 280");
push("      payload_fields:");
push("        tie_strength: FLOAT");
push("        channel: STRING");
push('    - relation_type_id: "help_ticket"');
push("      directed: true");
push("      max_total: 2000");
push("      payload_fields:");
push("        severity: FLOAT");
push("        created_day: FLOAT");
push("");
push("  event_types:");
push('    - event_type_id: "tick"');
push("      payload_fields:");
push("        dt: FLOAT");
push('    - event_type_id: "fiber_cut"');
push("      payload_fields:");
push("        ward: INTEGER");
push("        severity: FLOAT");
push('    - event_type_id: "repair_drive"');
push("      payload_fields:");
push("        ward: INTEGER");
push("        boost: FLOAT");
push('    - event_type_id: "training_day"');
push("      payload_fields:");
push("        skill_boost: FLOAT");
push("        trust_boost: FLOAT");
push('    - event_type_id: "subsidy_offer"');
push("      payload_fields:");
push("        income_ceiling: FLOAT");
push("        cost_discount: FLOAT");
push("        interest_boost: FLOAT");
push('    - event_type_id: "school_device_rollout"');
push("      payload_fields:");
push("        device_boost: FLOAT");
push("        trust_boost: FLOAT");
push('    - event_type_id: "rumor_wave"');
push("      payload_fields:");
push("        intensity: FLOAT");
push('    - event_type_id: "festival_stream"');
push("      payload_fields:");
push("        demand_boost: FLOAT");
push("        post_boost: FLOAT");
push('    - event_type_id: "service_slowdown"');
push("      payload_fields:");
push("        ward: INTEGER");
push("        severity: FLOAT");
push("");
push("  entities:");

for (let ward = 1; ward <= 5; ward += 1) {
  const capacity = 520 - (ward - 1) * 35;
  const reliability = 0.91 - (ward - 1) * 0.035;
  const backlog = ward === 5 ? 2.2 : ward === 3 ? 1.4 : 0.6 + ward * 0.15;
  push(`    - entity_id: "${hubId(ward)}"`);
  push('      entity_type: "network_hub"');
  push("      components:");
  push("        NetworkNode:");
  push(`          ward: ${ward}`);
  push(`          capacity: ${fixed(capacity, 1)}`);
  push("          load: 0.0");
  push(`          reliability: ${fixed(reliability, 3)}`);
  push(`          repair_backlog: ${fixed(backlog, 3)}`);
  push(`          outage_pressure: ${fixed(0.04 + ward * 0.015, 3)}`);
}

for (const p of residents) {
  const baseTrust = Math.max(0.18, Math.min(0.88, 0.28 + p.digitalSkill * 0.48 + (p.role === "teacher" ? 0.15 : 0)));
  const misinformation = Math.max(0.02, Math.min(0.45, 0.18 - p.digitalSkill * 0.10 + ((Number(p.id.slice(1)) * 7) % 11) / 100));
  const productivity = Math.max(0.18, Math.min(0.92, 0.34 + p.income / 2800 + p.digitalSkill * 0.18));
  const schoolProgress = p.role === "student" ? 0.38 + p.digitalSkill * 0.28 : 0.0;
  const healthAccess = p.role === "elder" ? 0.34 : 0.18 + p.digitalSkill * 0.16;
  const happiness = Math.max(0.25, Math.min(0.82, 0.48 + p.digitalSkill * 0.13 + (p.initiallySubscribed ? 0.05 : 0)));
  const savings = Math.max(0, p.income * 0.9 + ((Number(p.id.slice(1)) * 41) % 260));

  push(`    - entity_id: "${p.id}"`);
  push('      entity_type: "resident"');
  push("      components:");
  push("        PersonProfile:");
  push(`          household: ${p.household}`);
  push(`          ward: ${p.ward}`);
  push(`          age: ${p.age}`);
  push(`          role: ${q(p.role)}`);
  push(`          income: ${fixed(p.income, 1)}`);
  push(`          digital_skill: ${fixed(p.digitalSkill, 3)}`);
  push("        AccessState:");
  push(`          has_internet: ${p.initiallySubscribed ? "true" : "false"}`);
  push(`          device_quality: ${fixed(p.deviceQuality, 3)}`);
  push(`          router_health: ${fixed(p.routerHealth, 3)}`);
  push(`          plan_speed: ${fixed(p.planSpeed, 1)}`);
  push(`          monthly_cost: ${fixed(p.monthlyCost, 1)}`);
  push("        InternetBehavior:");
  push(`          usage_hours: ${fixed(p.initiallySubscribed ? 0.8 + p.digitalSkill * 2 : 0.0, 3)}`);
  push("          remote_work_hours: 0.0");
  push("          learning_hours: 0.0");
  push(`          misinformation_exposure: ${fixed(misinformation, 3)}`);
  push(`          trusted_info: ${fixed(baseTrust, 3)}`);
  push(`          social_posts: ${fixed(p.initiallySubscribed ? 0.5 + p.digitalSkill * 2.0 : 0.0, 3)}`);
  push("        Wellbeing:");
  push(`          productivity: ${fixed(productivity, 3)}`);
  push(`          school_progress: ${fixed(schoolProgress, 3)}`);
  push(`          health_access: ${fixed(healthAccess, 3)}`);
  push(`          happiness: ${fixed(happiness, 3)}`);
  push(`          savings: ${fixed(savings, 2)}`);
  push("        Adoption:");
  push(`          interest: ${fixed(p.interest, 3)}`);
  push(`          subscribed: ${p.initiallySubscribed ? "true" : "false"}`);
  push(`          days_online: ${p.initiallySubscribed ? 1 : 0}`);
  push(`          churn_risk: ${fixed(p.initiallySubscribed ? 0.08 : 0.18, 3)}`);
  push("        EventMemory:");
  push("          service_disruption: 0.0");
  push(`          rumor_belief: ${fixed(0.06 + (1 - p.digitalSkill) * 0.16, 3)}`);
  push("          help_requests: 0");
}

push("");
push("  relations:");
for (const p of residents) {
  const distance = 0.3 + ((Number(p.id.slice(1)) * 17) % 58) / 10;
  const demandWeight = 0.78 + (p.digitalSkill * 0.45) + (p.role === "remote_worker" ? 0.35 : 0) + (p.role === "student" ? 0.15 : 0);
  push(`    - {relation_type: "served_by", source: "${hubId(p.ward)}", target: "${p.id}", payload: {distance_km: ${fixed(distance, 2)}, demand_weight: ${fixed(demandWeight, 3)}}}`);
}

const friendshipKeys = new Set();
function addFriend(source, target, strength, channel) {
  if (source === target) return;
  const key = `${source}->${target}`;
  if (friendshipKeys.has(key)) return;
  friendshipKeys.add(key);
  push(`    - {relation_type: "friendship", source: "${source}", target: "${target}", payload: {tie_strength: ${fixed(strength, 3)}, channel: "${channel}"}}`);
}

for (let i = 1; i <= 100; i += 1) {
  const source = personId(i);
  const wardOffset = Math.floor((i - 1) / 20) * 20;
  const local1 = wardOffset + ((i - wardOffset) % 20) + 1;
  const local2 = wardOffset + (((i - wardOffset + 4) % 20) || 20);
  const cross = ((i + 36 - 1) % 100) + 1;
  addFriend(source, personId(local1), 0.42 + ((i * 5) % 35) / 100, "chat");
  addFriend(source, personId(local2), 0.30 + ((i * 7) % 30) / 100, "family");
  if (i % 4 === 0) {
    addFriend(source, personId(cross), 0.22 + ((i * 3) % 22) / 100, "market");
  }
}

push("");
push("  initial_events:");
const initialEvents = [
  { type: "subsidy_offer", timestamp: 8.0, priority: 10, handle: "subsidy_day_008", payload: { income_ceiling: 760.0, cost_discount: 6.0, interest_boost: 0.12 } },
  { type: "fiber_cut", timestamp: 18.0, priority: 20, handle: "fiber_cut_ward_3_day_018", payload: { ward: 3, severity: 0.42 } },
  { type: "training_day", timestamp: 24.0, priority: 10, handle: "library_training_day_024", payload: { skill_boost: 0.08, trust_boost: 0.12 } },
  { type: "school_device_rollout", timestamp: 31.0, priority: 10, handle: "school_devices_day_031", payload: { device_boost: 0.22, trust_boost: 0.08 } },
  { type: "rumor_wave", timestamp: 39.0, priority: 12, handle: "rumor_wave_day_039", payload: { intensity: 0.34 } },
  { type: "subsidy_offer", timestamp: 44.0, priority: 10, handle: "subsidy_day_044", payload: { income_ceiling: 900.0, cost_discount: 4.0, interest_boost: 0.08 } },
  { type: "fiber_cut", timestamp: 47.0, priority: 20, handle: "fiber_cut_ward_5_day_047", payload: { ward: 5, severity: 0.31 } },
  { type: "repair_drive", timestamp: 55.0, priority: 15, handle: "repair_ward_3_day_055", payload: { ward: 3, boost: 0.33 } },
  { type: "training_day", timestamp: 61.0, priority: 10, handle: "town_hall_training_day_061", payload: { skill_boost: 0.06, trust_boost: 0.10 } },
  { type: "festival_stream", timestamp: 72.0, priority: 11, handle: "festival_stream_day_072", payload: { demand_boost: 2.1, post_boost: 2.6 } },
  { type: "rumor_wave", timestamp: 74.0, priority: 12, handle: "rumor_wave_day_074", payload: { intensity: 0.22 } },
  { type: "repair_drive", timestamp: 80.0, priority: 15, handle: "repair_ward_5_day_080", payload: { ward: 5, boost: 0.28 } }
];
for (const event of initialEvents) {
  push(`    - event_type: "${event.type}"`);
  push(`      timestamp: ${fixed(event.timestamp, 1)}`);
  push(`      priority: ${event.priority}`);
  push(`      event_handle: "${event.handle}"`);
  push("      payload:");
  for (const [key, value] of Object.entries(event.payload)) {
    push(`        ${key}: ${Number.isInteger(value) ? value : fixed(value, 3)}`);
  }
}

push("");
push("  systems:");
push('    - system_id: "tick_memory_decay_residents"');
push('      triggered_by: ["tick"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push("      writes:");
push('        - target: "EventMemory.rumor_belief"');
push('          expr: "clamp(EventMemory.rumor_belief * 0.985 - InternetBehavior.trusted_info * 0.003, 0, 1)"');
push('        - target: "EventMemory.service_disruption"');
push('          expr: "clamp(EventMemory.service_disruption * 0.93, 0, 90)"');
push('        - target: "InternetBehavior.misinformation_exposure"');
push('          expr: "clamp(InternetBehavior.misinformation_exposure * 0.96 + AccessState.has_internet * rng(\\"ambient_misinfo\\", 0, 0.010), 0, 1)"');
push('        - target: "InternetBehavior.trusted_info"');
push('          expr: "clamp(InternetBehavior.trusted_info * 0.998, 0, 1)"');
push('        - target: "Adoption.churn_risk"');
push('          expr: "clamp(Adoption.churn_risk * 0.96 + EventMemory.service_disruption * 0.002 + EventMemory.rumor_belief * 0.003, 0, 1)"');
push("");
push('    - system_id: "daily_adoption_residents"');
push('      triggered_by: ["tick"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push("      writes:");
push('        - target: "Adoption.interest"');
push('          expr: "clamp(Adoption.interest + 0.006 + PersonProfile.digital_skill * 0.004 + InternetBehavior.trusted_info * 0.003 + AccessState.device_quality * 0.002 - EventMemory.rumor_belief * 0.006 - AccessState.monthly_cost * 0.00035 + rng(\\"adoption_daily\\", 0, 0.012), 0, 1)"');
push('        - target: "Adoption.subscribed"');
push('          expr: "Adoption.subscribed || Adoption.interest > (0.62 + EventMemory.rumor_belief * 0.18 + AccessState.monthly_cost * 0.002)"');
push('        - target: "AccessState.has_internet"');
push('          expr: "Adoption.subscribed"');
push('        - target: "Adoption.days_online"');
push('          expr: "Adoption.days_online + AccessState.has_internet"');
push("");
push('    - system_id: "daily_usage_and_wellbeing_residents"');
push('      triggered_by: ["tick"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push("      writes:");
push('        - target: "InternetBehavior.usage_hours"');
push('          expr: "clamp(AccessState.has_internet * (0.4 + PersonProfile.digital_skill * 2.4 + Adoption.interest * 1.1 + AccessState.device_quality * 1.0 + rng(\\"daily_usage\\", 0, 1.4)) * AccessState.router_health, 0, 9)"');
push('        - target: "InternetBehavior.remote_work_hours"');
push('          expr: "clamp(AccessState.has_internet * AccessState.router_health * ((PersonProfile.role == \\"remote_worker\\") * 5.0 + (PersonProfile.role == \\"teacher\\") * 1.8 + (PersonProfile.role == \\"vendor\\") * 1.2 + (PersonProfile.role == \\"worker\\") * 1.0 + (PersonProfile.role == \\"farmer\\") * 0.5), 0, 8)"');
push('        - target: "InternetBehavior.learning_hours"');
push('          expr: "clamp(AccessState.has_internet * ((PersonProfile.role == \\"student\\") * 3.2 + (PersonProfile.role == \\"teacher\\") * 1.0 + PersonProfile.digital_skill * 0.3), 0, 5)"');
push('        - target: "InternetBehavior.social_posts"');
push('          expr: "clamp(AccessState.has_internet * (InternetBehavior.social_posts * 0.25 + PersonProfile.digital_skill * 1.1 + rng(\\"daily_posts\\", 0, 2.5)), 0, 12)"');
push('        - target: "Wellbeing.productivity"');
push('          expr: "clamp(Wellbeing.productivity + InternetBehavior.remote_work_hours * 0.004 + AccessState.plan_speed * 0.0004 - EventMemory.service_disruption * 0.004 - Adoption.churn_risk * 0.002, 0, 1)"');
push('        - target: "Wellbeing.school_progress"');
push('          expr: "clamp(Wellbeing.school_progress + InternetBehavior.learning_hours * 0.006 + (PersonProfile.role == \\"student\\") * InternetBehavior.trusted_info * 0.003, 0, 1)"');
push('        - target: "Wellbeing.health_access"');
push('          expr: "clamp(Wellbeing.health_access + AccessState.has_internet * ((PersonProfile.role == \\"elder\\") * 0.006 + InternetBehavior.trusted_info * 0.002) - EventMemory.service_disruption * 0.002, 0, 1)"');
push('        - target: "Wellbeing.happiness"');
push('          expr: "clamp(Wellbeing.happiness + AccessState.has_internet * 0.002 + InternetBehavior.usage_hours * 0.0005 - InternetBehavior.misinformation_exposure * 0.003 - EventMemory.service_disruption * 0.004 - AccessState.monthly_cost * 0.0002, 0, 1)"');
push('        - target: "Wellbeing.savings"');
push('          expr: "clamp(Wellbeing.savings + Wellbeing.productivity * 0.8 + AccessState.has_internet * ((PersonProfile.role == \\"vendor\\") * PersonProfile.income * 0.004 + (PersonProfile.role == \\"remote_worker\\") * PersonProfile.income * 0.003 + (PersonProfile.role == \\"farmer\\") * PersonProfile.income * 0.001) - AccessState.monthly_cost / 30, -500, 10000)"');
push("");
push('    - system_id: "reset_network_load"');
push('      triggered_by: ["tick"]');
push('      kind: "per_entity"');
push('      entity_type: "network_hub"');
push("      writes:");
push('        - target: "NetworkNode.load"');
push('          expr: "0"');
push('        - target: "NetworkNode.outage_pressure"');
push('          expr: "clamp(NetworkNode.outage_pressure * 0.94 + NetworkNode.repair_backlog * 0.020, 0, 1)"');
push("");
push('    - system_id: "served_relation_network_effects"');
push('      triggered_by: ["tick"]');
push('      kind: "per_relation"');
push('      relation_type: "served_by"');
push("      writes:");
push('        - target: "self.NetworkNode.load"');
push('          expr: "NetworkNode.load + other.InternetBehavior.usage_hours * relation.demand_weight * (1.0 + other.AccessState.has_internet * 0.15)"');
push('        - target: "other.AccessState.plan_speed"');
push('          expr: "clamp((NetworkNode.capacity / (1 + NetworkNode.load)) * NetworkNode.reliability * 2.8 - relation.distance_km * 0.45, 1, 100)"');
push('        - target: "other.AccessState.router_health"');
push('          expr: "clamp(other.AccessState.router_health + NetworkNode.reliability * 0.002 - NetworkNode.outage_pressure * 0.012 - relation.distance_km * 0.001, 0.25, 1)"');
push('        - target: "other.EventMemory.service_disruption"');
push('          expr: "clamp(other.EventMemory.service_disruption + (NetworkNode.reliability < 0.55) * 0.6 + (NetworkNode.load > NetworkNode.capacity) * 0.4, 0, 90)"');
push("");
push('    - system_id: "daily_social_influence"');
push('      triggered_by: ["tick"]');
push('      kind: "per_relation"');
push('      relation_type: "friendship"');
push("      writes:");
push('        - target: "other.Adoption.interest"');
push('          expr: "clamp(other.Adoption.interest + AccessState.has_internet * relation.tie_strength * 0.003 + InternetBehavior.trusted_info * relation.tie_strength * 0.0015 - EventMemory.rumor_belief * relation.tie_strength * 0.002, 0, 1)"');
push('        - target: "other.InternetBehavior.misinformation_exposure"');
push('          expr: "clamp(other.InternetBehavior.misinformation_exposure + InternetBehavior.misinformation_exposure * relation.tie_strength * 0.002, 0, 1)"');
push('        - target: "other.InternetBehavior.trusted_info"');
push('          expr: "clamp(other.InternetBehavior.trusted_info + InternetBehavior.trusted_info * relation.tie_strength * 0.001, 0, 1)"');
push("");
push('    - system_id: "network_stress_and_slowdown_emission"');
push('      triggered_by: ["tick"]');
push('      kind: "per_entity"');
push('      entity_type: "network_hub"');
push("      writes:");
push('        - target: "NetworkNode.reliability"');
push('          expr: "clamp(NetworkNode.reliability + 0.012 + rng(\\"maintenance\\", 0, 0.012) - (NetworkNode.load / NetworkNode.capacity) * 0.080 - NetworkNode.repair_backlog * 0.006, 0.18, 0.99)"');
push('        - target: "NetworkNode.repair_backlog"');
push('          expr: "clamp(NetworkNode.repair_backlog + (NetworkNode.load / NetworkNode.capacity > 0.95) * 0.8 + (NetworkNode.reliability < 0.5) * 0.6 - 0.18, 0, 80)"');
push("      emit_events:");
push('        - event_type: "service_slowdown"');
push('          timestamp: "event.timestamp + 0.05"');
push("          priority: 0");
push('          when: "NetworkNode.load / NetworkNode.capacity > 0.90 || NetworkNode.reliability < 0.52"');
push("          payload:");
push('            ward: "NetworkNode.ward"');
push('            severity: "clamp(NetworkNode.load / NetworkNode.capacity - 0.85 + (0.55 - NetworkNode.reliability), 0.05, 1.0)"');
push("");
push('    - system_id: "service_slowdown_hits_residents"');
push('      triggered_by: ["service_slowdown"]');
push('      kind: "per_relation"');
push('      relation_type: "served_by"');
push('      where: "NetworkNode.ward == event.ward"');
push("      writes:");
push('        - target: "other.EventMemory.service_disruption"');
push('          expr: "clamp(other.EventMemory.service_disruption + event.severity, 0, 90)"');
push('        - target: "other.Adoption.churn_risk"');
push('          expr: "clamp(other.Adoption.churn_risk + event.severity * (0.03 + other.EventMemory.rumor_belief * 0.02), 0, 1)"');
push('        - target: "other.Wellbeing.happiness"');
push('          expr: "clamp(other.Wellbeing.happiness - event.severity * 0.030, 0, 1)"');
push('        - target: "other.EventMemory.help_requests"');
push('          when: "other.Adoption.churn_risk > 0.30 && rng(\\"ticket_write\\", 0, 1) > 0.88"');
push('          expr: "other.EventMemory.help_requests + 1"');
push("      create_relations:");
push('        - relation_type: "help_ticket"');
push('          source: "other"');
push('          target: "self"');
push('          expires_after: "14"');
push('          when: "other.Adoption.churn_risk > 0.30 && rng(\\"ticket_relation\\", 0, 1) > 0.88"');
push("          payload:");
push('            severity: "event.severity"');
push('            created_day: "event.timestamp"');
push("");
push('    - system_id: "process_help_tickets"');
push('      triggered_by: ["tick"]');
push('      kind: "per_relation"');
push('      relation_type: "help_ticket"');
push("      writes:");
push('        - target: "other.NetworkNode.repair_backlog"');
push('          expr: "max(0, other.NetworkNode.repair_backlog - relation.severity * 0.030)"');
push('        - target: "self.Wellbeing.happiness"');
push('          expr: "clamp(Wellbeing.happiness + 0.001, 0, 1)"');
push("");
push('    - system_id: "apply_fiber_cut"');
push('      triggered_by: ["fiber_cut"]');
push('      kind: "per_entity"');
push('      entity_type: "network_hub"');
push('      where: "NetworkNode.ward == event.ward"');
push("      writes:");
push('        - target: "NetworkNode.reliability"');
push('          expr: "clamp(NetworkNode.reliability - event.severity, 0.10, 0.99)"');
push('        - target: "NetworkNode.repair_backlog"');
push('          expr: "clamp(NetworkNode.repair_backlog + event.severity * 20, 0, 80)"');
push('        - target: "NetworkNode.outage_pressure"');
push('          expr: "clamp(NetworkNode.outage_pressure + event.severity, 0, 1)"');
push("");
push('    - system_id: "apply_repair_drive"');
push('      triggered_by: ["repair_drive"]');
push('      kind: "per_entity"');
push('      entity_type: "network_hub"');
push('      where: "NetworkNode.ward == event.ward"');
push("      writes:");
push('        - target: "NetworkNode.reliability"');
push('          expr: "clamp(NetworkNode.reliability + event.boost, 0.10, 0.99)"');
push('        - target: "NetworkNode.repair_backlog"');
push('          expr: "max(0, NetworkNode.repair_backlog - event.boost * 35)"');
push('        - target: "NetworkNode.outage_pressure"');
push('          expr: "clamp(NetworkNode.outage_pressure - event.boost, 0, 1)"');
push("");
push('    - system_id: "apply_training_day"');
push('      triggered_by: ["training_day"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push("      writes:");
push('        - target: "PersonProfile.digital_skill"');
push('          expr: "clamp(PersonProfile.digital_skill + event.skill_boost * (0.6 + rng(\\"training_attendance\\", 0, 0.6)), 0, 1)"');
push('        - target: "InternetBehavior.trusted_info"');
push('          expr: "clamp(InternetBehavior.trusted_info + event.trust_boost, 0, 1)"');
push('        - target: "EventMemory.rumor_belief"');
push('          expr: "clamp(EventMemory.rumor_belief - event.trust_boost * 0.4, 0, 1)"');
push('        - target: "Adoption.interest"');
push('          expr: "clamp(Adoption.interest + event.skill_boost * 0.5, 0, 1)"');
push("");
push('    - system_id: "apply_subsidy_offer"');
push('      triggered_by: ["subsidy_offer"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push('      where: "PersonProfile.income < event.income_ceiling"');
push("      writes:");
push('        - target: "AccessState.monthly_cost"');
push('          expr: "max(5, AccessState.monthly_cost - event.cost_discount)"');
push('        - target: "Adoption.interest"');
push('          expr: "clamp(Adoption.interest + event.interest_boost, 0, 1)"');
push('        - target: "Adoption.subscribed"');
push('          expr: "Adoption.subscribed || Adoption.interest > 0.48"');
push('        - target: "AccessState.has_internet"');
push('          expr: "Adoption.subscribed"');
push("");
push('    - system_id: "apply_school_device_rollout"');
push('      triggered_by: ["school_device_rollout"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push('      where: "PersonProfile.role == \\"student\\" || PersonProfile.role == \\"teacher\\""');
push("      writes:");
push('        - target: "AccessState.device_quality"');
push('          expr: "clamp(AccessState.device_quality + event.device_boost, 0, 1)"');
push('        - target: "Adoption.interest"');
push('          expr: "clamp(Adoption.interest + event.trust_boost, 0, 1)"');
push('        - target: "InternetBehavior.trusted_info"');
push('          expr: "clamp(InternetBehavior.trusted_info + event.trust_boost, 0, 1)"');
push("");
push('    - system_id: "rumor_wave_social_spread"');
push('      triggered_by: ["rumor_wave"]');
push('      kind: "per_relation"');
push('      relation_type: "friendship"');
push("      writes:");
push('        - target: "other.EventMemory.rumor_belief"');
push('          expr: "clamp(other.EventMemory.rumor_belief + event.intensity * relation.tie_strength * max(0.1, 1 - other.InternetBehavior.trusted_info) + InternetBehavior.misinformation_exposure * 0.05, 0, 1)"');
push('        - target: "other.InternetBehavior.misinformation_exposure"');
push('          expr: "clamp(other.InternetBehavior.misinformation_exposure + event.intensity * relation.tie_strength * 0.20, 0, 1)"');
push('        - target: "other.Adoption.churn_risk"');
push('          expr: "clamp(other.Adoption.churn_risk + event.intensity * relation.tie_strength * 0.035, 0, 1)"');
push("");
push('    - system_id: "festival_stream_spike"');
push('      triggered_by: ["festival_stream"]');
push('      kind: "per_entity"');
push('      entity_type: "resident"');
push("      writes:");
push('        - target: "InternetBehavior.usage_hours"');
push('          expr: "clamp(InternetBehavior.usage_hours + event.demand_boost * AccessState.has_internet * (0.5 + PersonProfile.digital_skill), 0, 12)"');
push('        - target: "InternetBehavior.social_posts"');
push('          expr: "clamp(InternetBehavior.social_posts + event.post_boost * AccessState.has_internet * (0.2 + PersonProfile.digital_skill), 0, 20)"');
push('        - target: "InternetBehavior.misinformation_exposure"');
push('          expr: "clamp(InternetBehavior.misinformation_exposure + event.post_boost * 0.02 * AccessState.has_internet, 0, 1)"');

fs.writeFileSync(outputPath, `${lines.join("\n")}\n`);
console.log(`Generated ${outputPath}`);
console.log(`Residents: ${residents.length}`);
console.log(`Friendship relations: ${friendshipKeys.size}`);
