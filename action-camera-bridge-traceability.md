# Action Camera Bridge — Traceability Ledger

**Purpose.** This document is the evidence that the compression was lossless. It records the
disposition of every statement in the frozen source, the results of the seven audits the compression
rule requires, and — separately — everything the extraction surfaced that would be a **requirement
change** and was therefore *not* applied.

**Semantic baseline.** `action-camera-bridge-prs-source-v1.0.md`
sha256 `e6178686acfa71e932784cf44041a988f9d72e8ff64162c89588bce8a700b473`
3,326 lines / 31,253 words / 76 sections.

**Compression result.** 229 requirements, 37 platform facts, 185 validation cases — **and seven statements
that should have been requirements and were not** (§4b, §5b, §6). The v1.1 delta of §5a took the set to 234
requirements and 192 cases; the v1.2 delta of §5b repaired the compression and took it to **246 requirements
and 201 validation cases.** Both deltas sit deliberately *outside* the equivalence claim.

| Document | Words | Role |
| --- | --- | --- |
| PRS | 15,240 | the normative reading path |
| Platform Evidence | 7,129 | why each requirement is correct at the pinned tags |
| Validation | 11,022 | how conformity is demonstrated |
| Ledger (this file) | 7,178 | audit record |
| Compression Rule | 2,577 | the method, amended by CR-01 and audits 5b, 6, 7 |

*(Word counts are post-v1.2. At the compression boundary the PRS was 12,207 and Validation 9,377; after v1.1
they were 12,910 and 9,963.)*

**On the word count.** The normative reading path fell from 31,253 words to 12,207 — a **61 %
reduction in what an implementer must read to know what to build**. The *total* across four documents
is slightly larger than the monolith, because IDs, the Verifies column and this ledger are new text.
That is the expected outcome and not a failure: the compression rule's acceptance criterion is the set
of admitted implementations, not the byte count.

---

## 1. Requirement-equivalence audit

Every section of the source, and what became of it. Dispositions:
**R** retained as a requirement · **M** merged into a requirement · **E** moved to Evidence as
non-normative · **V** moved to Validation as test-only · **D** deleted as exact duplication ·
**X** **removed by reviewed product decision** *(added in v1.2)*.

**`X` exists because `D` was the only disposition that removed anything, and `D` asserts "this said nothing
new."** A statement dropped because the product no longer wants it is a different act. Recording it as `D`
asserts something false; recording it nowhere makes a deliberate decision indistinguishable from the
oversight these audits exist to catch. **Every `X` names the change request that decided it.**

| v1.0 § | Subject | Disp. | Destination |
| --- | --- | --- | --- |
| Header | status, versions, architecture, distribution | R | PRS header, `SCOPE-01` |
| Header note | "content-identical to v0.9 apart from §16…" | D | revision history — superseded by this ledger |
| 0 | citation rule, version contract, standing rule | R+E | `SCOPE-01`, `SCOPE-02`; rule → Evidence §0 |
| 0 | three master-vs-baseline corrections | E | Evidence §0, `PF-BF-08`, `PF-INAV-04`, `PF-INAV-10` |
| 1 | product goal, bullet list | R | `PROD-01`…`PROD-03` |
| 2 | core UX, 7-step walkthrough | M | `PROD-02` — the steps restate `FC-03`/`RF-02`/`CAM-04`/`CTRL-01` |
| 3 | passenger claim, non-participation list, diagram | R | `SAFE-01` |
| 3.1 | closed-by-default policy | R | `SAFE-02` |
| 3.1 | *why* a denylist fails (empirical argument) | E | Evidence §4.1 |
| 3.1.1 | the allowlist table (18 rows) | R | `SAFE-03` |
| 3.1.1 | "exactly two permitted writes" | R | `SAFE-04` |
| 3.1.2 | `MSP_RTC` 247/246 swap + adapter rule | R+E | `SAFE-05`; citation → `PF-BF-02` |
| 3.1.3 | why an implementer reaches for prohibited commands | E | Evidence §4.1 — explicitly non-normative in v1.0 already |
| 3.1.3 | "reads of configuration are permitted; writes are not" | M | `SAFE-02` + Evidence §4.1 |
| 3.2 | electrical isolation | R | `SAFE-06` |
| 3.3 | RF coexistence, 5 numbered requirements | R | `RF-01`…`RF-05` |
| 3.3 | arm-state independence | R | `RF-06` |
| 3.3 | desense narrative, "adverse case is camera loss" | E | PRS keeps the one-line consequence; argument → PRS §2.3 preamble |
| 4.1 / 4.2 | four-wire interface, peak current | R | `FC-01`, `FC-02` |
| 5 | identification commands, supported variants | R | `FC-03` |
| 5.1 | version floor (block quote) | R | `FC-05` |
| 5.1 | fixed ordering | R | `FC-04` |
| 5.1 | *why the floor gates camera control* | E | Evidence §4.2 |
| 5.1 | byte-triple comparison + 3 encoding properties | R+E | `FC-06`; derivation → `PF-BF-04` |
| 5.1 | capability probing, per platform | R+E | `FC-07`; `PF-BF-05`, `PF-INAV-09` |
| 5.1 | two degraded states table | R | `FC-08`, `FC-09`, `FC-10` |
| 5.2 | armed state is not a control input; forbidden/permitted block | R | `FC-11` |
| 5.2 | `MSP_BOXIDS` scoping | R | `FC-12` |
| 5.2 | "the rule is deliberately the weaker one"; Arm Record note | E | Evidence §4.3 |
| 5.2 | `updateArmingStatus` else-branch citation | E | `PF-INAV-02` |
| 5.2 | the invariant §24.1 tests | R | `FC-13` |
| 6 | camera abstraction, state set, capability flags | R | `CAM-01`…`CAM-03` |
| 7 | truth model, state list | R | `CAM-04` |
| 8 | uppercase rule, four fields, blank-not-estimated | R | `OSD-01`, `OSD-02`, `OSD-03` |
| 9 | BF minimum version + no-2025.12.0 argument | M+E | `SCOPE-01`; argument → `PF-BF-01` |
| 9 | transport, constants table, payload | R+E | `OSD-07`; constants → `PF-BF-16` |
| 9 | armed behaviour, persistence, "not the safer platform" | E | `PF-BF-03`, `PF-BF-17` |
| 9 | user enables OSD elements | R | `OSD-17` |
| 9.1 | slot ownership | R | `OSD-04` |
| 9.2 | update policy table + no-redundant-write | R | `OSD-05`, `OSD-06` |
| 10 | INAV minimum version, injection confirmed | M+E | `SCOPE-01`; `PF-INAV-13` |
| 10 | transport, constants table | R+E | `OSD-08`; `PF-INAV-09` |
| 10.1 | 28-byte payload, field values, whole-element rewrite | R | `OSD-08` |
| 10.1 | armed behaviour, other-guards caveat, persistence | E | `PF-INAV-10` |
| 10.1 | persisted-text hazard diagram | E | `PF-BF-17` |
| 10.1 | **owned-slot initialisation requirement** | R | `OSD-09` |
| 10.1 | residual dead-bridge case | E+M | `PF-BF-17`; note → `INST-04` |
| 10.2 | parity, element ownership | M+E | `OSD-04`; parity → `PF-INAV-10` |
| 10.3 | hard constraint | R | `OSD-11` |
| 11 | video-system independence | R | `OSD-10` |
| 12 | control model, prohibitions, explicit ranges | R | `CTRL-01`, `CTRL-02`, `CTRL-03` |
| 12.1 | four control modes | R | `CTRL-04`, `CTRL-05` |
| 12.1.1 | Combi model + transition table | R | `CTRL-06` |
| 12.1.1 | requirements 1–4 | R | `CTRL-07`…`CTRL-10` |
| 12.1.1 | level-control default | R+E | `CTRL-11`; reason → Evidence §4.4 |
| 12.1.1 | setups without an arm switch (2-row cited table) | R+E | `INST-05`; citations → `PF-BF-20`, `PF-INAV-12` |
| 12.2 | `MSP_RC` serialization, AUX-only, response length | R+E | `CTRL-12`, `CTRL-13`; numeric note → `PF-BF-06`, `CTRL-14` |
| 12.2.1 | hysteresis, grid argument, min width, evaluator-only | R+E | `CTRL-15`…`CTRL-17`; grid → `PF-BF-07` |
| 12.3 | Push Button timing, 5 bullets | R | `CTRL-18`…`CTRL-23` |
| 12.4 | common semantic | R | `RCV-01` |
| 12.4 (BF) | failsafe substitution, hazard scope, freeze requirement | R+E | `RCV-02`; mechanism → `PF-BF-10` |
| 12.4 (BF) | guard validity while armed | E | `PF-BF-09` |
| 12.4.1 | bracket + permitted optimisation | R | `RCV-03`, `RCV-04` |
| 12.4.1 | data-leads-flag ordering diagram | E | `PF-BF-10` |
| 12.4.2 | assert/clear edges, no-encode rule, no `BOXFAILSAFE` case | R+E | `RCV-05`; `PF-BF-11`, `PF-BF-12` |
| 12.4.2 | consequences 1–4 | R+M | `RCV-06`, `RCV-07`; 4 → `CTRL-08` |
| 12.4.3 | qualification criterion block (A/B, grace, margin) | R | `RCV-08` |
| 12.4.3 | "a clear guard is not proof" + 3 assertion paths | E | `PF-BF-13` |
| 12.4.3 | init-value failure table | E | `PF-BF-13` |
| 12.4.3 | detail 1 — U8 tenths + floor | R+E | `RCV-09`; `PF-BF-15` |
| 12.4.3 | detail 2 — margin derivation | R+E | `RCV-08`, `RCV-10`; derivation → `PF-BF-14` |
| 12.4.3 | detail 3 — fallback to A only | R | `RCV-11` |
| 12.4.3 | sufficiency table (4 cases) + row-three note | E | `PF-BF-13` |
| 12.4.3 | closing requirements bullets | R | `RCV-12`…`RCV-15` |
| 12.4 (INAV) | two freeze mechanisms, no gate needed | R+E | `RCV-16`; `PF-INAV-05` |
| 12.4.4 | mid-stick init | E | `PF-INAV-06` |
| 12.4.4 | `ARMING_DISABLED_RC_LINK` mechanism | E | `PF-INAV-07` |
| 12.4.4 | the one trap — armed branch | E | `PF-INAV-02` |
| 12.4.4 | requirement block | R | `RCV-17`, `RCV-18` |
| 12.4.4 | known consequence | R | `RCV-19` |
| 12.5 | what `MSP_RC` contains; LC override; 8.0.1 note | R+E | `CTRL-24`; `PF-INAV-03` |
| 12.6 | overlap advisory + EXTRA caveat | R+E | `CTRL-25`, `CTRL-26`; `PF-BF-08` |
| 12.7 | LC/GVAR trigger; `..._CONFIGURED` prohibition | R+E | `CTRL-27`, `CTRL-28`; `PF-INAV-04` |
| 13 | camera mode / preset control (V1.2) | R | `SCOPE-06` |
| 14 | GPS and time push | R+E | `CAM-13`; ID hazard → `SAFE-05`, `PF-BF-02` |
| 15 | pairing, directed reconnect, discovery-is-explicit | R | `CAM-09`, `RF-02`, `RF-03` |
| 16 | setup surface list | R | `SETUP-25` |
| 16 | four entry authorities table | R | `SETUP-01` |
| 16 | authority-4 honesty; the narrower guarantee | R | `SETUP-02` |
| 16 | hub structure | R | `SETUP-04` |
| 16 | per-entry-not-per-enrolment | R | `SETUP-03` |
| 16 | bonded-BLE rejection argument | E | Evidence §4.9 |
| 16 | operating-procedure note | R | `SETUP-26` |
| 16 gesture | 9 numbered requirements | R | `SETUP-05`…`SETUP-14` |
| 16 gesture | AUX-not-inherited reasoning; sample budget | E | Evidence §4.11 |
| 16 gesture | recogniser table | R | `SETUP-12` |
| 16 passthrough | 4 verbs, no-flasher rule, 4 requirements | R | `SETUP-15`…`SETUP-19` |
| 16 passthrough | exit-behaviour citations (BF and INAV) | E | `PF-BF-18`, `PF-BF-19`, `PF-INAV-11` |
| 16.1 | four-mode table | R | `BOOT-01`, `BOOT-02` |
| 16.1 | boot-selection block + rules 1–5 | R | `BOOT-03`…`BOOT-10` |
| 16.1 | transitions diagram | M | `BOOT-11`, `BOOT-12` — the diagram restates them |
| 16.1 | operational-failure-is-normal | R | `BOOT-11` |
| 16.1 | `PAIR` 120 s lifetime | M | `PAIR-03`, `PAIR-04` — one canonical home |
| 16.1 | exiting is a fresh start; structural isolation | R+E | `BOOT-12`; argument → Evidence §4.10 |
| 16.1 | reboot-is-maintenance-only block quote | R | `BOOT-13` |
| 16.1 | obligation 1 — never autonomous | R | `BOOT-16` |
| 16.1 | obligation 2 — heap, peaks, floors, soak, provisional | R+E | `RES-01`…`RES-08`; argument → Evidence §4.7 |
| 16.1 | obligation 3 — MSP link | R | `BOOT-15` |
| 16.1 | obligation 4 — OSD content | R | `BOOT-14` |
| 16.2 | standalone-only scoping | R | `ARCH-03` |
| 16.2 | purpose flow diagram | D | restates `LEARN-*`, `SETUP-20`, `DIAG-*` |
| 16.2.1 | monitor content bullets | R | `SETUP-20` |
| 16.2.1 | same-implementation rule | R | `SETUP-21` |
| 16.2.1 | no live policy in `SETUP` + `manualStop` root-cause | R+E | `SETUP-24`; argument → Evidence §4.10 |
| 16.2.1 | dry run issues no command; explicit `UNQUALIFIED` | R | `SETUP-22`, `SETUP-23` |
| 16.2.2 | pairing surface; consistency-with-§15 argument | R+E | `PAIR-01`, `PAIR-02` |
| 16.2.2 | `PAIR` boot flow; parameterised target | R | `PAIR-01` |
| 16.2.2 | bounds table; maxima-not-durations | R | `PAIR-03`, `PAIR-04` |
| 16.2.2 | 120 s cap reasoning | E | Evidence §4.6 |
| 16.2.2 | outcome table | R | `PAIR-05` |
| 16.2.2 | candidate cache, 8 entries, truncation | R | `PAIR-06` |
| 16.2.2 | scan token | R | `PAIR-07` |
| 16.2.2 | selection rules; no-strongest-advertiser | R | `PAIR-08`, `PAIR-09` |
| 16.2.3 | required in `SETUP`; identical in `DIAG` | R | `LEARN-01` |
| 16.2.3 | needs no camera/BLE; not peak-2 conditional | R | `LEARN-02` |
| 16.2.3 | consumer diagram + mode table | M | `BOOT-02`, `SETUP-24`, `DIAG-06` |
| 16.2.3 | explicit start only | R | `LEARN-03` |
| 16.2.3 | 20 Hz polling requirement | R | `LEARN-04` |
| 16.2.3 | actuation suspended | R | `LEARN-05` |
| 16.2.3 | predicate block | R | `LEARN-06` |
| 16.2.3 | "why each figure" bullets | E | Evidence §4.5 |
| 16.2.3 | not-§12.2.1's-hysteresis note | R | `LEARN-06` closing paragraph |
| 16.2.3 | three outcomes | R | `LEARN-07`, `LEARN-17` |
| 16.2.3 | characterisation; no nominal values | R | `LEARN-08` |
| 16.2.3 | inference is a proposal | R | `LEARN-09` |
| 16.2.3 | admissibility | R | `LEARN-10` |
| 16.2.3 | confirmation re-baselines | R | `LEARN-11` |
| 16.2.3 | persisted representation | R | `LEARN-12`, `LEARN-13` |
| 16.2.3 | learnable actions | R | `LEARN-14` |
| 16.2.3 | capability gating | R | `LEARN-15` |
| 16.2.3 | learner never creates an authority | R | `LEARN-16` |
| 16.2.4 | V1.2-whole framing | R+E | `DIAG-01`; argument → Evidence §4.8 |
| 16.2.4 | path diagram, taxonomy, example | R | `DIAG-03`, `DIAG-04` |
| 16.2.4 | 7 requirement bullets | R | `DIAG-02`, `DIAG-05`…`DIAG-09`, `LEARN-01` |
| 16.2.4 | modes unmistakable | R | `DIAG-10` |
| 16.2.4 | resource/coexistence is functional, not §3.3 | E | Evidence §4.8 |
| 17 / 17.1 | freshness fields; threshold table | R | `CAM-05`, `CAM-06`, `CAM-07` |
| 18 | warning list | R | `OSD-12` |
| 18 | `REC STOPPED` priority + intent comparison | R | `OSD-13` |
| 18 | `REC STOPPED` during a freeze + scenario | R | `OSD-14` |
| 18 | `REC STOPPED` requires a camera that said so | R | `OSD-15` |
| 18 | `UNQUALIFIED` raises no warning | R | `OSD-16` |
| 19 | field-updatable; Setup-only; no interlock; version-tied claims | R | `UPD-01`…`UPD-05` |
| 20 | Verified / Expected / Unsupported policy | R | `CAM-08` |
| 20.1 | Open GoPro | **R+E** *(corrected in v1.2 — was `E`)* | `CAM-14`; citation → `PF-EXT-01` |
| 20.2 | Nano status, protocol families, hypothesis, licence | R+E | `CAM-10`…`CAM-12`; detail → `PF-EXT-02`, `PF-EXT-03` |
| 20.2 | *"Should DJI later add the Nano to the official protocol, the official path is preferred over DUML"* | **X** *(v1.2, CR-12)* | **No destination — removed by decision.** Identified as normative source content; lost to Evidence by the CR-07 defect; then **deliberately not restored**, not overlooked a second time. `PF-EXT-03` records that no requirement covers the case |
| 21.1 | polling table + 6 explanatory paragraphs | R | `MSP-01`…`MSP-05` |
| 21.1 | worst-case budget figures | E | Evidence §4.13 |
| 21.1 | cache/no-block/incomplete-bracket rule | R | `MSP-07`, `MSP-08` |
| 21.2 | quiet-bridge bullets | R | `MSP-06` |
| 21.3 | 5 numbered requirements | M | `RF-02`, `RF-03`, `RF-07`, `RF-08`, `RF-04`, `RF-09` |
| 22 | failure list (27 entries) | R | `FAIL-01`, `FAIL-02` |
| 23 | 8 installation steps + flow diagram | R | `INST-01` |
| 23 | manual entry is the fallback; no CLI; doc note | R | `INST-02`…`INST-04` |
| 24.1 | 87 acceptance rows | V | `VAL-FUNC-01`…`VAL-FUNC-89` |
| — | *`FC-10` exposure obligation, carried only by a 24.1 row* | **R+V** *(corrected in v1.2)* | `FC-10` |
| 24.2 | 58 failure rows + 3 closing notes | V | `VAL-FAIL-01`…`VAL-FAIL-58`, §2.1 |
| — | *five rows carrying the only statement of their expected behaviour* | **R+V** *(corrected in v1.2 — were `V`)* | `FC-14`, `CAM-15`, `RES-10`, `LEARN-18`, `SETUP-27` |

**§20.1, §24.1 and §24.2 were dispositioned wholesale, and that is where the compression failed.** The source
made §24.1 and §24.2 release conditions — *"a release shall not claim platform support until §24.1 and §24.2
pass"* — so their rows were **normative by reference**. Classifying 145 rows as `V` in two lines moved five
requirements out of the contract and left them stated only as tests. §20.1's `shall` went to Evidence under
the same reflex CR-01 was raised against. Audits 6 and 5b now catch both.
| 25 | core/adapter diagram; seam; `valid` is the only difference | R | `ARCH-01`, `ARCH-02` |
| 25 | boot modes are standalone-only | R | `ARCH-03` |
| 25 | ELRS needs separate acceptance; §3.3 applies doubly | R | `ARCH-05` |
| 25 | integrated build removes the bracket at source | E | Evidence §4.12 |
| 25 | startup qualification does not disappear | R | `ARCH-04` |
| 26 | V1 required list | R | `SCOPE-03` |
| 26 | V1.1 conditional | R | `SCOPE-04` |
| 26 | V1.2 conditional; `DIAG` whole; peak-2 does not block V1 | R+E | `SCOPE-05`, `SCOPE-07`; argument → Evidence §4.8 |
| 26 | explicitly out of scope | R | `SCOPE-08` |
| 27 preamble | six spikes | V | Validation §3 |
| 27.1 | 21 pass criteria | V | `VAL-SPIKE-01` |
| 27.2 | method, conditions, PASS formula, 4 criteria | V | `VAL-SPIKE-02` |
| 27.3 | 3 pass criteria | V | `VAL-SPIKE-03` |
| 27.4 | 5 probe steps, outcome table | V | `VAL-SPIKE-04` |
| 27.5 | narrow target, 6-row check table, fallback | V+R | `VAL-SPIKE-05`; fallback → `BOOT-05` |
| 27.6 | 3 questions, outcome table, closed open-item | V | `VAL-SPIKE-06` |
| 27.6 | "related open item — closed 2026-08-26" | D | its content is `PF-BF-20` / `PF-INAV-12`; the correction record is §5 below |

**Unexplained deletions: none.** Every source statement appears above exactly once.

---

## 2. Reverse audit

*Read the compressed PRS independently: could an implementation satisfy it while violating behaviour
required by the source?*

Eight candidate losses were checked. Six were false alarms; **two were real and are fixed.**

| # | Candidate loss | Verdict |
| --- | --- | --- |
| 1 | Merging `CTRL-08` (`manualStop` frozen) with `CTRL-06` would let an implementation clear the override on freeze entry | **Kept separate.** They are different behaviours and `VAL-SPIKE-01` item 10 tests them independently |
| 2 | Compressing `RCV-05`+`RCV-06` to "wait for the guard" loses "assert is immediate, clear is delayed" | **False alarm.** `RCV-06`'s "holds past the return of valid pilot input" states the asymmetry behaviourally; the mechanism is `PF-BF-11` |
| 3 | `RCV-14` merges four re-baseline paths into one rule | **Intended, and stronger.** The source explicitly says it is one rule; enumerating four invites four implementations. `VAL-FAIL-03`/`04`/`15` and `VAL-FUNC-59` cover the paths separately |
| 4 | The `PAIR` 120 s cap appeared in both §16.1 and §16.2.2; keeping only `PAIR-03`/`PAIR-04` could lose "enforced independently of every operation inside it" | **Real risk — preserved.** `PAIR-04` carries the independence clause verbatim, and `VAL-FAIL-38` tests exactly it |
| 5 | Command IDs moved to Evidence would let a shared `MSP_RTC` constant conform | **Real defect in the rule's own classification table — corrected.** IDs, encodings, bit positions and orderings are inputs to conformance and are stated in the PRS. See §5, CR-01 |
| 6 | `MSP-01`'s table lost the "only when an AUX control is enabled" qualifier and its exception | **False alarm.** The qualifier is in the table row and `MSP-04` states the exception; `VAL-FAIL-49` tests it |
| 7 | `SETUP-02` compresses four authority-strength arguments into one guarantee | **Correct compression.** The source itself says the narrower statement is "the one this section actually rests on"; the authority-4 honesty clause is retained because it changes what may be claimed |
| 8 | Dropping §16.2's flow diagram loses the SETUP→DIAG ordering of learn/observe/validate | **Real — recovered.** `INST-01` steps 4 and 7 carry the ordering normatively |

---

## 3. Boundary-value audit

Every numeric value, threshold, count and boundary in the PRS, with the requirement that owns it.
**Each has exactly one canonical home.**

| Value | Requirement | Kind |
| --- | --- | --- |
| Betaflight 2025.12.5 / INAV 8.0.1 | `SCOPE-01` | version floor |
| byte triple `(25,12,5)` / `(8,0,1)` | `FC-06` | encoding |
| 115200 baud default | `FC-01` | configuration |
| `MSP_RTC` 247 (BF) / 246 (INAV) | `SAFE-05` | command ID |
| all 18 allowlist command IDs | `SAFE-03` | command ID |
| exactly 2 permitted writes | `SAFE-04` | count |
| ±10 µs hysteresis | `CTRL-15` | threshold |
| 4 × hysteresis = 40 µs minimum range width | `CTRL-16` | threshold |
| 2 × hysteresis boundary separation | `CTRL-16` | threshold |
| 2 consecutive samples (Push Button) | `CTRL-18` | count |
| 250 ms documented minimum hold | `CTRL-19` | timing |
| 400 ms default event separation | `CTRL-20` | timing |
| 10 Hz / 20 Hz poll rates | `CTRL-21`, `MSP-01` | rate |
| channels 1–4 excluded | `CTRL-12` | scope |
| 1 µs — explicitly meaningless | `CTRL-14` | negative bound |
| `grace = max(failsafe_delay × 100 ms, 100 ms) + margin` | `RCV-08` | formula |
| `margin = max(2 × poll period, 100 ms)` | `RCV-08` | formula |
| 100 ms `failsafe_delay` floor | `RCV-09` | threshold |
| one poll period per failed transaction | `RCV-10` | formula |
| zero successful observations ⇒ never qualifies | `RCV-10` | absolute |
| `STATUS₁ → RC → STATUS₂` ordering | `RCV-03` | ordering |
| `ARMED` clear **and** `RC_LINK` clear | `RCV-17` | predicate |
| 3 s → `CAM STALE`; 10 s → `CAM LOST` | `CAM-06` | timing |
| 4 OSD fields, ≤ 15 characters | `OSD-02` | count |
| BF 4 slots × 16 chars; subtype base 7 | `OSD-07` | count |
| INAV exactly 28 bytes; 8 elements × 15 chars | `OSD-08` | payload |
| 1 Hz timer, ≤ 1 Hz battery/storage | `OSD-05` | rate |
| owned-slot write **before** camera connect | `OSD-09` | ordering |
| `ESP_RST_SW` + valid request ⇒ special mode | `BOOT-03` | predicate |
| invalidate **before** execute | `BOOT-06` | ordering |
| every other reset reason ⇒ `RUN` | `BOOT-07` | absolute |
| `ESP_RST_POWERON` only (gesture) | `SETUP-08` | predicate |
| 10–15 s gesture window | `SETUP-10` | timing |
| 4 transitions, alternating, ending at baseline | `SETUP-12` | pattern |
| 2 consecutive qualified samples per transition | `SETUP-12` | count |
| 3.0 s pattern timer, from the **first** transition | `SETUP-12` | timing |
| 4 maintenance verbs; 0 flasher verbs | `SETUP-15` | count |
| 20 Hz learning rate | `LEARN-04` | rate |
| median of 5 qualified samples | `LEARN-06` | predicate |
| ≥ 100 µs over ≥ 2 consecutive samples | `LEARN-06` | threshold |
| 5 samples, max − min ≤ 20 µs | `LEARN-06` | threshold |
| ≥ 2 channels ⇒ ambiguous | `LEARN-06` | count |
| 15 s acquisition timeout | `LEARN-06`, `LEARN-17` | timing |
| 30 s scan / 15 s bind / 120 s lifetime | `PAIR-03` | timing |
| ≤ 8 cache entries, truncation reported | `PAIR-06` | count |
| exactly 1 candidate ⇒ auto; ≥ 2 ⇒ explicit | `PAIR-08` | predicate |
| ≥ 32 KiB free heap | `RES-03` | **provisional →** `VAL-SPIKE-03` |
| ≥ 16 KiB largest allocatable block | `RES-03` | **provisional →** `VAL-SPIKE-03` |
| no monotonic degradation | `RES-04` | predicate |
| `RUN` ≥ 30 min / ≥ 10 cycles; others ≥ 60 s | `RES-05` | timing |
| backoff ceiling ≥ 5 s | `RF-07` | threshold |
| duty-cycle ceiling | `RF-08` | **provisional →** `VAL-SPIKE-02` |
| BLE TX power | `RF-04` | **provisional →** `VAL-SPIKE-02` |
| GPS push ≤ 10 Hz | `CAM-13`, `MSP-01` | rate |
| `PASS if abs(Δ) ≤ max(step, 2σ)` | `VAL-SPIKE-02` | formula |

**Vague-term scan.** `sufficient` / `reasonable` / `adequate` / `appropriate` / `acceptable` / `enough`
across the PRS: **0 remaining as an undelegated threshold.** Three occurrences carried over from v1.0
were completeness statements rather than thresholds and were rephrased (`FC-11`, `RCV-18`, `SETUP-11`);
one is logged as CR-04 below rather than changed. `bounded` and `stable` appear only as **defined
terms**, each within two lines of the number that defines it.

**Delegated values: 4**, each naming both its closing measurement and its write-back destination
(`RES-03`, `RF-04`, `RF-08` — and `BOOT-05`'s fallback, which names its triggering check).

---

## 4. Traceability audit

Mechanically verified over the whole set. **Figures below are post-delta**; the parenthesised value is the
figure at the compression boundary, before §5a.

```
requirements defined          246     (229 at the compression boundary, 234 after v1.1)
duplicate definitions           0
gaps in ID numbering            0
dangling requirement refs       0     (all five artifacts)
deontic statements in Evidence 14     (audit 5b: 0 new obligations; 2 repaired in v1.2)
platform facts defined         37
dangling platform-fact refs     0
unreferenced platform facts     0
validation cases              201     (123 FUNC · 59 FAIL · 13 REV · 6 SPIKE)
validation cases with no requirement    0
requirements with no validation case    0
undelegated vague terms in the PRS      0
```

The requirement↔test relation is **many-to-many by design**: `VAL-FUNC-49` verifies four predicates of
`LEARN-06`, and `RCV-08` is verified by seven cases. The audit asserts non-emptiness in both directions,
not a bijection.

**Platform-dependent requirements** each carry at least one `PF-*` reference at the pinned baseline:
`FC-05`…`FC-07`, `CTRL-14`…`CTRL-16`, `CTRL-24`, `CTRL-26`, `CTRL-27`, `RCV-02`…`RCV-19`, `OSD-01`,
`OSD-07`…`OSD-09`, `SAFE-05`, `SAFE-07`, `SETUP-18`, `SETUP-19`, `INST-05`, `CAM-10`…`CAM-12`.

**Unresolved platform assertions: 0.** Every `[BF]`/`[INAV]` claim in Evidence carries a file and line
at the pinned tag.

---

## 4a. Conformance-input audit

*Added to the method after the first application (`action-camera-bridge-compression-rule.md` §6.5).
It catches CR-01-class defects directly rather than indirectly through the deletion test.*

Every literal an implementation consumes was enumerated by category and checked for presence in the
**PRS**, not merely somewhere in the set. A literal reachable only from Evidence is a defect: it means an
implementer must retrieve a semantic fact from a document that is not the contract.

```
conformance inputs enumerated  108
present in the PRS             108
evidence-only (defects)          0
absent from both                 0
```

| Category | Inputs | Evidence-only |
| --- | --- | --- |
| Command names | 18 | 0 |
| Numeric command IDs | 16 | 0 |
| Subtypes / element type constants | 4 | 0 |
| Bit and flag names | 5 | 0 |
| Payload layout literals | 6 | 0 |
| Wire encodings | 4 | 0 |
| State values | 13 | 0 |
| Timings | 16 | 0 |
| Thresholds | 8 | 0 |
| Ordering rules | 5 | 0 |
| Reset reasons | 2 | 0 |
| Modes and protocol verbs | 8 | 0 |
| Formulae | 4 | 0 |

**Answer to the audit's question — *can an implementation be written correctly without retrieving any
additional semantic fact from Evidence?* — is yes.** Evidence carries citations, derivations and
rationale for all 108 inputs and originates none of them.

---

## 4b. Validation-entailment audit *(audit 6, added after external review)*

Run over all 201 validation cases in both directions.

```
cases audited                                  201
hidden requirements found (forward leg)          6   all restored, see §5b
decorative citations found (reverse leg)         2   VAL-REV-06/FC-02, VAL-FUNC-104/FC-10
```

The six were `FC-14`, `CAM-15`, `RES-10`, `LEARN-18`, `SETUP-27` and `CAM-14`. Every one had a validation
case that was **not an orphan** — each cited two or three requirements — so the traceability audit passed them
all. None of the cited requirements said what the case actually checked.

Of the two reverse-leg hits, `VAL-REV-06` cited `FC-02` while testing solder joints and baud rate, never peak
current or bulk capacitance; and `VAL-FUNC-104` required FC metadata to be *"tracked and exposed"* where
`FC-10` said only *track*. **The first hid an untested requirement behind a green count; the second let a test
strengthen the contract without a requirement saying so.** Both are now repaired at the requirement, not by
weakening the test.

---

## 4c. Internal-consistency audit *(audit 7, added after external review)*

Nine contradictions, **all inherited from v1.0** and all preserved faithfully by the compression:

| Conflict | Resolved by |
| --- | --- |
| `SAFE-03`'s closed allowlist vs the traffic `MSP-01` requires | `SAFE-03` `When` cells made exhaustive |
| `FC-05`/`FC-09` OSD prohibition vs `OSD-09`/`BOOT-14`/`SETUP-14` obligations | `OSD-19` |
| `PAIR-03` first-hit scan vs `PAIR-08`'s *exactly one* | `PAIR-11`, `PAIR-03` |
| `PAIR-08`'s `may` reaching an undefined outcome | `PAIR-08` made `shall` |
| `CTRL-21`'s absolute 20 Hz vs `MSP-08`'s unbounded reduction | `MSP-08` floor |
| `CTRL-15` undefined in the hysteresis band at re-baseline | `CTRL-30` |
| `SETUP-17`'s assumption false and its test unrunnable | `SETUP-17`, `SETUP-28` |
| `SETUP-02` claiming authority 3 proves presence | `SETUP-02` |
| `OSD-12` warnings with no semantics or arbitration | `OSD-20` |

**These are defects in the source, not in the compression** — which is why they belong to a normative delta
and not to the equivalence claim. They surfaced because extraction is the most thorough read a specification
gets, and then survived five audits because every one of those audits ran between documents rather than within
one.

---

## 5. Change-request log

**Found by the extraction; deliberately NOT applied.** Compression may not change requirements. Each
item below needs a separate decision.

### CR-01 — the classification rule itself had a defect · **CLOSED, method amended**

The compression rule's destination table sends every "platform fact / source evidence" to Evidence. That
is wrong for a whole class of facts. `MSP_RTC = 247/246` is a platform fact whose loss from the normative
document would let a conforming implementation **set the flight controller's clock**. Same for the
`MSP_FC_VERSION` byte encoding, INAV's 28-byte payload, `ARMING_DISABLED_RC_LINK`'s bit 18, Betaflight's
subtype base 7, and the `STATUS₁→RC→STATUS₂` ordering.

**Resolution applied:** the rule's own test is authoritative over its table — *if removing the sentence
could allow two conforming implementations to behave differently, it belongs in the PRS.* Facts that are
**inputs to conformance** stay normative; facts that **justify** a requirement move to Evidence. Recorded
in PRS §0 as a standing reading rule. **No requirement changed.**

### CR-02 — `PAIR-06`'s "only enough identity" is not a testable bound · **CLOSED in v1.1**

`PAIR-06` requires the cache to "hold only enough identity to let the user choose and the bridge later
direct a bind." Two implementations could store different field sets and both conform. This is a
minimisation constraint bounded by two named functional needs rather than a threshold, and it was
identically vague in v1.0 — so it is **logged, not fixed**. Closing it means naming the permitted fields.

### CR-03 — `SETUP-10`'s window is a range, not a value · **CLOSED in v1.1**

"10–15 s" is a **range with no rule selecting a point in it.** Two conforming implementations will give
users measurably different windows. v1.0 has the same range. Closing it means either fixing a value or
stating the range as a configurable with a default.

### CR-04 — parameters delegated to measurements not yet run · **CLOSED by process, v1.1**

Not a defect — it is the delegation pattern the rule permits — but **three of the four delegated values
are closed by `VAL-SPIKE-02` and `VAL-SPIKE-03`, neither of which has run.** A release cannot be claimed
against a provisional floor. Tracked here so the write-back is not forgotten.

### CR-05 — `CTRL-04` mode 2 makes Arm Record a redundant mode · **CLOSED in v1.1**

v1.0 says "Arm Record is functionally this mode with a conventional setup choice." It is a **preset, not a
control mode**, and listing it as mode 1 of four means an implementation may build four evaluators where
three suffice. Retained as written to preserve v1.0's numbering and the `INST-01` step-4 reference.
Closing it means demoting Arm Record to a documented preset over `CTRL-04` mode 2.

### CR-06 — no requirement states the OSD update **latency** budget · **CLOSED in v1.1**

`OSD-05` bounds update *rates* and `CAM-06` bounds *staleness*, but nothing bounds the interval between a
camera state change and the corresponding OSD write. The product's primary value claim is
camera-**confirmed** state; a conforming implementation could add a second of latency to every transition.
Absent from v1.0 as well.

---

### CR-07 — audit 5 ran in only one direction · **CLOSED, method amended**

It asked *is every conformance input in the PRS?* and answered 108/108. It never asked whether Evidence
contained an obligation of its own. Two did (`CAM-14`, and the DJI-official-path clause). **CR-01 recurred
under the audit written to prevent CR-01**, because that audit could only check literals someone thought to
enumerate. Compression rule §6.5b now scans Evidence for deontic verbs mechanically.

### CR-08 — traceability proves linkage, not entailment · **CLOSED, method amended**

`requirement → test` and `test → requirement` were both non-empty while five requirements sat inside test
rows with no requirement stating them. Compression rule §6.6, run in both directions.

### CR-09 — nothing audited the contract against itself · **CLOSED, method amended**

Compression rule §6.7. Its first run produced the nine rows of §4c.

### CR-10 — seven statements left the contract · **CLOSED in v1.2**

The equivalence claim was false as published. See §5b and §6.

### CR-11 — FC family and version gating · **OPEN, deferred by decision 2026-08-26**

`FC-05` gates on a version floor only. Two consequences are unaddressed **by decision, not by oversight**:

1. `SCOPE-01` states an INAV ceiling — *"8.0.1+ within 8.x"* — that no requirement enforces. INAV 9.0 passes
   `FC-05`.
2. **`FC-08`/`FC-09` send an unrecognised FC variant to telemetry-disabled compatibility with camera control
   *active*.** The entire `RCV-*` model — guarded commit, `armingDisableFlags`, `ARMING_DISABLED_RC_LINK` —
   assumes Betaflight or INAV mechanics, and an unknown variant's RC-validity and failsafe semantics have
   never been audited.

Item 2 is the safety-relevant half and does not depend on any hypothetical future release. **This CR stays
open and shall be resolved before implementation reaches `FC-05`.**

### CR-12 — official-DJI-path preference · **CLOSED: removed by decision 2026-08-26**

Source §20.2 required that, should DJI add the Nano to its official protocol, the official path be preferred
over DUML. It was lost to Evidence by the CR-07 defect. On review it is **deliberately not restored** — it is
policy about a hypothetical, and the V1.1 effort is directed at the DUML path. **Recorded here as a removal,
not as a loss**, so the two are never confused.

### CR-13 — warning thresholds: mechanism kept, value delegated to the user · **CLOSED by decision 2026-08-26**

`OSD-12` named `CAM BAT LOW` and `CARD LOW` with no threshold, no source semantics and no arbitration — one of
the nine inherited contradictions of §4c. Two resolutions were available and they are **not** equivalent:

| | Effect on the inherited requirement |
| --- | --- |
| **(a) chosen** — bridge owns the mechanism, user owns the number, warning disabled until configured | `OSD-12` preserved in full |
| (b) rejected — remove threshold-based warnings; expose only camera-native states | **deletes two of `OSD-12`'s six warnings** — a source removal requiring disposition `X` and its own CR |

Recorded because the two look like the same decision and are not: (a) resolves a gap, (b) would have been a
scope reduction. `OSD-20` implements (a).

### CR-14 — Nano/DUML priority is not a release gate · **CLOSED by decision 2026-08-26**

Raised on external review: the v1.2 treatment leaves the Nano at `Expected`, keeps `RES-09` closing for V1
from GoPro alone, and does not make V1 wait on `VAL-SPIKE-04` — which appeared to contradict a stated focus on
Osmo Nano / DUML. **The focus is a development priority, not a scope change**, and `SCOPE-04` now says so.

The alternative — making Nano feasibility a V1 gate — was rejected because `VAL-SPIKE-04` is already
independent, immediately runnable and hardware-ready, so gating buys no schedule; while `SCOPE-04` already
specifies the negative-probe fallback, which gating would convert into a release blocker for the GoPro
product.

---

## 5a. v1.1 normative delta — applied 2026-08-26

**These are requirement changes and are deliberately outside the compression.** The compression's
semantic-equivalence claim covers v1.0 → the four-artifact set only; this section is the delta applied
afterwards, so that claim stays clean.

| CR | Change | New / changed IDs | Behavioural effect |
| --- | --- | --- | --- |
| CR-02 | Candidate-cache contents become a **closed list** of four permitted classes; unbounded scan data prohibited; storage bounds delegated | **`PAIR-10`** new, `PAIR-06` trimmed, **`RES-09`** new | Narrows what may be retained. Previously any field set conformed |
| CR-03 | Gesture window becomes **15.0 s** from the **first qualified control sample**; armed-window behaviour stated | `SETUP-10` rewritten | Removes 5 s of implementation freedom; the start point was already normative |
| CR-04 | Delegated-parameter registry with `OPEN` / `PROVISIONAL` / `CONDITIONAL`, plus the release rule | **`REL-02`** new | No behavioural change; blocks release against an `OPEN` parameter |
| CR-05 | Three control **primitives**; Arm Record demoted to a **preset** over Level | `CTRL-04` rewritten, **`CTRL-29`** new | **Zero intended behavioural effect.** Removes one evaluator from the implementation surface |
| CR-06 | OSD **state-change latency ≤ 100 ms**, bridge contribution only | **`OSD-18`** new | Adds a bound where none existed; a conforming v1.0 implementation could add unbounded latency |

**Requirements: 229 → 234.** IDs `CTRL-29`, `OSD-18`, `PAIR-10`, `RES-09`, `REL-02`. No ID reused, none
renumbered.

**Validation: 185 → 192.** New cases `VAL-FUNC-110`…`VAL-FUNC-116`; `VAL-FUNC-08` and `VAL-SPIKE-01`
item 2 re-pointed from `CTRL-04` to `CTRL-29` and extended to prove Arm Record instantiates no separate
evaluator; `VAL-SPIKE-01` gains an OSD-latency item; `VAL-SPIKE-03` and `VAL-SPIKE-04` gain the `RES-09`
write-back.

### Notes on two of them

**CR-03 — the start point did not need fixing.** The source already said *"the window starts at
qualification, not at power-on"*, and `SETUP-12` ties the baseline to the same instant, so the two cannot
disagree. **The range was the whole of the ambiguity.** What v1.1 adds beyond the number is the exclusion
of a reading nobody intended (the state-machine transition considered apart from the sample that caused
it) and the armed-window statement — because a window that opens, runs and expires unrecognised while
armed looks like a defect to a bench tester and is the interlock working. `VAL-FUNC-111` tests the
duration is **invariant** across two `failsafe_delay` settings whose qualification instants differ by
seconds, which is the test that fails if the window is still measured from power-on.

**CR-05 is a taxonomy change with no runtime consequence, and that is exactly why it needed an ID.**
`CTRL-29` states that Arm Record requires no separate evaluator and is indistinguishable from a
hand-entered Level mapping. Without that stated, "it's just a preset" is a comment; with it, `VAL-FUNC-08`
and `VAL-FUNC-110` can fail an implementation that builds a fourth evaluator.

---

## 5b. v1.2 normative delta — applied 2026-08-26

Arising from two independent external audits of the compressed set. **The first four rows are the repair of
the compression itself**; the rest are inherited defects and v1.1 repairs, deliberately outside any
equivalence claim.

| Class | Change | IDs |
| --- | --- | --- |
| **Restored** — lost by the CR-08 defect | FC-restart detection and backend reinstatement | **`FC-14`** |
| | Malformed camera frame rejected atomically, no state update | **`CAM-15`** |
| | Configuration integrity → safe defaults, fallback surfaced | **`RES-10`** |
| | Learning abandonment ≡ timeout, bridge ends it unaided | **`LEARN-18`** |
| | Bridge shall not attempt to recover the FC | **`SETUP-27`** |
| **Restored** — lost by the CR-07 defect | Supported-model list re-read per release | **`CAM-14`** |
| **Consistency** *(audit 7)* | `SAFE-03` `When` column exhaustive and normative | `SAFE-03` |
| | Single `osdBackendEnabled` prerequisite on every OSD write | **`OSD-19`** |
| | Discovery completion criterion; first-hit termination prohibited | **`PAIR-11`**, `PAIR-03`, `PAIR-08` |
| | Push Button rate floor at 10 Hz, else no Push Button control | `MSP-08`, `CTRL-21` |
| | Range-latch initialisation from the nominal stored range | **`CTRL-30`** |
| | Threat boundary restated as observable; frame format defined | `SETUP-17`, **`SETUP-28`** |
| | Each entry authority described as what it proves | `SETUP-02` |
| | Warning sources, user-set thresholds, arbitration order | **`OSD-20`** |
| **New policy** | One-shot vs level delivery; no blind non-idempotent retry | **`CAM-16`** |
| | Three-state compatibility lifecycle; probe never reaches `Verified` | `CAM-08` |
| **v1.1 repairs** | Release gate by applicability, `VAL-REV-*` included | `REL-01` |
| | Candidate-cache bound closes per release, per claimed driver | `RES-09`, `REL-02` |
| | Release rule extended to `PROVISIONAL` and `CONDITIONAL` | `REL-02` |
| | Armed observation inhibits the remainder of the window | `SETUP-10` |
| **Entailment** *(audit 6 reverse leg)* | `FC-10` gains the exposure obligation its test already checked | `FC-10` |

**Requirements: 234 → 246.** New IDs `FC-14`, `CTRL-30`, `CAM-14`, `CAM-15`, `CAM-16`, `OSD-19`, `OSD-20`,
`SETUP-27`, `SETUP-28`, `LEARN-18`, `PAIR-11`, `RES-10`. No ID reused, none renumbered.

**Validation: 192 → 201.** New `VAL-FUNC-117`…`VAL-FUNC-123`, `VAL-FAIL-59`, `VAL-REV-13`. Six cases
re-pointed onto the requirements restored from them; `VAL-FAIL-46` rewritten around a property the bridge can
actually observe; `VAL-SPIKE-04`'s promotion to `Verified` removed; `VAL-SPIKE-03`/`04` write-backs scoped per
release.

### One note on `VAL-FAIL-46`

It required the bridge to reject a maintenance frame *"outside a passthrough session"* — a discrimination the
bridge cannot make, since identical UART bytes are identical UART bytes and it cannot observe the FC's
passthrough state. **A test that cannot fail for the right reason is not coverage.** It now tests what
`SETUP-28`'s framing makes real: ordinary FC MSP responses, including payloads shaped like maintenance frames,
are never parsed as commands, with each of the five framing fields rejected independently.

---

## 5c. Freeze

**v1.2 is the frozen audited baseline as of 2026-08-26.** 246 requirements · 201 validation cases · 37
platform facts · seven audits clean.

**Subsequent findings land as a new delta section, with their own change requests.** This audit history —
§1's dispositions, §2's reverse audit, §4a–§4c's audit results, §5's change-request log and the §5a/§5b delta
tables — **is not edited in place.** A ledger rewritten to match the current state stops being evidence of how
the current state was reached, which is the one thing it exists to be.

---

## 6. Acceptance

> *Compression succeeds only when the shorter specification admits exactly the same conforming
> implementations as the audited source specification.*

**Not met at the compression boundary as originally published. Met after the v1.2 delta of §5b.**

**The original claim was false, and is retracted rather than qualified.** Seven statements that constrain a
conforming implementation were absent from the contract: six restored by §5b, one (`CR-12`) deliberately
removed on the record. Under this criterion, seven is not a margin — it is a failure, and the audit that
should have caught it did not exist until CR-08.

What the failure was **not**: no control, failsafe, qualification, boot-mode, pairing or safety state machine
was damaged. The losses were peripheral obligations — FC-restart recovery, malformed-frame handling,
configuration integrity, learning abandonment, FC non-recovery, model-list revalidation — each carried by a
§24 acceptance row or a §20 prose `shall` that a wholesale disposition swept up. That is a narrow and
diagnosable failure mode, which is why the repair is twelve requirements rather than a re-derivation.

The four claims below held at the boundary and hold now.

- No shortened statement admits behaviour previously prohibited — §2, items 1, 4, 5 and 8 were the
  candidates, and each is preserved or recovered.
- No statement prohibits behaviour previously permitted — the permitted optimisations (`RCV-04`),
  permissions (`CTRL-25`, `CAM-13`, `CTRL-27`) and optional scope (`SCOPE-05`, `DIAG-*`) are retained as
  `may` with their bounds intact.
- **No platform distinction was generalised away.** `RCV-*` keeps the Betaflight and INAV mechanisms
  separate under one common semantic (`RCV-01`), which is the §6 example of the rule applied.
- **No safety invariant was weakened.** `SAFE-01`…`SAFE-07`, `RF-01`…`RF-09`, `FC-11`, `BOOT-13` and
  `SETUP-02` are stated at v1.0 strength or more precisely.
- **No formerly testable requirement became subjective.** The vague-term scan is clean. CR-02 and CR-03
  recorded the two spots that were already subjective in v1.0; **both are closed by the v1.1 delta of §5a**,
  and `VAL-FAIL-46` — the one case that was untestable as written — is closed by §5b.

**No information was discarded to shorten the PRS.** Material necessary to justify the contract moved to
Evidence; material necessary to verify it moved to Validation; only genuine duplication was deleted, and
every deletion is itemised in §1. **One statement was removed by product decision** and carries disposition
`X` against its source line, so the source inventory stays complete.

### The relationship, stated precisely

**This is not an equivalence claim and shall not be restated as one.**

```
source monolith  →  compression boundary
     NOT EQUIVALENT.  Seven statements constraining a conforming
     implementation were absent from the contract.        (§4b, §5b)

compression boundary  →  v1.2
     six losses restored                                  (§5b)
     one removed by product decision                      (CR-12, §1 disp. X)
     nine inherited contradictions resolved               (§4c)
     three v1.1-delta defects and two entailment defects repaired

source monolith  →  v1.2
     DELIBERATELY DIFFERENT.  v1.2 admits a narrower set of conforming
     implementations than v1.0 did.  That is the intended outcome, not
     a residual error.
```

Any claim that v1.2 is *better* than the source monolith is an engineering judgement about implicit semantics
removed. **It is not a formal property and carries none of the auditable weight the original equivalence
criterion did.** The three statements above are the auditable ones.
