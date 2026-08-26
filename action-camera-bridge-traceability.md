# Action Camera Bridge — Traceability Ledger

**Purpose.** This document is the audit record of the compression and of every delta applied since.
It records the disposition of every statement in the frozen source, the results of the seven audits the
compression rule requires, and — separately — everything the extraction surfaced that would be a
**requirement change** and was therefore *not* applied.

**It is not evidence that the compression was lossless, and shall not be cited as such.** §6 records the
opposite: the compression lost seven statements. What this document is evidence *of* is that the loss was
found, itemised and repaired on the record — which is a weaker and truer claim.

**Semantic baseline.** `action-camera-bridge-prs-source-v1.0.md`
sha256 `e6178686acfa71e932784cf44041a988f9d72e8ff64162c89588bce8a700b473`
3,326 lines / 31,253 words / 76 sections.

**Compression result.** 229 requirements, 37 platform facts, 185 validation cases — **and seven statements
that should have been requirements and were not** (§4b, §5b, §6). The v1.1 delta of §5a took the set to 234
requirements and 192 cases; the v1.2 delta of §5b repaired the compression and took it to 246 requirements
and 201 cases; the v1.3 delta of §5c took it to **247 requirements and 202 validation cases.** **All three
deltas sit deliberately *outside* the equivalence claim**, which failed at the boundary and cannot be
satisfied retroactively (§6).

| Document | Words | Role |
| --- | --- | --- |
| PRS | 15,884 | the normative reading path |
| Platform Evidence | 7,129 | why each requirement is correct at the pinned tags |
| Validation | 11,615 | how conformity is demonstrated |
| Ledger (this file) | 10,877 | audit record |
| Compression Rule | 3,202 | the method, amended by CR-01, audits 5b/6/7, and CR-23/24 |

*(Word counts are **regenerated 2026-08-27** and are post-v1.3. At the compression boundary the PRS was
12,207 and Validation 9,377; after v1.1 they were 12,910 and 9,963; after v1.2, 15,413 and 11,022. The v1.2
freeze published four of these figures stale — this table is a measured block and is regenerated at every
delta, §5d.)*

**On the word count.** The normative reading path fell from 31,253 words to 12,207 at the compression
boundary — a **61 % reduction in what an implementer must read to know what to build**. It has since grown
back to roughly half the monolith as three deltas made implicit semantics explicit. The *total* across four
documents is now larger than the monolith, because IDs, the Verifies column and this ledger are new text.

**Neither number is the acceptance criterion**, which is the set of admitted implementations. The word count
is reported because it is the thing most likely to be quoted as if it were a result, and it is not one.

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
| 9 | armed behaviour, persistence, "not the safer platform" | **R+E** *(corrected in v1.3, CR-21 — was `E`)* | `OSD-07` retains *"writes are accepted while armed"*; persistence and the platform argument → `PF-BF-03`, `PF-BF-17` |
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
| 10 Hz floor; < 10 Hz prohibited; 200 ms two-sample span at 10 Hz | `MSP-08` | threshold |
| 100 ms OSD state-change latency | `OSD-18` | timing |
| 1000 / 1500 / 2000 µs — explicitly **not** assumable | `LEARN-08` | negative bound |
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
| **15.0 s** gesture window, from the first qualified sample | `SETUP-10` | timing |
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
| discovery dwell | `PAIR-12` | **delegated →** `VAL-SPIKE-07`; interim = `PAIR-03` bound |
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

**Delegated values: 7 rows in `REL-02`** — `RF-04`, `RF-08`, `RES-09`, `RES-03` (two rows), `BOOT-05`
and `PAIR-12` — each naming both its closing measurement and its write-back destination. The previous figure
of **4** predated `RES-09`, `PAIR-12` and the second `RES-03` row (CR-23).

**Regenerated 2026-08-27 from the current PRS** by extracting every numeric literal carrying a unit and
attributing it to the requirement block containing it, then diffing against this table. That pass is what
surfaced `MSP-08`, `OSD-18` and `LEARN-08` as unrepresented. **This block is a measurement of the artifact
and is regenerated at every delta** (§5d).

---

## 4. Traceability audit

Mechanically verified over the whole set. **Figures below are post-delta**; the parenthesised value is the
figure at the compression boundary, before §5a.

**Regenerated 2026-08-27 against the v1.3 artifacts.**

```
requirements defined          247     (229 at the boundary · 234 after v1.1 · 246 after v1.2)
duplicate definitions           0
gaps in ID numbering            0
dangling requirement refs       0     (all five artifacts)
deontic statements in Evidence 12     (audit 5b: 0 new obligations)
platform facts defined         37
dangling platform-fact refs     0
unreferenced platform facts     0
validation cases              202     (123 FUNC · 59 FAIL · 13 REV · 7 SPIKE)
validation cases with no requirement    0
requirements with no validation case    0
undelegated vague terms in the PRS      0
```

The Evidence figure previously read **14**, annotated *"2 repaired in v1.2"*. A mechanical scan of the
current artifact returns **12**, and the two figures cannot be reconciled by that annotation alone because
v1.2 also *added* a deontic line — §0's convention statement. **The headline number was reporting a
different artifact than the one on disk** (CR-23). Corrected to a count of the current file.

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

**Re-enumerated from zero on 2026-08-27 (CR-23).** The previous `108/108` was carried forward across the
v1.1 and v1.2 deltas without re-running, although both added inputs — `OSD-18`'s 100 ms, `MSP-08`'s 10 Hz
floor, `CTRL-04`'s primitive cardinality, `PAIR-06`'s cache content classes and `SETUP-28`'s frame structure
among them. **Incrementing a carried-forward total would have been wrong in both directions**, since some
v1.0 inputs were replaced rather than supplemented, so the count below is a fresh enumeration.

```
conformance inputs enumerated  114
present in the PRS             113
evidence-only (defects)          0
absent from both                 1   ← Betaflight ARM permanent box ID (CR-24)
```

The nine `MSP_SET_*` / `MSP2_INAV_SET_*` identifiers that appear only in Evidence were checked and are **not
defects**: each is a command the bridge is prohibited from sending, and `SAFE-03` is a **closed allowlist**,
so an implementation needs the list of what is permitted and never the list of what is not. `ARM_DELAY_MS`
and `MSP_RESULT_ERROR` are likewise rationale — the first for `VAL-SPIKE-06`'s arming audit, the second for
why `OSD-08` fixes 28 bytes — and neither is consumed by the bridge.

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
additional semantic fact from Evidence?* — is yes for 113 of 114 inputs.** Evidence carries citations,
derivations and rationale for those and originates none of them.

**The exception is not an Evidence-only input; it is absent from both documents.** `FC-12` requires an
`MSP_BOXIDS` lookup *"to locate the ARM box bit"* on Betaflight, and locating it means matching ARM's
**permanent box ID** — a Betaflight constant that appears in neither the PRS nor Evidence. An implementer
must currently retrieve it from Betaflight source unaided. **This is the first `absent from both` result any
audit here has produced**, and it was produced by re-enumerating rather than by carrying a total forward,
which is the argument for CR-23's rule. Left `OPEN` as CR-24 rather than filled in, because the value has
not been read at the pinned tag and a guessed platform constant is worse than a recorded gap.

---

## 4b. Validation-entailment audit *(audit 6, added after external review)*

Run over all 201 validation cases in both directions.

**Regenerated 2026-08-27 (CR-23).** The figures below are the v1.2 run plus the v1.3 re-run; the v1.2 run
covered the reverse leg by reading cases rather than by any mechanical screen, which is how three further
hits survived it.

```
v1.2 run   cases audited                       201
           hidden requirements (forward leg)     6   all restored, see §5b
           decorative citations (reverse leg)    2   VAL-REV-06/FC-02, VAL-FUNC-104/FC-10

v1.3 re-run
           (case, requirement) pairs screened  413   over all 202 cases
           reported by external review           2   one upheld (CR-18), one rejected (CR-19)
           found by the v1.3 screen              3   VAL-FUNC-05/FC-09, VAL-REV-01/CTRL-03,
                                                     VAL-REV-11/FAIL-02
```

**On the screen's usefulness — it is weak, and saying so is part of the result.** It scores lexical overlap
between a case's prose and each cited requirement's prose, and flagged **116 of 413 pairs**. A 28 % flag rate
is not a defect rate; `VAL-FUNC-01`/`FC-03` scores zero overlap and is perfectly entailed. **The screen
cannot establish entailment and is not evidence of it.** What it does is order the manual reading, and the
16 zero-overlap pairs were read individually — which is where all three hits came from. The remaining 100
flagged pairs have **not** been read one by one, and this audit does not claim they have.

Of the three: `VAL-FUNC-05` tested capability-probe degrade while `FC-09` governs unsupported *variants* — a
different trigger reaching the same consequence, now entailed by citing `OSD-19`, which is what unifies them.
`VAL-REV-01`'s headerless CI build proves the transport seam but not that one evaluator sits behind it, and
it is `CTRL-03`'s **only** case, so the review was extended rather than the citation dropped. `VAL-REV-11` is
a coverage roll-up and cannot test `FAIL-02`'s behaviour at all; `FAIL-02` moved to `VAL-FAIL-14`, which
forces the bridge non-functional for real, and that case now runs three ways.

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

### CR-24 — a conformance input is absent from both documents · **OPEN**

`FC-12` requires a Betaflight `MSP_BOXIDS` lookup *"to locate the ARM box bit"*. The lookup returns permanent
box IDs in the order their bits appear in `MSP_STATUS`'s flight-mode flags, so locating ARM means knowing
**ARM's permanent box ID** — and that constant is in neither the PRS nor Evidence.

**Not filled in here.** The value has not been read at Betaflight `2025.12.5`, and a platform constant
asserted without reading its source is exactly the class of claim `SCOPE-02` and the Evidence citation
discipline exist to prevent. **It shall be read at the pinned tag, recorded as a platform fact and cited
from `FC-12`, before implementation reaches the maintenance-gesture interlock (`SETUP-09`).**

Scope is narrow: the input is needed only on Betaflight and only where a Maintenance Entry AUX is
configured, so it blocks `SETUP-05`, not the control path. It is the first `absent from both` result these
audits have produced.

### CR-25 — three further decorative citations · **CLOSED in v1.3**

Found by the regenerated audit 6 reverse leg, not by external review. Dispositioned individually in §4b:
`VAL-FUNC-05`/`FC-09` repaired at the citation (`OSD-19` added), `VAL-REV-01`/`CTRL-03` repaired by extending
the review because it is that requirement's only case, and `VAL-REV-11`/`FAIL-02` repaired by moving the
requirement to a case that can fail it.

**The pattern across all five reverse-leg hits found so far is one thing:** a case cites the requirement its
*subject matter* belongs to rather than the requirement its *pass criterion* would falsify. Traceability
scores that as coverage. Nothing else does.

### CR-15 — armed state gated the OSD on one backend and not the other · **CLOSED in v1.3**

Source §9 and §10.1 both recorded that their OSD handler is unguarded while armed, and **both were
dispositioned `E`**. `OSD-07` retained the statement anyway; `OSD-08` did not. `VAL-FUNC-09` then cited both
and was Req/Req, so the case was **unentailed on INAV** — the CR-08 pattern exactly: *test requires X, cited
requirement does not say X, Evidence contains X* (`PF-INAV-10`).

**The repair is not the symmetric sentence.** *"Writes are accepted while armed"* is a property of the FC,
not an obligation the PRS can impose on it — `OSD-07` was stating a platform fact inside a requirement, so
patching `OSD-08` to match would have made both backends state a fact and neither state a duty. `OSD-19` now
carries the bridge-side obligation once, for both backends: **armed state is not a term in
`osdBackendEnabled`, and no OSD write may be suppressed because the FC reports armed.** `OSD-08` separately
regains the platform fact with its `PF-INAV-10` citation, so the two backends read symmetrically.

This is `FC-11` under-reaching: it enumerated camera control, BLE reconnect policy and setup as the surfaces
where armed state is not an input, and **OSD writes are a fourth surface it never named.**

### CR-16 — Validation restated a release gate that `REL-01` calls defective · **CLOSED in v1.3**

The Validation header declared *"this document contains no requirements"* and then stated one: a release gate
naming `VAL-FUNC-*` and `VAL-FAIL-*` only. `REL-01` names that exact formulation as the defect it exists to
fix — it would let all 22 `VAL-REV-*`-only requirements fail while the release passed. The restatement was
correct when written and was **not updated when CR-05's successor amended `REL-01` in v1.1**.

**A restatement is a second home for a normative rule, and second homes go stale silently.** The header now
points at `REL-01` and states nothing itself.

### CR-17 — `PAIR-11`'s completion criterion referenced a parameter that did not exist · **CLOSED in v1.3**

`PAIR-11` required a scan to run to *"a stated minimum scan duration, or the `PAIR-03` bound, whichever comes
first"*. **No minimum scan duration is stated anywhere in the specification** — one occurrence, inside
`PAIR-11` itself. And *"whichever comes first"* is vacuous: any minimum below the 30 s bound is always first,
so the entire criterion rested on the undefined value. An implementation could set it to 100 ms and satisfy
the text while restoring the first-hit behaviour `PAIR-11` was written to prohibit.

`PAIR-12` now names the dwell and delegates its value to `VAL-SPIKE-07` via `REL-02`. **No number was
invented.** The interim value is the `PAIR-03` bound — the longest the specification permits, and the only
choice that cannot conclude too early — because the failure is asymmetric: too short auto-binds a stranger's
camera (`PAIR-09`), too long makes a bench user wait.

Fixing the 30 s bound as the criterion outright was considered and rejected: it would make reaching the bound
*be* completion, leaving `SCAN_TIMEOUT` (*"scan reached its bound without completing"*) unreachable and
`VAL-FUNC-42` untestable.

### CR-18 — `VAL-FAIL-56` cited a requirement it cannot exercise · **CLOSED in v1.3**

The case cited `RCV-01` (*"RX loss shall not create a camera-control transition"*) while testing RC-validity
loss during learning. **`LEARN-05` suspends camera actuation for the whole of a learning operation**, so
there is no camera-control transition available to create and `RCV-01` cannot fail there. Replaced with
`LEARN-17`, which is what the case actually checks: acquisition timeout returns to idle and changes no
configuration.

### CR-19 — `VAL-FUNC-30`'s `CTRL-26` citation was reported decorative and is not · **CLOSED: finding rejected**

External review recommended removing `CTRL-26` from `VAL-FUNC-30` or adding a Betaflight test, arguing the
case's Req marking on INAV proved the citation decorative because `CTRL-26` is `[BF]`-only.

**The argument inverts.** `CTRL-26`'s normative content is a prohibition — *"no advisory behaviour shall
depend on it"* — and `MSP_MODE_RANGES_EXTRA` does not exist in INAV 8.0.1. **The INAV run is therefore the
only test of that prohibition**: an advisory that depended on the command fails there and nowhere else.
Removing the citation would have deleted the sole coverage of `CTRL-26`.

Recorded rather than dismissed, because the case *text* did not say this and a future reviewer would reach
the same wrong conclusion. The text now states what the INAV column proves. **The finding was wrong; the
observation that produced it was not.**

### CR-20 — `VAL-FUNC-11` entailed nothing from the requirement it cited · **CLOSED in v1.3**

The case tests that `MSP_RC` returns live AUX values while armed; `CTRL-12` says only that the bridge reads
FC-effective values through `MSP_RC` on both platforms, and says nothing about armed state.

Unlike CR-15, **no requirement was missing.** The tested statement is a platform property, and the
requirement that depends on it is `FC-13` — armed-invariance is unsatisfiable if the platform stops
reporting live values while armed. Repaired at the citation. **An entailment failure does not imply a lost
requirement, and repairing every one with a new requirement would inflate the contract with platform facts.**

### CR-21 — a §1 disposition understated what the PRS kept · **CLOSED in v1.3**

§1 row 9 dispositioned source §9's armed-behaviour prose as `E`, but `OSD-07` retained the statement. The
row is now `R+E` with its prior value shown. Found while diagnosing CR-15, and it is the reason CR-15 read
as a §24-wholesale residue at first: **the ledger said both platforms' armed prose went to Evidence, and one
of them had not.**

### CR-22 — the ledger asserted an equivalence it elsewhere retracted · **CLOSED in v1.3**

Two statements, both wrong and mutually reinforcing. The Purpose block opened *"This document is the evidence
that the compression was lossless"* while §6 recorded the compression as **NOT EQUIVALENT**; and §6 itself
said *"Met after the v1.2 delta of §5b"*, which cannot be true of a criterion scoped to the compression
boundary — and doubly cannot be true of a baseline that `X`-removed a source statement by decision (CR-12).

Both corrected. The relationship block added in v1.2 was already right; **these two sentences contradicted
it from above and below.**

### CR-23 — audits 3 and 5 reported pre-delta figures · **CLOSED in v1.3, and the method amended**

§3's boundary table still listed *"10–15 s gesture window"* after CR-03 fixed `SETUP-10` to **15.0 s**, had
no entry for `OSD-18`'s 100 ms added by CR-06, and reported *"Delegated values: 4"* against `REL-02`'s six
rows at the time — seven once `PAIR-12` joined them in this same delta. §4a still reported `108/108` although v1.1 and v1.2 added conformance inputs. §4b's Evidence
deontic count read 14 where the current artifact has 12.

**These blocks state the measured condition of the artifact, not the history of how it got there**, so
carrying them forward across a delta makes them false. §5c now separates the two kinds of block, and each
regenerated block is dated.

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

## 5c. v1.3 audit delta — applied 2026-08-27

Raised by an external re-run of all seven audits against the v1.2 artifacts rather than against this ledger's
reported results. **It found that the v1.2 freeze was premature**: two specification defects survived, and
four audit blocks reported figures from before the delta they followed.

| # | Change | Where | Kind |
| --- | --- | --- | --- |
| 1 | Ledger Purpose no longer claims the compression was lossless | §Purpose | contradiction (CR-22) |
| 2 | §6 acceptance reads **NOT MET**; *"met after the v1.2 delta"* removed | §6 | contradiction (CR-22) |
| 3 | Validation header defers to `REL-01` and states no gate of its own | Validation §0 | stale restatement (CR-16) |
| 4 | `OSD-19` — armed state is not a term in `osdBackendEnabled`, on either backend | `OSD-19` | **specification defect** (CR-15) |
| 5 | `OSD-08` regains the armed platform fact `OSD-07` already carried | `OSD-08` | symmetry (CR-15) |
| 6 | `PAIR-11` completes on the `PAIR-12` dwell; *"whichever comes first"* removed | `PAIR-11` | **specification defect** (CR-17) |
| 7 | `PAIR-12` — discovery dwell, delegated, interim value = `PAIR-03` bound | new | new requirement (CR-17) |
| 8 | `REL-02` gains the discovery-dwell row | `REL-02` | registry (CR-17) |
| 9 | `VAL-SPIKE-07` — closes `PAIR-12` by measuring the camera, not the bridge | new | new case (CR-17) |
| 10 | `VAL-FUNC-09` cites `OSD-19` and tests that the bridge *issues* the armed write | `VAL-FUNC-09` | entailment (CR-15) |
| 11 | `VAL-FUNC-11` cites `FC-13` | `VAL-FUNC-11` | entailment (CR-20) |
| 12 | `VAL-FUNC-30` states what its INAV column proves about `CTRL-26` | `VAL-FUNC-30` | clarity (CR-19) |
| 13 | `VAL-FUNC-121` runs the full `PAIR-12` dwell, at whatever value it holds | `VAL-FUNC-121` | entailment (CR-17) |
| 14 | `VAL-FAIL-56` cites `LEARN-17`, not `RCV-01` | `VAL-FAIL-56` | decorative citation (CR-18) |
| 15 | §1 row 9 disposition `E` → `R+E` | §1 | ledger accuracy (CR-21) |
| 16 | §3 boundary table regenerated | §3 | stale audit (CR-23) |
| 17 | §4a conformance inventory re-enumerated from zero | §4a | stale audit (CR-23) |
| 18 | §4b figures regenerated; two further reverse-leg hits dispositioned | §4b | stale audit (CR-23) |
| 19 | `VAL-FUNC-05` cites `OSD-19` — probe-failure degrade is entailed, not assumed | `VAL-FUNC-05` | decorative citation (CR-25) |
| 20 | `VAL-REV-01` extended to confirm one shared evaluator, not just the seam | `VAL-REV-01` | decorative citation (CR-25) |
| 21 | `FAIL-02` moved from a coverage roll-up to `VAL-FAIL-14`, which can fail it | `VAL-REV-11`, `VAL-FAIL-14` | decorative citation (CR-25) |
| 22 | CR-24 opened — Betaflight ARM permanent box ID absent from both documents | `FC-12` | **open finding** |
| 23 | Word counts, traceability figures and PRS status header refreshed | §Purpose, §4, PRS header | bookkeeping |

**One reported finding was rejected** — `VAL-FUNC-30`/`CTRL-26`, recorded as CR-19 with the argument, because
the reasoning that produced it will recur. **Two were reclassified**: the INAV OSD loss was reported as
residue of the §24 wholesale disposition and is not — both platforms' armed prose came from body prose
dispositioned `E` (CR-15, CR-21) — and `VAL-FUNC-11` was reported as a missing requirement and is a missing
citation (CR-20).

### 5d. Freeze rule

**Two kinds of block live in this ledger and they are not governed by the same rule.**

**Historical — never edited in place.** §1's dispositions, §2's reverse audit, §5's change-request log and
every delta table. These record *how the current state was reached*. A correction to one is annotated with
its prior value and names the CR that made it (§1 row 9 is the worked example); it is never silently
overwritten.

**Measured — regenerated at every delta, and dated.** §3's boundary table, §4's traceability figures, §4a's
conformance inventory and §4b's entailment counts. These state *the condition of the artifact now*. Carrying
one forward unchanged across a delta does not preserve history; it publishes a false measurement, which is
what CR-23 was.

**A delta is not complete until every measured block has been regenerated from the current artifacts.** The
v1.2 delta was declared complete without that step, which is why it froze on four stale numbers.

---

## 6. Acceptance

> *Compression succeeds only when the shorter specification admits exactly the same conforming
> implementations as the audited source specification.*

**NOT MET.**

This criterion is scoped to the compression boundary alone (compression rule §7), and at that boundary it
failed. **A later delta cannot retroactively satisfy it** — v1.2 repaired six of the seven losses and
`X`-removed the seventh by product decision, so v1.2 does not admit the source's set of conforming
implementations either, and is not intended to. **v1.2 and v1.3 are new baselines, not equivalent
compressions.** Corrected in v1.3 (CR-22): the previous wording said *"met after the v1.2 delta"*, which is
incoherent under this criterion and contradicted the relationship block below.

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
