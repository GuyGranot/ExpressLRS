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
and 201 cases; the v1.3 delta of §5c took it to **247 requirements and 202 validation cases**; the v1.4
delta of §5e amended six requirements and added none; the v1.5 delta of §5f added one validation case and
one platform fact and amended `FC-12`; the v1.6 delta of §5g amended three requirements; the v1.7 delta of
§5h added one platform fact and amended two. **All seven deltas sit deliberately *outside* the equivalence
claim**, which failed at the boundary and cannot be satisfied retroactively (§6).

| Document | Words | Role |
| --- | --- | --- |
| PRS | 17,206 | the normative reading path |
| Platform Evidence | 7,713 | why each requirement is correct at the pinned tags |
| Validation | 12,630 | how conformity is demonstrated |
| Ledger (this file) | 21,299 | audit record |
| Compression Rule | 4,268 | the method, amended by CR-01, audits 5b/6/7, and CR-23/24 |

*(Word counts are **regenerated 2026-08-27** and are post-v1.7. At the compression boundary the PRS was
12,207 and Validation 9,377; after v1.1, 12,910 and 9,963; after v1.2, 15,413 and 11,022; after v1.3, 15,884
and 11,615. The v1.2 freeze published four of these figures stale — this table is a measured block and is
regenerated at every delta, §5d.)*

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
| **[BF]** ARM permanent ID **0**, and its response position = the ARM status-bit index | `FC-12` | constant + correspondence |
| all 18 allowlist command IDs | `SAFE-03` | command ID |
| exactly 2 permitted writes | `SAFE-04` | count |
| ±10 µs hysteresis | `CTRL-15` | threshold |
| 4 × hysteresis = 40 µs minimum range width | `CTRL-16` | threshold |
| 2 × hysteresis boundary separation | `CTRL-16` | threshold |
| 2 consecutive samples (Push Button) | `CTRL-18` | count |
| 250 ms documented minimum hold | `CTRL-19` | timing |
| 400 ms default event separation | `CTRL-20` | timing |
| 10 Hz / 20 Hz poll rates | `CTRL-21`, `MSP-01` | rate |
| 10 Hz floor; < 10 Hz prohibited; two consecutive samples need **200 ms under adverse phase** at 10 Hz, against `CTRL-19`'s 250 ms hold | `MSP-08` | threshold |
| 100 ms OSD state-change latency | `OSD-18` | timing |
| 1000 / 1500 / 2000 µs — explicitly **not** assumable | `LEARN-08` | negative bound |
| channels 1–4 excluded | `CTRL-12` | scope |
| `channelCount = payloadLength / 2`; no count field on the wire | `CTRL-13` | derivation |
| 1 µs — explicitly meaningless | `CTRL-14` | negative bound |
| `grace = max(failsafe_delay × 100 ms, 100 ms) + margin` | `RCV-08` | formula |
| `margin = max(2 × poll period, 100 ms)` | `RCV-08` | formula |
| 100 ms `failsafe_delay` floor | `RCV-09` | threshold |
| one poll period per failed transaction | `RCV-10` | formula |
| zero successful observations ⇒ never qualifies | `RCV-10` | absolute |
| `STATUS₁ → RC → STATUS₂` ordering | `RCV-03` | ordering |
| `ARMED` clear **and** `ARMING_DISABLED_RC_LINK` clear | `RCV-17` | predicate |
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

### Closed enumerations — added 2026-08-27 (CR-30)

A constraint of the form *"there are N of these and no others"* is a boundary: it is falsifiable, an
implementation can violate it by adding an N+1th, and §3's stated contract has always covered it under
**count**. None of the six below was in the table above, because the regeneration extracted numeric literals
carrying units and a closure constraint carries none.

| Closure | Requirement | Kind |
| --- | --- | --- |
| exactly **3** control primitives, "no others" | `CTRL-04` | closed set |
| exactly **2** armed-state exceptions, "and no others" | `FC-11` | closed set |
| exactly **4** permitted cache-content classes | `PAIR-10` | closed set |
| exactly **4** entry authorities, "and no others" | `SETUP-01` | closed set |
| boot-request creators — a closed list per request | `BOOT-08` | closed set |
| exactly **3** terms in `osdBackendEnabled`, exhaustive | `OSD-19` | closed set |

**`OSD-19` is the instructive one: v1.4 found a closure that v1.3 created**, in the same delta that
regenerated this table and reported it clean. A measured block regenerated by a method narrower than its own
stated contract does not measure the artifact; it measures the method (§5d, and the compression rule's
§6.9).

**Vague-term scan.** `sufficient` / `reasonable` / `adequate` / `appropriate` / `acceptable` / `enough`
across the PRS: **0 remaining as an undelegated threshold.** Three occurrences carried over from v1.0
were completeness statements rather than thresholds and were rephrased (`FC-11`, `RCV-18`, `SETUP-11`);
one is logged as CR-04 below rather than changed. `bounded` and `stable` appear only as **defined
terms**, each within two lines of the number that defines it.

**Delegated values: 7 rows in `REL-02`** — `RF-04`, `RF-08`, `RES-09`, `RES-03` (two rows), `BOOT-05`
and `PAIR-12` — each naming both its closing measurement and its write-back destination. The previous figure
of **4** predated `RES-09`, `PAIR-12` and the second `RES-03` row (CR-23).

**Regenerated 2026-08-27 from the current PRS**, in two passes, because one pass was demonstrably not enough.

1. **Literals** — every numeric literal carrying a unit, attributed to the requirement block containing it,
   then diffed against this table. This surfaced `MSP-08`, `OSD-18` and `LEARN-08` as unrepresented.
2. **Closures** — every *"no others"*, *"closed list"*, *"exhaustive"*, *"and nothing else"* and
   *"may contain only"* construction, likewise attributed and diffed. **Added in v1.4 after pass 1 alone was
   reported as a complete regeneration and was not** (CR-30); it surfaced the six above.
3. **Derivations** — every rule that *determines* a quantity without stating one: a formula, a ratio, a
   "derived from" relation. **Added in v1.7** after `CTRL-13` — *"the response length determines how many
   channels are available"* — was found to be in neither of the first two passes (CR-47). It carries no
   literal and closes no set, so both were blind to it, and it had been outside this table since v1.0.

**This block is a measurement of the artifact and is regenerated at every delta** (§5d). **Both passes run,
and the method is stated so that the next regeneration can be checked against §3's contract rather than
against the previous regeneration.**

---

## 4. Traceability audit

Mechanically verified over the whole set. **Figures below are post-delta**; the parenthesised value is the
figure at the compression boundary, before §5a.

**Regenerated 2026-08-27 against the v1.8 artifacts**, by tools sharing one definition set (CR-49).

```
requirements defined          251     (229 at the boundary · 234 after v1.1 · 246 after v1.2 ·
                                       247 after v1.3; v1.4-v1.7 amended, added none;
                                       v1.8 added MSP-09, FC-15, IND-01, LEARN-19)
duplicate definitions           0
gaps in ID numbering            0
dangling requirement refs       0     (all five artifacts)
deontic statements in Evidence 12     (audit 5b: 0 new obligations)
platform facts defined         39     (PF-BF-22 in v1.5 closing CR-24; PF-BF-23 in v1.7 closing CR-44,
                                       its four line numbers corrected in v1.8 — CR-50)
platform-dependent reqs with
  no platform fact                0
dangling platform-fact refs     0
unreferenced platform facts     0
validation cases              207     (125 FUNC · 60 FAIL · 15 REV · 7 SPIKE)
case->requirement relations   489     (417 table-case + 72 spike, one explicit pair each — CR-45)
audit-6 coverage in v1.8              delta-impact: new edges plus every surviving edge whose
                                       requirement or case text changed (CR-38); verdicts
                                       preserved for untouched edges. NOT a full re-read.
validation cases with no requirement    0
requirements with no validation case    0     under the STRICT relation unit, for the first
                                       time since that unit was defined. v1.7's `0` was
                                       measured under the loose reading (CR-49, §6.11)
undelegated vague terms in the PRS      0
```

The Evidence figure previously read **14**, annotated *"2 repaired in v1.2"*. A mechanical scan of the
current artifact returns **12**, and the two figures cannot be reconciled by that annotation alone because
v1.2 also *added* a deontic line — §0's convention statement. **The headline number was reporting a
different artifact than the one on disk** (CR-23). Corrected to a count of the current file.

The requirement↔test relation is **many-to-many by design**: `VAL-FUNC-49` verifies four predicates of
`LEARN-06`, and `RCV-08` is verified by seven cases. The audit asserts non-emptiness in both directions,
not a bijection.

**Platform-dependent requirements** each carry at least one `PF-*` reference at the pinned baseline, with
**no exceptions** for the first time since this audit was written:
`FC-05`…`FC-07`, `CTRL-14`…`CTRL-16`, `CTRL-24`, `CTRL-26`, `CTRL-27`, `RCV-02`…`RCV-19`, `OSD-01`,
`OSD-07`…`OSD-09`, `SAFE-05`, `SAFE-07`, `SETUP-18`, `SETUP-19`, `INST-05`, `CAM-10`…`CAM-12`, **`FC-12`**,
**`CTRL-12`**, **`CTRL-13`**
(added v1.5 — it had been platform-dependent with no platform fact behind it, which is what CR-24 was).

**Unresolved platform assertions: 0.** Every `[BF]`/`[INAV]` claim in Evidence carries a file and line
at the pinned tag.

---

## 4a. Conformance-input audit

*Added to the method after the first application (`action-camera-bridge-compression-rule.md` §6.5).
It catches CR-01-class defects directly rather than indirectly through the deletion test.*

Every literal an implementation consumes was enumerated by category and checked for presence in the
**PRS**, not merely somewhere in the set. A literal reachable only from Evidence is a defect: it means an
implementer must retrieve a semantic fact from a document that is not the contract.

**Re-enumerated on 2026-08-27 with a stated counting unit, which is the thing this block never had**
(v1.4, CR-31). v1.3 re-enumerated the headline to `114` and left the category breakdown beneath it
untouched — the breakdown was byte-identical to v1.2, where the headline read `108`. **It summed to 109 in
both versions, so it had never agreed with either headline**, and nobody had noticed because nothing ever
added the rows up. That is CR-23's own failure, committed inside the block that documents CR-23.

**The counting unit, so that the next regeneration is comparable to this one.** One conformance input is
**one row of a published enumeration** — a row of a PRS table, an item of a PRS list, or a row of §3's
boundary table. Nothing is counted twice: where an identifier and a constraint *on* that identifier both
exist, the identifier is counted where it is defined and the constraint where §3 records it.

**Group A — identifiers, constants and fixed strings.** Every item is published, either in the PRS table
named or in the list given here.

| # | Category | Inputs | Where the items are | Evidence-only |
| --- | --- | --- | --- | --- |
| A1 | MSP command names | 18 | `SAFE-03`, one per row | 0 |
| A2 | Distinct numeric command IDs, both platforms | 20 | `SAFE-03` BF/INAV columns, plus `188` recorded by `OSD-07` as prohibited | 0 |
| A3 | OSD element and subtype constants | 5 | `CUSTOM_ELEMENT_TYPE_NONE`, `CUSTOM_ELEMENT_TYPE_TEXT`, `CUSTOM_ELEMENT_VISIBILITY_ALWAYS`, `MAX_CUSTOM_ELEMENTS`, `MSP2TEXT_CUSTOM_MSG_0` | 0 |
| A4 | FC flag and bit names | 4 | `ARMED`, `BOXFAILSAFE`, `ARMING_DISABLED_RC_LINK`, `ARMING_DISABLED_RX_FAILSAFE` | 0 |
| A5 | ESP reset-reason constants | 2 | `ESP_RST_POWERON`, `ESP_RST_SW` | 0 |
| A6 | Boot modes | 4 | `BOOT-02`, one per row | 0 |
| A7 | Maintenance protocol verbs | 4 | `SETUP-15`, and it states no others exist | 0 |
| A8 | `PAIR` outcome values | 6 | `PAIR-05`, one per row | 0 |
| A9 | FC degraded-state values | 2 | `FC-08`, one per row | 0 |
| A10 | OSD warning strings | 6 | `OSD-20`, one per row | 0 |
| A11 | OSD non-warning state strings | 5 | `CAM STALE` (`CAM-06`), `CONNECTING` (`OSD-09`), `SETUP` (`BOOT-14`), `SETUP GESTURE` and `SETUP n/4` (`SETUP-14`) | 0 |
| | **Group A** | **76** | | **0** |

**A membership defect in v1.4's own version of this block, found by v1.5.** It reported `Group A 76 +
Group B 62 = 138` with one input `absent from both` — but the absent input, ARM's permanent ID, appeared in
**neither group's published enumeration**. The arithmetic summed and the membership did not, so the block
passed the check CR-31 introduced while still not being fully checkable. **A total that sums is necessary and
not sufficient; every counted item must appear in a published enumeration.** Now that `FC-12` states the
constant it is a §3 row and belongs to Group B (CR-37).

**Group B — specified values, rules, closures and derivations: §3's boundary table, one input per row.**
§3 publishes **67** rows. Three are not Group B inputs: two are already counted in Group A (`SAFE-05`'s `MSP_RTC` pair in
A2, `SAFE-03`'s allowlist in A1) and one is a validation pass criterion rather than an implementation input
(`VAL-SPIKE-02`'s formula). **Group B = 64**; v1.5 added `FC-12`'s ARM permanent ID and its status-bit
correspondence, and v1.7 added `CTRL-13`'s channel-count derivation.

```
Group A                    76
Group B                    64
                          ───
conformance inputs        140      ← the two groups sum to it, and every item is in one of them
present in the PRS        140
evidence-only (defects)     0
absent from both            0      ← CR-24 closed in v1.5; CR-44 closed in v1.7
```

**The total moved from 114 to 139 because the unit changed, not because inputs were added.** The earlier
figures counted by no stated rule, which is why they could not be reproduced, could not be reconciled with
their own breakdown, and cannot be meaningfully compared to this one. **A number that cannot be recounted was
never a measurement**; treat `108` and `114` as withdrawn rather than superseded.

The nine `MSP_SET_*` / `MSP2_INAV_SET_*` identifiers that appear only in Evidence were checked and are **not
defects**: each is a command the bridge is prohibited from sending, and `SAFE-03` is a **closed allowlist**,
so an implementation needs the list of what is permitted and never the list of what is not. `ARM_DELAY_MS`
and `MSP_RESULT_ERROR` are likewise rationale — the first for `VAL-SPIKE-06`'s arming audit, the second for
why `OSD-08` fixes 28 bytes — and neither is consumed by the bridge.

**Answer to the audit's question — *can an implementation be written correctly without retrieving any
additional semantic fact from Evidence?* — is yes for all 140 inputs.** Evidence carries citations, derivations and rationale, and originates nothing.

---

## 4b. Validation-entailment audit *(audit 6, added after external review)*

Run over all **203** validation cases in both directions.

**What one relation is** *(defined v1.6, CR-45 — it had never been defined)*. **One relation is one explicit
`(validation-case ID, requirement ID)` pair.** It is enumerated from two places and nowhere else: the
`Verifies` cell of a table case, and the `**Verifies:**` line of a spike. **No shorthand is permitted in
either** — no `X-01`…`X-05` range, no `X-*` wildcard — and the counting script **refuses to produce a number**
if it finds one, rather than counting the shorthand as the two IDs it mentions.

```
current, regenerated 2026-08-27 from the v1.10 artifacts

  table-case relations        429      204 cases
  spike relations              72        7 cases
                             ────
  total                       501      211 cases; every case carries at least one

  spike Verifies lines are derived from pass criteria, not authored (CR-49).
  12 requirement mentions remain in spike bodies with no edge, each adjudicated
  as cross-reference, consequence, or negative scoping — never left unruled.
```

**Every earlier figure in this section is a dated historical snapshot and not a current measurement.** They
were produced by a token count that read `RES-01`…`RES-05` as two relations rather than five and omitted the
spikes entirely, so they are not comparable to the total above and are not made comparable by arithmetic.

**Regenerated 2026-08-27 (CR-23).** The figures below are the v1.2 run plus the v1.3 re-run; the v1.2 run
covered the reverse leg by reading cases rather than by any mechanical screen, which is how three further
hits survived it.

```
v1.2 run   cases audited                       201
           hidden requirements (forward leg)     6   all restored, see §5b
           decorative citations (reverse leg)    2   VAL-REV-06/FC-02, VAL-FUNC-104/FC-10

v1.3 re-run
           (case, requirement) pairs screened  413   claimed as covering all 202 cases;
                                                     it covered the 195 table rows — corrected below
           reported by external review           2   one upheld (CR-18), one rejected (CR-19)
           found by the v1.3 screen              3   VAL-FUNC-05/FC-09, VAL-REV-01/CTRL-03,
                                                     VAL-REV-11/FAIL-02
           of which repaired incompletely        2   both re-opened in v1.4 (CR-32)
```

**v1.4 read every *flagged* pair. v1.5 read every *token-counted relation*. v1.6 is the first run over a
defined edge set** (CR-38, CR-45). The method requires each cited relation to be semantically checked and
separately states that a lexical screen *"does not perform the audit"* — from which it follows that the
screen may order the reading but cannot choose what gets read. v1.4 read the 110 the screen selected and
reported the audit complete.

```
historical snapshots — not current measurements

v1.4   relations as then counted                414
       flagged by the screen                    110   all read
       reported complete                        yes   on 27 % of them

v1.5   relations as then counted                403   token count; ranges unexpanded,
                                                      spikes not covered at all
       read semantically                        403
       citations dropped as decorative            3
       cases extended instead of dropped          3

current — v1.6, against the canonical edge set

       table-case relations                     406   +3: RES-02..04, hidden inside a range
       spike relations                           58   new: spikes now carry Verifies lists
       total                                    464
       read semantically in v1.6                464   the 406 re-derived, the 58 read for the first time
       defects found                              0
```

**Five defects survived v1.4, and four of them were in relations the screen never flagged** — high lexical
overlap and no entailment, which is the failure mode a word-overlap heuristic is structurally blind to.
`VAL-FUNC-13`/`CTRL-20` shares *press*, *ms* and *detected* with its requirement and does not test it.

**The fifth is the one that matters more.** `VAL-REV-07`/`PROD-03` **was** flagged, **was** read in v1.4, and
was recorded as entailed. The case says the bridge *"works independently of the video system"*; `PROD-03`
says a **failure** must not affect flight or degrade the control link. The reading matched a phrase and not a
claim. **So the reading has a false-negative rate too — one known instance in 110 — and completing this audit
meant re-reading the 110, not only the 293.** A count of relations read is a stronger claim than a count of
relations flagged, and still not a proof.

**The spikes were outside every count until v1.6, and outside the audit with them.** They are prose
sections rather than table rows, so nothing enumerated their citations; v1.5's claim that they were "read
directly" was true of their text and produced no edges anyone could check. Each now carries an explicit
`**Verifies:**` line — for three of them the union of their own per-item annotations, for the other four
written for the first time — and their 58 edges are inside the total, the reading and the audit.

**All 58 hold.** `VAL-SPIKE-01`'s list is the largest at 38 and is the one most at risk of being a topic
list rather than a coverage claim; every entry traces to one of its 22 numbered pass criteria, including the
two easiest to dismiss — `SCOPE-01` and `SCOPE-02` are falsified by item 22, which forbids substituting a
forward-compatibility run on a newer build for the pinned-baseline run.

**The screen is retired as a selector and kept only as an ordering heuristic.** It scored lexical overlap
between a case's prose and each cited requirement's prose. Measured against v1.5's completed read, it flagged
110 relations containing 1 of the 5 remaining defects, and left 293 containing the other 4 — **worse than
indifferent as a selector, because it also made the unflagged 293 feel accounted for.**
`VAL-FUNC-01`/`FC-03` scores zero overlap and is perfectly entailed; `VAL-REV-04`/`SCOPE-08` was flagged only
because `SCOPE-08` is a bare list of exclusions, one of which is the case's exact subject. **The screen
cannot establish entailment and is not evidence of it** (compression rule §6.9).

**The dropped citations across v1.4 and v1.5 are one pattern:** a case citing the requirement its *subject
matter* belongs to rather than the requirement its pass criterion would falsify. `VAL-REV-11`/`SAFE-06` is
the clearest — a coverage roll-up cannot falsify a behavioural requirement about the FC surviving a dead
bridge. **v1.3 repaired that exact defect on that exact case for `FAIL-02` and left its twin in place**,
which is what a partial read produces: the pairs you happen to open get fixed.

**One of v1.5's three drops was created by v1.5 itself.** CR-29 moved the OSD-suppression condition out of
`MSP-06` and into `OSD-19`; `VAL-FUNC-83` went on citing `MSP-06` for a clause that had left it. A citation
is a claim about the requirement's *current* text, so **moving normative content invalidates every citation
that pointed at where it used to be** — and nothing but a full re-read finds those. The citation now names
`OSD-19`.

**One finding is a genuine coverage gap rather than a citation error, and it is the most serious of the 17.**
`RCV-09` fixes a 100 ms floor for `failsafe_delay = 0`. Both of its cases exercised 5 and 50 — **nothing
tested 0**, so a bridge encoding zero grace would have passed every case while violating a requirement whose
whole content is that it must not. `VAL-FAIL-09` now runs the zero case; `VAL-FUNC-19` no longer cites the
requirement it never tested.

The other extensions cover requirements whose *only* case was the weak one, where dropping the citation
would have deleted the coverage rather than corrected it: `CTRL-01`'s explicit-range storage (`VAL-FUNC-12`
now runs on non-standard endpoints), `RF-01`'s duty-cycle bound (`VAL-FUNC-98` now records the measurement
against the bound, since two unbounded figures can be equal), and `PAIR-01`'s single-mode structure
(`VAL-FUNC-95` now verifies the automatic reconnect enters no `PAIR` boot).

**v1.5 found three more of exactly that kind, all sole-case requirements**, and one of them is the most
easily-missed class in the set — a requirement about what is *stored* rather than what is *observed*:

| Requirement | What its only case did not reach | Extension |
| --- | --- | --- |
| `RCV-04` | the optimisation itself — that a non-transition poll issues **no** `STATUS₂` | `VAL-FUNC-24` now counts transactions in both directions |
| `CTRL-10` | a Combi button on a non-stop action staying **out** of the arbitration | `VAL-FUNC-26` now runs one mapped to photo capture |
| `CTRL-17` | that hysteresis is **not folded into the stored range** | `VAL-FUNC-27` now re-reads the persisted range after driving both thresholds |

`CTRL-17` is the instructive one. Every observable behaviour it governs is already correct in a build that
widens the stored range, because the evaluator applies hysteresis either way. **It fails only later, when the
hysteresis default changes** — and a test that watches behaviour cannot see it. The case now reads the store.

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

**Re-run 2026-08-27 against the current PRS (v1.4, CR-27).** It was **not** re-run in v1.3, although v1.3
changed the PRS — and it is the one audit of the seven that is both re-runnable and cheap, because it reads a
single document. The v1.3 header nonetheless reported seven audits re-run and clean.

**What the skip cost is measurable:** all three contradictions below were introduced or left by v1.2 and v1.3,
and the first was found by an external reviewer rather than by the audit whose entire purpose is finding it.

| # | Contradiction | Introduced | Resolved by |
| --- | --- | --- | --- |
| A | `PAIR-11` completes when the `PAIR-12` dwell elapses; `PAIR-05`'s `SCAN_TIMEOUT` fires when the `PAIR-03` bound is reached without completing. **`PAIR-12`'s interim value is that same bound**, so at 30 s both apply | v1.3 | `PAIR-11` precedence clause; `SCAN_TIMEOUT` recorded as unreachable while the parameter is open, and `VAL-FUNC-42`'s discovery half gated on its closure (CR-27) |
| B | `LEARN-04` requires 20 Hz polling during learning *"regardless of the currently configured control modes"*; `MSP-08` declares a **closed** permitted range with a 10 Hz fallback and states the Push Button rate is what gives way | v1.2 | `LEARN-04` scoped against the fallback, and a target that cannot reach 20 Hz declines Push Button controls rather than mis-learning them at 10 Hz (CR-27) |
| B′ | **B's repair was justified by wrong arithmetic.** v1.4 argued a 250 ms demonstration *"spans two samples at 20 Hz and may span one at 10 Hz"* — both figures wrong, and the second contradicts `MSP-08`, which correctly states two samples need 200 ms at 10 Hz. The policy was right and its stated reason was not | v1.4 | `LEARN-04` rewritten around sample counts: 250 ms buys **5** samples at 20 Hz and **2** at 10 Hz, against `LEARN-06`'s five-sample stability predicate (CR-42) |
| C | OSD-write suppression stated in **three** places at three different scopes — `OSD-19`'s three exhaustive terms, `MSP-06`'s telemetry-disabled clause, `FC-08`'s table column — against `OSD-19`'s own claim to be *"the single place that resolves"* it and §0's no-duplication rule | v1.3 | `MSP-06` and `FC-08` defer to `OSD-19`; the rule now has one home (CR-29) |

**B is the one worth dwelling on.** A and C are visible from a single requirement's wording. B is only visible
by holding two requirements in mind that sit 300 lines apart, share no vocabulary, and were written for
different reasons — and its failure mode is silent: a Push Button control learned at the fallback rate is
stored as a Level control and never fires. **That is what a within-document audit is for**, and it is why
"the document changed but the audit is expensive" is never the trade-off it appears to be.

**B′ is the sharper lesson, and v1.4's clean audit-7 result is what made it possible.** The contradiction was
repaired and the repair's *justification* was arithmetic nobody re-derived — so audit 7 passed on a cluster
whose stated reasoning was false, because the audit compares requirements to each other and a wrong reason
for a right rule contradicts nothing. **A prose justification is not checked by consistency**, and where it
contains a computation, the computation is a measured claim.

### Re-run 2026-08-27 for v1.6 — the `CTRL-18`…`CTRL-23` ↔ `MSP-08` ↔ `LEARN-04`/`LEARN-06` cluster

Re-derived from first principles, because that cluster is where B and B′ both lived. Samples arrive at
`k·P + φ` with arbitrary phase `φ ∈ [0, P)`; the count guaranteed in a hold of length `T` is
`min over φ of floor((T−φ)/P) + 1`.

```
20 Hz, P = 50 ms    T = 250 ms  →  5 samples guaranteed, 6 typical
10 Hz, P = 100 ms   T = 250 ms  →  2 samples guaranteed, 3 typical
10 Hz, P = 100 ms   T = 200 ms  →  2 samples guaranteed        (MSP-08's figure, correct)
20 Hz, P = 50 ms    5 samples   →  requires T ≥ 250 ms
10 Hz, P = 100 ms   5 samples   →  requires T ≥ 500 ms
```

**No contradiction remains in the cluster.** `CTRL-18` needs 2 and gets 2 at the fallback rate; `LEARN-06`
needs 5 and gets 5 only at 20 Hz, which is why `LEARN-04` elevates and why the fallback is closed to
learning. `CTRL-23` and `LEARN-06` do not interact, because `LEARN-05` emits no events during learning.

**One observation, recorded rather than repaired: the margin is exactly zero.** `CTRL-19`'s documented
250 ms is precisely `5 × 50 ms`, so the documented minimum hold is the *shortest* hold that satisfies
`LEARN-06` — a 240 ms hold yields four samples and is reported *unstable*. That degrades honestly, so no
number moves; it is written into `LEARN-04` so that a later change to the learning rate, the stability window
or the documented hold cannot be made without meeting the coupling. **Three requirements share one arithmetic
identity and none of them owned it.**

---

The original nine, **all inherited from v1.0** and all preserved faithfully by the compression:

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

It asked *is every conformance input in the PRS?* and answered `108/108` — a figure since **withdrawn as
unreproducible** (CR-31), though not for the reason this change request concerns. It never asked whether Evidence
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

### CR-11 — FC family and version gating · **CLOSED in v1.8**

`FC-05` gates on a version floor only. Two consequences are unaddressed **by decision, not by oversight**:

1. `SCOPE-01` states an INAV ceiling — *"8.0.1+ within 8.x"* — that no requirement enforces. INAV 9.0 passes
   `FC-05`.
2. **`FC-08`/`FC-09` send an unrecognised FC variant to telemetry-disabled compatibility with camera control
   *active*.** The entire `RCV-*` model — guarded commit, `armingDisableFlags`, `ARMING_DISABLED_RC_LINK` —
   assumes Betaflight or INAV mechanics, and an unknown variant's RC-validity and failsafe semantics have
   never been audited.

Item 2 is the safety-relevant half and does not depend on any hypothetical future release. It stayed open
by decision until v1.8, whose trigger this entry set itself: *"shall be resolved before implementation
reaches `FC-05`."* Phase 2 of the implementation plan implements `FC-05`, so the deferral expired on its
own terms rather than by anyone changing their mind.

**Resolution — item 1, the ceiling.** `FC-05` now gates INAV on `major == 8` **and** `triple >= 8.0.1`;
`FC-06` states that the INAV comparison is two-sided; `VAL-FUNC-02` runs an INAV 9.0.0 build against it.
`SCOPE-02` gains the ordering that stops the hole reopening: **forward-compatibility validation →
reviewed `SCOPE-01` delta → the release that supports it**. A build that passes validation is still
unsupported until `SCOPE-01` is amended, so the document cannot say *"within 8.x"* while a validated
INAV 9 is in service.

**Resolution — item 2, the safety-relevant half.** `FC-09` no longer routes an unrecognised variant into
telemetry-disabled compatibility. That state leaves camera control **active**, and the entire `RCV-*`
model — guarded commit, `armingDisableFlags`, `ARMING_DISABLED_RC_LINK` — is built on Betaflight and INAV
mechanics. An unaudited variant has no known RC-validity or failsafe semantics, so the bridge cannot tell a
live `MSP_RC` value from a held or substituted one. **It was the one place this specification let an
unaudited platform reach an actuator.** An unsupported variant now enters the named state
`UNSUPPORTED_FC_VARIANT` with camera control suppressed.

**`FC-08` becomes the canonical post-identification taxonomy**, four outcomes rather than *"the two
degraded states"* — a phrase that was already wrong the moment a third suppressing state existed. The
split is by **when the state is decided**: `FC-08`'s outcomes all follow a completed identification;
failure to complete identification at all is `FC-15`.

**`OSD-19` needed no edit, and that is worth recording.** `FC-09` was already a term of
`osdBackendEnabled`, so the OSD half of the variant case was always right. Only the camera-control half
was wrong — which is exactly why the defect survived: every audit that looked at unsupported variants
looked at OSD writes, and the column that mattered was the other one.

**The validation half is part of the closure, not a follow-up.** `VAL-FUNC-83` *asserted the behaviour
this CR deletes* — *"unsupported FC variant enters telemetry-disabled compatibility"* — so closing the
requirement without it would have left the case and the requirement contradicting each other. Amended:
`VAL-FUNC-83` (camera control suppressed, proven by MSP capture over a full control range),
`VAL-FUNC-119` (extended from OSD-only to camera control in both suppressing states), `VAL-FUNC-02`
(the ceiling), and `VAL-SPIKE-01` item 21 (three degrade kinds, not two, with a new subcase **d** for
the variant path and ordering re-lettered to **e**).

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

### CR-26 — the v1.3 header claimed an audit status the ledger did not support · **CLOSED in v1.4**

v1.3's PRS header read *"Seven audits re-run against the artifacts themselves … all clean except one open
item."* Four blocks carried a regeneration marker. Audit 6 had read 16 of its 116 flagged pairs and **said so
in this ledger**, and audit 7 had not been re-run at all.

**The ledger was honest and the header was not**, which is the more dangerous arrangement of the two: the
summary is what gets read and quoted, and it was the only place the shortfall was invisible. The header now
states each audit's state separately, because they differ.

**The rule this produces:** a status line reports the audit that ran least, not the audit that ran best.
Where audits differ in state, the header states them individually — it does not average them into a verdict.

### CR-27 — audit 7 was not re-run, and three contradictions were waiting · **CLOSED in v1.4**

Audit 7 reads one document against itself. It is the cheapest of the seven and the only one that can catch a
contradiction a delta *introduces*, and v1.3 changed the PRS without running it. §4c records the three it
found: the `PAIR-11`/`PAIR-03` equality collision, the `LEARN-04`/`MSP-08` rate conflict, and the OSD
suppression duplication (that one repaired under CR-29).

**The `PAIR-11` collision was found by an external reviewer instead.** `PAIR-12`'s interim value is the
`PAIR-03` bound, so the dwell elapses exactly when the bound is reached and `SCAN_TIMEOUT` cannot arise —
the same unreachability that was the stated reason for *rejecting* a fixed 30 s completion criterion one
delta earlier. **A repair recreated the defect it was chosen to avoid**, and the audit that would have said
so was skipped.

`PAIR-11` now states precedence explicitly and records the unreachability as a temporary property of an open
parameter; `VAL-FUNC-42`'s discovery half is deferred against `PAIR-12`'s closure rather than quietly
unrunnable.

**The rule this produces:** an audit that reads a document against itself is re-run whenever that document
changes. Cost is not the criterion — it is the only audit that can see a defect a delta created, and a delta
is exactly when one appears.

### CR-28 — a validation case decided things it had no authority over · **CLOSED in v1.4**

`VAL-SPIKE-07` closed `PAIR-12`, which `REL-02` delegates to it. It also directed that `PAIR-03` be raised if
the measurement exceeded that bound, and stated that a camera advertising unclassifiably *"shall be paired by
directed bind"*.

**The second is a hidden requirement — a `shall` binding an implementation, written in the validation
document, created by the delta that ran the audit which exists to find hidden requirements.** It is also
unimplementable: every directed-bind target originates from a classified candidate (`PAIR-01` → `PAIR-08` →
`PAIR-10`), so the contract offers no route to bind a camera discovery cannot classify. **Promoting it to a
requirement would have specified a mechanism that does not exist**; it was removed.

The first is a different error and worth separating. It obliges no implementation, so it is not a hidden
requirement — it is a spike helping itself to write authority over a requirement `REL-02` did not delegate to
it. Both now produce a change request instead of a decision.

**The rule this produces:** a delegated measurement may write back exactly the parameter delegated to it.
Every other consequence it implies — another bound, a product fallback, a scope change — leaves the spike as
a change request.

### CR-29 — one rule, three homes, three scopes · **CLOSED in v1.4**

`OSD-19` declares its three terms exhaustive and calls itself *"the single place that resolves"* the OSD
prohibitions. It was not: `MSP-06` independently suppressed OSD writes in telemetry-disabled compatibility
mode, and `FC-08`'s table asserted *"Platform OSD writes: none"* for both degraded states.

Neither contradicted `OSD-19` **today** — both are implied by it — which is precisely why this survived. The
defect is structural: `MSP-06`'s scope was one of `FC-08`'s two states, narrower than `OSD-19`'s three terms,
so **amending `OSD-19` would have left two stale statements of its own rule behind it.** Both now defer.

**The rule this produces:** where a requirement claims to be the single home of a rule, that claim is
checkable and shall be checked. A restatement that is currently consistent is not harmless — it is a
divergence scheduled for the next amendment.

### CR-30 — audit 3's method was narrower than audit 3's contract · **CLOSED in v1.4**

§3's contract reads *"every numeric value, threshold, **count** and boundary"*. v1.3 regenerated it by
extracting numeric literals carrying units. A closure — *"there are three and an implementation shall provide
no others"* — carries no unit, so the method could not see one, and six were missing: `CTRL-04`, `FC-11`,
`PAIR-10`, `SETUP-01`, `BOOT-08` and `OSD-19`.

**`OSD-19` settles the argument: v1.3 created that closure and regenerated this table in the same delta, and
reported the result clean.** A closed enumeration is as falsifiable as a threshold — an implementation
violates it by providing an N+1th — and it had never been in scope of the extraction.

The regeneration now runs two passes and **publishes the method**, so the next one can be checked against §3's
contract rather than against the previous regeneration.

**The rule this produces:** a regeneration is validated against the block's stated contract, not against the
previous regeneration. Otherwise the first narrow pass silently becomes the definition of the block.

### CR-31 — an inventory whose breakdown had never matched its headline · **CLOSED in v1.4**

v1.3 re-enumerated audit 5's headline from `108` to `114` and left the 13-row category table beneath it
byte-identical to v1.2. **It summed to 109 in both versions**, so it had agreed with neither headline in any
version of this document. It went unnoticed for three deltas because nothing ever added the rows up.

This is CR-23's failure — a measured block carried forward across a delta — committed **inside the block that
documents CR-23**, and by the delta that wrote the rule. Spot-checking confirmed the rows were independently
wrong as well: *"Command names 18"* against 20 in the PRS at the time.

The deeper problem was that **no counting unit had ever been stated**, so no figure was reproducible and none
could be reconciled with any other. §4a now states the unit, publishes the items or names the table that
holds them, and shows the two groups summing to the total. `108` and `114` are **withdrawn, not superseded**.

**The rule this produces:** a measured block that states a total and a breakdown shall be internally
checkable, and the check is part of producing it. A count with no stated unit is not a measurement, and
cannot be compared to the next one.

### CR-32 — audit 6 completed; the partial read had been reported as a result · **CLOSED in v1.4**

v1.3's reverse leg screened 413 pairs, read the 16 zero-overlap ones, and recorded plainly that 100 flagged
pairs were unread. The header called the audit clean anyway (CR-26). All 110 flagged pairs on the current
artifacts have now been read: **13 decorative citations dropped, 4 cases extended, 1 genuine coverage gap.**

**The coverage gap is the reason completing this mattered.** `RCV-09` requires a 100 ms floor when
`failsafe_delay = 0`. Both its cases exercised 5 and 50; **nothing exercised 0.** A bridge encoding zero grace
would have passed the entire suite while violating the only thing that requirement says. That is not a
citation defect — it is a hole in the tested surface, and only reading the citation against the case's actual
pass criterion exposes it.

**Two of v1.3's three repairs were incomplete**, both in the same way: the reported citation was fixed and its
twin on the same case was not. `VAL-FUNC-05` gained `OSD-19` and kept `FC-09`; `VAL-REV-11` lost `FAIL-02` and
kept `SAFE-06`, though a coverage roll-up can no more falsify one than the other. **Partial reads produce
partial repairs**, and both twins sat in the unread 100.

**The rule this produces:** where a screen orders a manual audit, the audit is not complete until every
flagged item is read. A screen's output is a work list, and a work list reported as a result is the same
error in a different place.

### CR-33 — `CTRL-22`'s behavioural clause was tested by nothing · **CLOSED in v1.5**

`CTRL-22` has two clauses: a press during a control freeze **is lost**, and setup documentation shall say so.
Its four cases covered the documentation clause twice and the behavioural clause **not at all**.
`VAL-FUNC-29` tested that recovery emits no *spurious* event — `RCV-14`'s rule — without ever making a press
during the freeze; `VAL-FUNC-117` tested one-shots across *camera* unavailability, which is `CAM-16`.

**Two different freezes, one word.** The RC-validity freeze and camera unavailability both "freeze" the
control path, and a citation that reads correctly under one meaning of the word is decorative under the
other. `VAL-FUNC-29` now presses during the guard and proves the press is never released — not on recovery,
not on the re-baseline that follows.

**The rule this produces:** where a requirement has two clauses, its coverage is checked per clause. A
requirement is not covered because it is cited; it is covered when each thing it obliges could fail a case.

### CR-34 — `CTRL-20`'s 400 ms was specified and untested · **CLOSED in v1.5**

Its only case, and `VAL-SPIKE-01` item 4, both read *"250 ms press detected; 100 ms press either detected or
ignored, never double-fired."* **"Never double-fired" tests the purpose of the interval, not the interval.**
An implementation with a 200 ms event-separation floor passes both verbatim.

This is `RCV-09`'s defect again one delta later — a named default whose case exercises everything around it —
and it was in the relations the screen never flagged. Both now measure the interval from both sides.

### CR-35 — `PROD-03` was cited by a review that exercised none of it · **CLOSED in v1.5**

`VAL-REV-07` checked BLE control, telemetry, OSD, video independence, wiring and hands-off operation.
`PROD-03` requires that **failure** of the bridge, the BLE link or the camera has no flight effect and no
measurable control-link degradation. Neither was exercised, and it was `PROD-03`'s only case.

**The repair looks like the defect this ledger removed in v1.4, and is not.** `VAL-REV-11`/`SAFE-06` was
dropped because a coverage roll-up cannot falsify `SAFE-06`, which has behavioural content of its own.
`PROD-03` has none of its own: it is a conjunction over failure modes that other requirements test.

**But the conjunction stated in v1.5 was wrong, and the error hid a coverage hole** (corrected v1.6, CR-41).
v1.5 wrote *"its content **is** the conjunction of `SAFE-06` and `RF-05`"*. `PROD-03` names **three** failure
modes — bridge, **Bluetooth connection**, camera — and `SAFE-06` enumerates bridge and camera faults only.
Link loss is in neither cited requirement, so the gate as written skipped a third of what it gates.
`VAL-FAIL-26` and `VAL-FAIL-27` now carry that member.

**A gate over cases is only as complete as the enumeration behind it**, and an enumeration written as a
conjunction of two IDs invites exactly this: the IDs look authoritative, and nobody re-reads the requirement
to check that they cover it. **Stated here because the distinction from `VAL-REV-11` is not visible from the
citation graph**, and the next reviewer would otherwise flag the repair as the defect it resembles.

### CR-36 — `VAL-SPIKE-07`'s and `MSP-06`'s repairs left stale references · **CLOSED in v1.5**

Bookkeeping, both from earlier deltas. Validation §3 opened *"Six spikes"* after `VAL-SPIKE-07` made seven,
contradicting the same document's `202 = 123/59/13/7` breakdown. The PRS provenance paragraph still named
only the v1.1 and v1.2 deltas, and still read *"the compression boundary itself changed no requirement"* —
which §6 of this ledger contradicts directly, recording seven obligations lost there. It now reads that no
requirement change was *intended* at the boundary and that seven were nevertheless lost.

**The rule this produces:** prose that counts or characterises the artifact set is a measured statement, even
when it reads like an introduction. Sentences of that kind are regenerated with the tables they describe.

### CR-37 — a total that summed while its membership did not · **CLOSED in v1.5**

v1.4's §4a reported `Group A 76 + Group B 62 = 138` with one input `absent from both`. The absent input
appeared in **neither group's published enumeration** — so the arithmetic check CR-31 introduced passed, and
the block still was not fully checkable.

**Summing is necessary and not sufficient.** A measured block that publishes a total, a breakdown and an
enumeration must satisfy all three against each other: the breakdown sums to the total, **and every counted
item appears in the enumeration.** CR-31's rule caught the first and not the second, which is why the second
is now written down.

### CR-38 — audit 6 read what a screen selected, not what the method requires · **CLOSED in v1.5**

The method requires every cited relation to be semantically checked, and separately says a lexical screen
*"does not perform the audit."* v1.4 read the 110 relations the screen flagged, left 293 unread, and reported
the audit complete — **the third consecutive delta to report a work list as a result**, and the first to do it
after writing the rule against it (CR-32).

All 403 relations have now been read *(403 as counted then; the unit was undefined and the true edge count is 464 — CR-45, v1.6)*. Five defects survived v1.4; **four were in relations the screen never
flagged**, because the citations most likely to be decorative are the ones whose subject matter matches, which
is what produces a high overlap score. The screen is retired as a selector.

**The fifth is the harder result.** `VAL-REV-07`/`PROD-03` was flagged, was read in v1.4, and was recorded as
entailed (CR-35). The case says the bridge *"works independently of the video system"*; the requirement is
about **failure** not affecting flight. A phrase matched and the claims did not. **So completing this audit
meant re-reading the 110 as well as the 293** — a relation read once is evidence, not proof.

**And one drop was self-inflicted by this same delta.** CR-29 moved the OSD-suppression condition from
`MSP-06` to `OSD-19`; `VAL-FUNC-83` went on citing `MSP-06` for a clause no longer in it. The citation still
named a real requirement, so nothing mechanical could see it. **Relocating normative text invalidates
citations without breaking them.**

**The rule this produces:** the reading set is every current relation. A screen may order it and may not
select it; a delta that moves a clause re-reads both ends; and prior verdicts are re-derived, not inherited.

### CR-40 — `CTRL-20` constrained spacing; two cases required suppression · **CLOSED in v1.6**

`CTRL-20` said consecutive Push Button events shall be separated by 400 ms. `VAL-FUNC-13` and `VAL-SPIKE-01`
item 4 required that two presses inside the interval yield **one** event — which is a stronger claim, and one
the requirement did not make. **Deferring the second press satisfies the spacing rule exactly**: the two
events would still be 400 ms apart.

**The test was right and the requirement was incomplete**, which is the reverse of the usual finding and is
why it survived: a reviewer checking whether the case is entailed by the requirement sees a plausible
connection, and only a reviewer asking whether the requirement *admits an implementation the case rejects*
sees the gap. `CTRL-20` now says discarded, not queued or deferred, and both cases assert the absence of a
late second event rather than only the absence of an immediate one.

**The rule this produces:** a case that constrains more than its requirement is a defect in the requirement
until proven otherwise. Entailment runs in one direction and is checked in that direction; **admissibility
runs in the other**, and a conforming implementation that fails a case is the symptom.

### CR-41 — `PROD-03`'s gate covered two of its three failure modes · **CLOSED in v1.6**

See CR-35, revised in place. `PROD-03` covers failure of the bridge, **the Bluetooth connection** and the
camera; the v1.5 gate named `SAFE-06` cases and `VAL-SPIKE-02`, and `SAFE-06` enumerates bridge and camera
faults only. `VAL-FAIL-26` and `VAL-FAIL-27` now carry the link-loss member.

### CR-42 — a repair justified by arithmetic nobody checked · **CLOSED in v1.6**

v1.4 resolved the `LEARN-04`/`MSP-08` contradiction correctly and justified it with *"a 250 ms momentary
demonstration spans two samples at 20 Hz and may span one at 10 Hz."* **Both figures are wrong** — it is five
and two — and the second contradicts `MSP-08` on the same page, which states correctly that two consecutive
samples need 200 ms at 10 Hz.

The policy was right for a reason nobody had written down: **`LEARN-06` needs five samples for its stable
position predicate, and `CTRL-18` needs two.** Only the second survives the fallback rate. `LEARN-04` now
carries the sample counts and the phase analysis.

**Audit 7 passed over this twice**, and could not have caught it: it compares requirements to each other, and
**a wrong reason for a right rule contradicts nothing.** §4c now records the arithmetic explicitly.

**The rule this produces:** a justification containing a computation is a measured claim, not prose. It is
re-derived when the block around it is regenerated, and it names the requirement whose number it depends on.

### CR-43 — a case mandated a permitted optimisation · **CLOSED in v1.6**

`RCV-04` explicitly permits collapsing three transactions to two where a sample implies no transition.
v1.5's `VAL-FUNC-24` extension required the two-transaction pattern to be observed — **so a conforming
implementation that brackets every poll would fail it**, and the case's own text said as much while
requiring it anyway.

Same shape as CR-40 and found in the same pass. The case now classifies the build into either permitted
pattern and fails only the third: a transition-bearing poll carrying two transactions, which is `RCV-03`
violated under `RCV-04`'s cover.

### CR-44 — `CTRL-13` has no platform-evidence chain · **CLOSED in v1.7**

`CTRL-13` requires channel count to come from the `MSP_RC` **response length** rather than a constant. That is
a claim about what both firmwares serialize, and it carries **no `PF-*` reference** — while §4 asserts that
every platform-dependent requirement carries at least one. It is a genuine gap in audit 4, not a citation
style question: an implementer has no recorded basis for believing the response length tracks the FC's
channel count.

The fact to record is **one combined Betaflight/INAV platform fact** — no ID is reserved here, because a
reserved ID reads as a citation and would dangle — covering `MSP_RC = 105` at both pinned tags, Betaflight
serializing `rcData[0..channelCount-1]`, INAV serializing `rxGetChannelValue(0..channelCount-1)`, and the
conclusion that response payload cardinality derives from FC channel count.

**It was not written when this was raised**, because the serializer locations had not been read at
`2025.12.5` and `8.0.1`, and §4's *"Unresolved platform assertions: 0"* is only true while no platform claim
in Evidence lacks a file and line. Adding one without them would have made that figure false in order to
close the gap in the figure beside it.

**Closed in v1.7 from the pinned tags** as `PF-BF-23` — one fact covering both platforms, following
`PF-BF-04` rather than the split `PF-BF-02`/`PF-INAV-01` pattern, because the behaviour is identical on each
and only differing values justify splitting.

**Waiting paid a second time, for a different reason than CR-24.** The fact is not *"`MSP_RC` is 105"* — that
was already in `SAFE-03`. It is that both platforms emit `2 × channelCount` bytes **with the count encoded
nowhere in the payload**, so the frame length is the only carrier and `channelCount = payloadLength / 2` is
the sole derivation. `CTRL-13` had asserted the conclusion for six versions with nothing behind it, and the
conclusion is only as good as that absence-of-a-count-field observation.

**Two things in the proposed wording were changed.** The suggested fact ended *"the bridge **shall** derive
the available channel count from the received payload length"* — a normative obligation in Evidence, which is
the CR-01 defect the §4 deontic scan exists to catch, and `CTRL-13` already carries it. The fact states the
wire format and stops; the scan still reports 12. And the observation that Betaflight reads `rcData[i]`
while INAV calls `rxGetChannelValue(i)` is recorded **without** adding a `CTRL-24` citation: it corroborates
that requirement's framing but `PF-INAV-03` is its basis, and a second citation there would be decorative in
exactly the way CR-25 and CR-32 describe.

### CR-47 — audit 3 was blind to derivations · **CLOSED in v1.7**

Closing CR-44 exposed it. §3's contract covers *"every numeric value, threshold, count and boundary"*, and
its regeneration ran two passes: literals carrying units (CR-30's original), and closed enumerations (CR-30's
addition). **`CTRL-13` is neither.** *"The response length determines how many channels are available"*
states no literal and closes no set — it is a **derivation**, a rule that fixes a quantity by relating it to
another — so both passes were structurally blind to it, and it had been outside the boundary table since
v1.0 without anyone noticing.

A third pass now looks for formulas, ratios and derived-from relations. It found `CTRL-13`; the derivations
already in the table — `RCV-08`'s `grace`, `RCV-10`'s per-transaction extension — were there because they
carry units and pass 1 caught them incidentally.

**The rule this produces, and it is CR-30's generalised:** a regeneration method is validated against the
block's *contract*, and a contract phrased as a list of examples ("value, threshold, count, boundary")
invites a method that matches the examples rather than the category. **Where a block's scope is a category,
the method states how it enumerates the category** — not which shapes of it the author happened to think of.

### CR-48 — a section heading was deleted by an anchor-based edit · **CLOSED in v1.7**

v1.5's `PF-BF-22` insertion replaced an anchor ending `--- ## 2. INAV platform facts` with text that did not
restore it. **Every INAV platform fact has been sitting under `## 1. Betaflight platform facts` for two
deltas**, and nothing caught it: the structural verifier checks IDs, references, fences and table shape, and
does not check that a document's own section structure survives an edit.

Restored in v1.7. **The class matters more than the instance:** an edit anchored on `old → new` deletes
whatever was inside the anchor and not reproduced in the replacement, silently and without breaking any ID.
Three audits and a verifier passed over it, because all of them read the parts and none reads the skeleton.
The verifier now has one more thing it should check, and until it does, **an anchor that spans a heading is
re-read after replacement** rather than assumed.



### CR-45 — the relation count had no unit, and the spikes were outside it · **CLOSED in v1.6**

Audit 6 reported 414, then 403, with no definition of what one relation was. The counter tokenised
backticked IDs, so `RES-01`…`RES-05` counted as **two** relations rather than five, and the seven spikes —
prose sections with no `Verifies` cell — contributed **none at all** while being described as covered.

`464` replaces both: **406 table-case edges + 58 spike edges**, under a stated unit — one explicit
`(case ID, requirement ID)` pair. The range is expanded, every spike carries a `**Verifies:**` line, and the
counting script **refuses to emit a number** if it finds a range or wildcard rather than silently counting
the endpoints.

**This is audit 5's lesson arriving at audit 6 one delta late** (CR-31): a reported total needs a stated
unit and enumerable membership. It was written down for conformance inputs and not applied to relations,
because nobody asked whether the relation count had a unit — it looked like a count of visible things.

### CR-46 — measured blocks mixing generations · **CLOSED in v1.6**

Three places presented figures from different artifact generations as one measurement. §4a still carried a
paragraph explaining that ARM's permanent ID was `absent from both` and `OPEN` as CR-24, immediately below
the regenerated block reporting `139/139` and `absent from both 0`. §4b's totals mixed v1.4's 414 with
v1.5's 403 without marking either as historical. Compression rule §6.9 computed a lesson from `110 of 414`
and stated it against a 403-relation artifact.

All three are now dated snapshots or deleted. **Historical figures belong in the change-request log, which is
never edited; measured blocks state the current artifact and nothing else.** The failure mode is specific:
a reader scanning a measured block cannot tell which sentences are measurements, so one stale paragraph
makes the whole block untrustworthy rather than partly wrong.

### CR-24 — a conformance input is absent from both documents · **CLOSED in v1.5**

`FC-12` requires a Betaflight `MSP_BOXIDS` lookup *"to locate the ARM box bit"*. The lookup returns permanent
box IDs in the order their bits appear in `MSP_STATUS`'s flight-mode flags, so locating ARM means knowing
**ARM's permanent box ID** — and that constant is in neither the PRS nor Evidence.

**It was not filled in when this was raised**, because the value had not been read at Betaflight
`2025.12.5` and a platform constant asserted without reading its source is exactly the class of claim
`SCOPE-02` and the Evidence citation discipline exist to prevent.

**Closed in v1.5 from the pinned tag**, recorded as `PF-BF-22` with file and line, and stated normatively in
`FC-12`. **The correspondence turned out to matter more than the constant.** `permanentId = 0` alone would
have invited the natural implementation — treat ARM as bit 0 — which is correct only on a craft where ARM is
the sole active box, because `MSP_BOXIDS` reports only *active* boxes and `packFlightModeFlags()` indexes the
same traversal. Everywhere else it reads an unrelated mode's bit **as the arming interlock**, silently, in the
one place whose whole purpose is refusing maintenance entry while the aircraft is armed.

So the recorded gap was worth more than a guessed value would have been: a guess would have been *right about
the number* and wrong about the thing that uses it. `VAL-FUNC-124` is the case that separates them — it runs
with other modes active so ARM is not at position 0, and a hardcoded implementation passes every other gesture
case and fails that one.

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

## 5e. v1.4 audit delta — applied 2026-08-27

**What triggered it.** External review of the v1.3 artifacts. Its central claim was not about a requirement:
it was that *"seven audits re-run and clean except CR-24"* was unsupported by the ledger sitting underneath
it. That was correct, and re-checking it found more than was reported — audit 7 had not been re-run either,
audit 3's gap was six closures rather than two, and audit 6's completion turned up a coverage hole in the
tested surface.

**Four of the five reported findings were upheld. None was rejected.** One was reclassified: the reviewer
read `VAL-SPIKE-07`'s two overreaches as one kind of defect, and only the unclassifiable-camera fallback is
a hidden requirement — the `PAIR-03` instruction obliges no implementation and is a change-control overreach
instead (CR-28).

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | PRS status header states each audit's state separately; the single verdict is withdrawn | self-description | CR-26 |
| 2 | Audit 7 re-run against the current PRS; §4c records the run and its three findings | audit | CR-27 |
| 3 | `PAIR-11` — precedence at equality stated; completion wins over `SCAN_TIMEOUT` | specification | CR-27 |
| 4 | `PAIR-11` — `SCAN_TIMEOUT` recorded as unreachable while `PAIR-12` is interim | specification | CR-27 |
| 5 | `VAL-FUNC-42` — discovery half deferred against `PAIR-12`'s closure, not silently unrunnable | validation | CR-27 |
| 6 | `LEARN-04` — scoped against `MSP-08`'s 10 Hz fallback; a target that cannot reach 20 Hz declines Push Button controls rather than mis-learning them | specification | CR-27 |
| 7 | `VAL-SPIKE-07` — unclassifiable-camera directed-bind fallback **removed** as a hidden requirement, and unimplementable under `PAIR-01`/`PAIR-08`/`PAIR-10` | validation | CR-28 |
| 8 | `VAL-SPIKE-07` — exceeding the `PAIR-03` bound leaves `PAIR-12` `OPEN` and raises a change request | validation | CR-28 |
| 9 | `VAL-SPIKE-07` — an explicit statement of what the spike may and may not decide | validation | CR-28 |
| 10 | `MSP-06` — OSD suppression clause defers to `OSD-19` instead of naming a narrower condition | specification | CR-29 |
| 11 | `FC-08` — OSD column attributed to `OSD-19`; the camera-control column stays its own | specification | CR-29 |
| 12 | §3 — six closed enumerations added: `CTRL-04`, `FC-11`, `PAIR-10`, `SETUP-01`, `BOOT-08`, `OSD-19` | audit | CR-30 |
| 13 | §3 — regeneration runs a second closure pass, and the method is published | method | CR-30 |
| 14 | §3 — `RCV-17`'s row named `RC_LINK`; corrected to `ARMING_DISABLED_RC_LINK` | audit | CR-30 |
| 15 | §4a — re-enumerated with a stated counting unit; items published or their table named; groups shown summing to the total | audit | CR-31 |
| 16 | §4a — `108` and `114` withdrawn rather than superseded | audit | CR-31 |
| 17 | §4a — prose cited `PAIR-06` for the cache-content classes; that is `PAIR-10` | audit | CR-31 |
| 18 | Audit 6 completed: all 110 flagged pairs read individually | audit | CR-32 |
| 19 | 13 decorative citations dropped, each requirement falsified by a case named in §4b | validation | CR-32 |
| 20 | `VAL-FAIL-09` — extended to `failsafe_delay = 0`, closing `RCV-09`'s untested 100 ms floor | validation | CR-32 |
| 21 | `VAL-FUNC-12`, `VAL-FUNC-98`, `VAL-FUNC-95` — extended rather than stripped, being their requirement's only case | validation | CR-32 |
| 22 | §4b — scope corrected: the screen covers 195 table rows, not 202 cases; the 7 spikes were read directly | audit | CR-32 |
| 23 | Compression rule §6.8 — a regeneration shall cover the block's stated contract, and a measured block shall be internally checkable | method | CR-30, CR-31 |

**One requirement changed meaning, and none was added or removed.** Rows 3, 4, 6, 10 and 11 amend existing
requirements; row 7 deletes an obligation that should never have been in the validation document. The
requirement count is unchanged at **247**.

### What this delta says about the two before it

v1.2 froze on four stale measured blocks. v1.3 found them, wrote the rule against them (CR-23), and then
**broke that rule twice inside the blocks it was regenerating** — carrying forward audit 5's breakdown
(CR-31) and regenerating audit 3 by a method narrower than audit 3's contract (CR-30). It also created two of
the three contradictions audit 7 has now found, and did not run audit 7.

**The pattern across three deltas is not carelessness; it is that a delta is the moment a document is most
likely to acquire a defect and least likely to be checked for one** — the attention is on the repair, and
the repair is assumed to inherit the correctness of its motivation. Every rule added in v1.3 and v1.4 is a
response to that: regenerate measured blocks (CR-23), regenerate them *completely* (CR-30), make them
self-checking (CR-31), re-run the within-document audit whenever the document changes (CR-27), and never
report a work list as a result (CR-26, CR-32).

**What this delta does not claim.** That the artifacts are now clean. It claims each audit's state is stated
accurately, one item is open, and the reader can check every measured figure in this document against the
artifacts without trusting a summary — which is a weaker claim than v1.3 made and, unlike v1.3's, is
supported by what is written underneath it.

---

## 5f. v1.5 audit delta — applied 2026-08-27

**What triggered it.** External review of the v1.4 artifacts, which upheld the v1.4 repairs and then made one
structural objection: audit 6 had read the relations a screen selected, not the relations the method requires.
It supplied five surviving defects as proof and the Betaflight source locations that close CR-24.

**Every reported finding was upheld. Two were larger than reported, and one had a different cause than the
reviewer assigned it** — `VAL-REV-07`/`PROD-03` was not missed because the screen failed to flag it. It was
flagged, read in v1.4, and misjudged.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `PF-BF-22` — ARM permanent ID **0**, and the `MSP_BOXIDS`↔`packFlightModeFlags` correspondence, cited at `2025.12.5` | evidence | CR-24 |
| 2 | `FC-12` — locate ARM by searching for permanent ID 0 and using its response position as the status-bit index | specification | CR-24 |
| 3 | `FC-12` — a hardcoded bit 0 is prohibited, and an absent permanent ID 0 leaves the Maintenance Entry AUX unconfigured rather than the interlock running without an input | specification | CR-24 |
| 4 | `VAL-FUNC-124` — new case: ARM deliberately not at position 0, so a hardcoded implementation fails | validation | CR-24 |
| 5 | `VAL-FUNC-29` — a press is now made **during** the freeze and proven lost, closing `CTRL-22`'s behavioural clause | validation | CR-33 |
| 6 | `VAL-FUNC-117` — `CTRL-22` citation dropped; it tests `CAM-16` | validation | CR-33 |
| 7 | `VAL-FUNC-13` and `VAL-SPIKE-01` item 4 — `CTRL-20`'s interval measured from both sides | validation | CR-34 |
| 8 | `VAL-REV-07` — `PROD-03` gated over the `SAFE-06` and `RF-05` cases, with the distinction from `VAL-REV-11` recorded | validation | CR-35 |
| 9 | `VAL-FUNC-122` — `MSP-01` citation dropped; the case tests the allowlist, not a poll rate | validation | CR-38 |
| 10 | Validation §3 — "Six spikes" → seven | bookkeeping | CR-36 |
| 11 | PRS provenance — all five deltas named; the boundary claim corrected to *intended*, with the seven lost obligations stated | bookkeeping | CR-36 |
| 12 | §4a — Group B gains `FC-12`'s constant; totals 139/139/0; the v1.4 membership defect recorded | audit | CR-37 |
| 13 | §3 — ARM permanent ID and its correspondence added as a boundary row | audit | CR-37 |
| 14 | **Audit 6 read in full: all 403 relations, including the 110 v1.4 had read** | audit | CR-38 |
| 15 | `VAL-FUNC-24` — `RCV-04`'s optimisation verified in both directions | validation | CR-38 |
| 16 | `VAL-FUNC-26` — a non-stop Combi button proven to stay out of the arbitration (`CTRL-10`) | validation | CR-38 |
| 17 | `VAL-FUNC-27` — the **persisted** range re-read after driving both thresholds (`CTRL-17`) | validation | CR-38 |
| 18 | `VAL-FUNC-83` — `MSP-06` → `OSD-19`; the cited clause had been moved by v1.4's own CR-29 | validation | CR-38 |
| 19 | `VAL-FUNC-115` — `MSP-07` citation dropped | validation | CR-38 |
| 20 | Compression rule §6.9 — the reading set is every current relation; relocation invalidates citations; stored-state requirements need a case that reads the store; prior verdicts are re-derived | method | CR-38 |

**247 requirements, 203 validation cases, 38 platform facts, 403 relations.** One requirement gained
normative content (`FC-12`); none was added or removed.

### What is now closed, and what that took

**CR-24 is closed, and it is the only item that has ever been left `OPEN` for want of a fact rather than a
decision.** It stayed open for two deltas because the value had not been read at the pinned tag. The record
argues for having waited: the constant alone would have produced the natural and wrong implementation — ARM
is permanent ID 0, therefore ARM is bit 0 — which fails on any craft with a second active mode, silently, in
the arming interlock. **The gap was worth more than a correct guess**, because what was missing was not the
number.

**Audit 6 is complete for the first time.** Not "clean" — complete, meaning every relation currently in the
artifacts was read against the requirement it cites. Five defects survived v1.4's partial read; four were
never flagged, and the fifth was flagged, read, and misjudged.

### The pattern this delta ends, and the one it does not

Four consecutive deltas reported a partial result as a complete one: v1.2 froze on stale blocks, v1.3 called
seven audits clean when four had run, v1.4 called audit 6 complete on 27 % of its relations, and each wrote
the rule its successor then broke in a new place. **v1.5 breaks the sequence in one specific sense** — the
audit's own definition of its scope is now satisfied rather than approximated.

**It does not make the artifacts correct, and this ledger should not be read as claiming that.** One relation
in 110 was misjudged on a careful pass, so the honest estimate is that a handful of the 403 are still wrong
and nobody knows which. What has changed is narrower and worth having: **the claim made in the header is now
the claim the evidence underneath supports**, and where the evidence is a judgement rather than a measurement,
it says so.

---

## 5g. v1.6 audit delta — applied 2026-08-27

**What triggered it.** A joint repair order from external review of the v1.5 artifacts. Three of its items
were new technical defects, three were ledger and editorial cleanup, and one — the definition of a relation —
turned out to invalidate a measurement v1.5 had presented as its headline result.

**Every item was upheld. Two were larger than reported.** The `PROD-03` gate was missing a member *and* the
CR that justified it stated a false conjunction; and the review's own suggested `LEARN-04` wording, while
correct in direction, needed the phase analysis written out before the numbers could be trusted — which is
how the zero-margin coupling between `CTRL-19`, `LEARN-04` and `LEARN-06` surfaced.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `CTRL-20` — a press inside the interval is **discarded**, not queued or deferred | specification | CR-40 |
| 2 | `VAL-FUNC-13`, `VAL-SPIKE-01` item 4 — assert the absence of a *late* second event, not only an immediate one | validation | CR-40 |
| 3 | `VAL-FUNC-24` — classifies the build into either permitted transaction pattern; `RCV-04` stays optional | validation | CR-43 |
| 4 | `LEARN-04` — rewritten around sample counts: 5 at 20 Hz, 2 at 10 Hz, against `LEARN-06`'s five-sample predicate | specification | CR-42 |
| 5 | `LEARN-04` — the zero-margin coupling of `CTRL-19`'s 250 ms to `LEARN-06`'s five samples, recorded so a later change cannot miss it | specification | audit 7 |
| 6 | `MSP-08` — states that 250 ms guarantees `CTRL-18`'s two observations at 10 Hz under adverse phase | specification | CR-42 |
| 7 | §3 — the `MSP-08` row rewritten to say what the 200 ms figure is | audit | CR-42 |
| 8 | `VAL-REV-07` — the `PROD-03` gate names all three failure modes; `VAL-FAIL-26`/`VAL-FAIL-27` carry the Bluetooth-link member | validation | CR-41 |
| 9 | CR-35 — rationale corrected in place; `PROD-03` is not `SAFE-06 ∧ RF-05` | ledger | CR-41 |
| 10 | `VAL-FUNC-54` — `RES-01`…`RES-05` expanded to five explicit citations | validation | CR-45 |
| 11 | `VAL-SPIKE-01`…`VAL-SPIKE-07` — every spike carries an explicit `**Verifies:**` line | validation | CR-45 |
| 12 | Audit 6 — relation defined; **464 edges** = 406 table + 58 spike, under a stated unit | audit | CR-45 |
| 13 | Audit 6 — all 464 read semantically; the 406 re-derived, the 58 read for the first time; **0 defects** | audit | CR-45 |
| 14 | Audit 7 — re-run on the `CTRL-18`…`CTRL-23` ↔ `MSP-08` ↔ `LEARN-04`/`LEARN-06` cluster from first principles | audit | CR-42 |
| 15 | §4a — the stale `absent from both` / `OPEN` paragraph deleted; its history stays in CR-24 | audit | CR-46 |
| 16 | §4b — every pre-v1.6 figure marked as a dated snapshot | audit | CR-46 |
| 17 | Compression rule §6.9 — the `110 of 414` lesson de-mixed from the 403-relation artifact | method | CR-46 |
| 18 | Compression rule §6.9.1 — a relation count is a measurement: stated unit, no ellipsis, counter refuses rather than guesses | method | CR-45 |
| 19 | `SETUP-11` — duplicated word removed | bookkeeping | CR-46 |
| 20 | PRS provenance — each delta named; §5d is the freeze rule, not a delta | bookkeeping | CR-46 |
| 21 | `CTRL-13` — no platform-evidence chain; **left OPEN** rather than cited without reading the tags | audit | CR-44 |

**247 requirements, 203 validation cases, 38 platform facts, 464 relations.** Three requirements gained
normative content (`CTRL-20`, `LEARN-04`, `MSP-08`); none was added or removed.

### The two findings worth keeping

**A case can be wrong by being right.** `CTRL-20` and `RCV-04` produced the same shape twice in one delta:
a validation case that constrains **more** than its requirement. Every audit here checks entailment — does
the requirement imply the case — and neither of these fails that test. What they fail is the reverse:
**does the requirement admit an implementation the case rejects?** A deferred press satisfies `CTRL-20` as
written and fails `VAL-FUNC-13`; a bracket-always build satisfies `RCV-04` and failed `VAL-FUNC-24`. One is
repaired in the requirement and one in the case, and telling them apart is the judgement — but neither is
visible while reading in the direction the audit was designed for.

**A right rule can rest on a wrong reason indefinitely.** `LEARN-04`'s policy was correct from v1.4 and its
stated justification was arithmetic that is off by a factor in both directions. Audit 7 passed over it
twice and could not have caught it: consistency is a relation between requirements, and **a false
computation inside a requirement contradicts nothing.** Prose that computes is a measurement wearing prose's
clothes, and it is now regenerated like one.

### What this delta does not claim

That the artifacts are correct. 464 relations were read and none failed, which is a stronger result than any
previous delta produced and is still one careful pass by one reader — v1.5 read 110 relations carefully and
misjudged one of them. **The count is now reproducible, the membership is published, and the unit is stated;
none of that makes a judgement right.** CR-44 is open, CR-11 is open by decision.

---

## 5h. v1.7 delta — applied 2026-08-27

**What triggered it.** The Betaflight and INAV serializer locations, read at the pinned tags and supplied by
external review, closing the last open finding that was open for want of a fact rather than a decision.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `PF-BF-23` — `MSP_RC = 105` on both platforms; both serialize one U16 per `channelCount` with no count field | evidence | CR-44 |
| 2 | `CTRL-13` — states `channelCount = payloadLength / 2` and cites the fact | specification | CR-44 |
| 3 | `CTRL-12` — cites the fact for the shared `105` | specification | CR-44 |
| 4 | The proposed fact's closing `shall` **not** carried into Evidence; obligation stays in `CTRL-13` | evidence | CR-44 |
| 5 | The `rcData` / `rxGetChannelValue` difference recorded **without** a `CTRL-24` citation | evidence | CR-44 |
| 6 | §3 — `CTRL-13`'s derivation added; a third regeneration pass for derivations | audit | CR-47 |
| 7 | §4a — Group B 63 → 64; conformance inputs **140/140** | audit | CR-47 |
| 8 | §4 — platform-dependent requirements now have **no** exceptions; `CTRL-12`, `CTRL-13` added to the list | audit | CR-44 |
| 9 | Evidence — `## 2. INAV platform facts` heading restored after two deltas without it | structure | CR-48 |

**247 requirements, 203 validation cases, 39 platform facts, 464 relations.** No requirement, case or
relation was added or removed; `CTRL-12` and `CTRL-13` gained content.

### What closing this cost, and what it bought

**Held open for two deltas, and the delay earned its keep twice.** CR-24 waited and produced a
correspondence more important than the constant. CR-44 waited and produced an *absence* — no count field
anywhere in the payload — which is the whole basis for `CTRL-13` and is not visible from the value `105`
that the review's first sentence supplied. **A fact recorded from memory would have contained the number and
missed the reason.**

**Both edits to the proposed wording were about which document holds what.** A `shall` in Evidence and a
citation on a requirement that already had its basis are small things individually; they are the two defect
classes this ledger has spent five deltas removing, and they arrived together inside a correct and welcome
contribution. **The discipline is not suspicious of the content — it is indifferent to where the content
came from**, which is the only form of it that works.

### The finding neither side was looking for

`CTRL-13` was outside the boundary table for **seven versions**, and closing its evidence gap is what
surfaced it (CR-47). Audit 3's two passes look for literals and for closures; a derivation is neither. The
generalisation is uncomfortable and worth stating: **a contract phrased as examples produces a method that
matches the examples.** §3 says *"value, threshold, count and boundary"*, and two regenerations built exactly
those two matchers.

And `PF-BF-22`'s insertion had silently deleted a section heading in v1.5 (CR-48). **Three audits and a
structural verifier ran over that document twice without noticing**, because every one of them reads the
parts and none reads the skeleton.

---

### CR-49 — spike `Verifies:` lines were authored, not derived · **CLOSED in v1.8**

Five requirements carried no relation under §4b's unit — `RF-04`, `SAFE-01`, `CAM-01`, `RES-06`,
`RES-08` — while audit 4 reported *"requirements with no validation case: 0"*. Both were true at once:
the figure was produced under the loose reading, where any mention counts, and v1.6's unit definition
(CR-45) silently falsified it. **A unit definition that repairs one claim can falsify older claims, and
nothing re-checked them.**

**The five were a symptom.** Re-deriving every spike's line from its pass criteria found **22 body-only
mentions across five spikes**. `VAL-SPIKE-01` had 38 declared and **zero** gaps; the other six were
hand-authored in the same v1.6 delta, with no note that two methods were in use.

**Five adjudication classes, ruled against the pass criteria and never against the mention:**

| Class | Test | Action |
| --- | --- | --- |
| relation | a numbered pass criterion exercises it | add to `**Verifies:**` |
| cross-reference | a pointer to a rule living elsewhere | leave in body, no edge |
| consequence | failure *implicates* it without testing it | no edge; find the case that does |
| negative scoping | named in order to be excluded | no edge; fix what the exclusion exposes |
| **misattribution** | the cited requirement does not say what is claimed | **remove the citation** |

**11 mentions became relations** — `RF-04` on spike 2; `OSD-02`, `RES-02`…`RES-05`, `RES-08`, `RES-09`,
`REL-02` on spike 3; `RES-09`, `REL-02` on spike 4. `RF-04` is the sharpest: `REL-02` already named
spike 2 as its closer, so the registry and §4b were asserting opposite things about the same pair.

**Two were removed as misattributions**, which is a worse class than decorative and where removal is not
optional. Spikes 6 and 7 both recorded a negative result *"per `CAM-08`"*. **`CAM-08` is the camera
compatibility lifecycle** — `Unsupported → Expected → Verified` by named evidence — and states no general
rule that negative experimental results are recorded. Spike 4 cites `CAM-08` correctly, about promotion,
which is the contrast that proves the other two were borrowing authority the requirement does not carry.

**Nine remained in the bodies with no edge at this adjudication, all deliberately** — a figure true when
measured and superseded later in the same delta, see CR-55: `SAFE-01` and `CAM-01` (consequence),
`RES-06` (negative scoping), `CTRL-12`, `PAIR-08`, `REL-01`, `RF-04`-on-spike-7, `SCOPE-03`, `SCOPE-05`
(cross-reference). **`RF-04` is a relation on spike 2 and a cross-reference on spike 7** — the same ID,
opposite rulings, because spike 2 determines the value and spike 7 merely tests *at* the far end of range.
That pair is the argument for ruling per criterion rather than per identifier.

**Three cases carry what the spikes never tested:**

| Case | Verifies | Why the spike did not cover it |
| --- | --- | --- |
| `VAL-REV-14` | `SAFE-01` | Spike 2 measures RF degradation, which is `RF-05`'s; no clause of `SAFE-01` is exercised by an attenuator sweep |
| `VAL-REV-15` | `CAM-01` | Spike 3 ports the driver *"behind the `CAM-01` interface"* but no pass criterion tests that isolation |
| `VAL-FUNC-125` | `RES-06` | Spike 3 names `RES-06` **only to exclude it**; `Opt`/`Opt`, gating V1.2 and not V1 |

`RES-06` needed a case rather than a scope note because §6's invariant — *"0 orphan requirements (every
requirement has ≥ 1 validation case)"* — carries **no applicability exception**. `REL-01` has applicability
semantics; the graph invariant does not, and it was not weakened to make an orphan disappear.

### What this cost to find, and one defect it created

**The headline count was wrong twice before it was right.** The first extractor bounded each spike by the
*next spike heading*, so the final block ran to end-of-file and absorbed `## 4. Regression procedure` —
which is where the `SAFE-02` briefly attributed to spike 7 actually lives. The corrected extractor bounds
by the next heading of **any** level, and the count is 22, not 23. *A measured figure from a script whose
bounds were never checked is the same defect as a hand-authored `Verifies:` line.*

**And applying the fix introduced a third instance of the same class.** Spike 3's new line was written
wrapped across two lines for readability. **Both `spikegap2.py` and `v16_edges.py` read exactly one line**,
so three of the eleven new edges — `RES-08`, `RES-09`, `REL-02` — vanished silently and reappeared as
body-only mentions on the next run. Nothing in the format states that a `Verifies:` line must not wrap,
and nothing checked it. The extractor now **refuses to run** on a wrapped line rather than undercounting.

**Result: 0 orphan requirements under the strict unit**, for the first time since the unit was defined.
Spike edges 58 → 71, table edges 406 → 412, total 464 → 483 across 206 cases — every figure re-derived
from disk, none carried forward.

---

### CR-52 — no MSP transaction deadline anywhere · **OPEN until the write-back**, raised in v1.8

Three requirements turn on being able to declare an MSP request failed — `RCV-10` extends a qualification
interval per *"unanswered or failed"* transaction, `MSP-06` requires tolerating *"a missing or late reply"*,
`MSP-07` treats an incomplete bracket as a failed one — and **no requirement said when that happens.**
`REL-02`'s own defect rule did not catch it: that rule covers a parameter *delegated* elsewhere, and this
one was simply absent. Two conforming implementations at 20 ms and 500 ms would qualify at different times
against the same FC.

**`MSP-09` separates three things the first draft of this repair conflated.**

| Concept | Disposition |
| --- | --- |
| Transaction deadline | **`OPEN`** in `REL-02`. Measurement: `VAL-SPIKE-01`. **Closure: a reviewed write-back recorded against this CR** |
| Retry backoff | **1.0 s fixed, post-terminal** — a literal in `MSP-09`, not a registry row, because it is product cadence and not a physical unknown |
| One outstanding transaction | an invariant, stated in `MSP-09` |

**`VAL-SPIKE-01` is not the closer, and the distinction is the point.** A spike that selects the value it
measured is the delegated-vagueness defect `REL-02` exists to prevent — *"measured worst case + margin"* is
not a rule, because the margin is unnamed. Three roles stay apart: **the spike measures, a reviewed decision
closes, and a re-run verifies.** This CR therefore stays open until the write-back, exactly as CR-24 and
CR-44 stayed open for facts that did not exist yet.

**The invariant was not written down anywhere, which nobody noticed until it was needed.**

> **Attribution note.** Successive reviews, and this ledger's own drafts, repeatedly attributed the
> one-outstanding-transaction invariant to `MSP-01` — *"the invariant stays in `MSP-01`"*. A search of the
> full artifact set for `outstanding`, `one request`, `single request`, `in flight at a time` and `pipelin`
> returned **no such requirement, in any document**. `MSP-01` is a poll-rate table and says nothing about
> concurrency. **`MSP-09` therefore introduces the invariant normatively rather than cross-referencing
> `MSP-01`.**

This note exists because without it the historical argument is unreadable: several rounds of review turn on
*preserving* a rule that was never written, and a later reader would look for the `MSP-01` clause being
preserved and not find it. The rule was assumed by `MSP-01`'s rates, by `MSP-07`'s bracket and by every
retry in the document, and stated by none of them.

**This is a different failure from the tooling defects around it.** Those were measurements taken with
unchecked instruments. This was a *requirement credited with content it never had* — an attribution that
survived because everyone citing it was citing each other.

**Post-terminal retry, and what it replaced.** An earlier draft specified a *periodic* 1.0 s identification
retry, which contradicts the invariant whenever the deadline exceeds 1.0 s — and the deadline is `OPEN`, so
it cannot be assumed shorter. Waiting 1.0 s **after a transaction terminates** decouples the cadence from
whatever value the measurement eventually produces. `MSP-06` requires only that failed requests back off and
never retry-storm; it does not require exponential backoff, so a bounded flat interval satisfies it.

**No §3 row yet.** An earlier draft called the deadline a *derivation* and claimed audit 3's pass 3 must
pick it up. It is not one: the supporting argument — that the deadline must be strictly less than the poll
period or the one-outstanding invariant breaks — was **wrong**. A longer deadline makes polls *slip*,
degrading the achieved rate, which `MSP-08` already governs. **A wrong reason for a plausible rule is the
CR-42 defect**, and it survived two review rounds before being caught. §3 gains a row when a value is
written back, and not before.

### CR-53 — an FC that never answers, and no local indicator · **CLOSED in v1.8**

`FC-05` covers supported-but-old; `FC-14` covers a restart; `FAIL-01`'s 25 entries covered neither this nor
anything like it; and **the document contained no indicator requirement at all.** So the most likely
installation error — MSP not enabled on the chosen UART, `INST-01` step 2 — produced a bridge inert forever
with no way to say so, because `OSD-19` cannot reach an OSD backend that was never probed.

**`FC-15` is defined by failure to *complete*, not by silence — and its own validation case is what proved
the first framing wrong.** Drafted as `FC_NOT_RESPONDING` entered after a no-response interval, it fails on
the partial case: `MSP_API_VERSION` answers, `MSP_FC_VARIANT` never does, the FC **is** responding, the
timer never expires, and identification never finishes. The name was false in exactly that case, and the
*"partial identification response"* test would have imposed behaviour the requirement did not entail —
the audit-6 defect, arriving inside the repair for a different one. **Entry is now: no complete identity
within 2.0 s of the first request, with individual replies not resetting the clock.**

Recovery is automatic and needs no reboot: retry under `MSP-09`'s post-terminal rule, exit only into one of
`FC-08`'s outcomes, and **camera control stays suppressed until the whole qualification chain succeeds**.

**`FAIL-01`'s 26th entry names an observable, not a cause.** *"FC MSP identification unavailable or
incomplete"* — because `FC-15` cannot see whether MSP is disabled, on the wrong UART, at the wrong baud,
miswired, still booting, or partially implemented. All present identically. `INST-01` step 2 carries the
diagnosis; the failure list carries what is observed. And the entry arrives with `VAL-FAIL-60`, because
`FAIL-01` states that each listed failure *"has a corresponding acceptance test in `VAL-FAIL-*`"* and
`VAL-REV-11` rolls that up — a bare row would have falsified the clause it was added to.

**`IND-01` is keyed on an asserted diagnostic, not on total system state.** A two-row table of *"ordinary
operation"* versus `FC_NOT_IDENTIFIED` is **not closed**: there is a window of up to 2.0 s where
identification is in progress and the state has not been entered, and `PAIR` never runs identification at
all. Both would have fallen through, and audit 3's pass 2 would have regenerated a "closure" that was not
one. Keyed on assertion, both are simply solid and no third code is needed. The indicator is **on-board and
adds no installation wiring**, or reporting a fault would change `INST-01`'s four-solder-joint contract.

### What applying these two cost

**A new requirement family broke the definition set the same day it was unified.** `IND` was absent from
`defs.py`'s family list, so `verify.py` counted 249 while `orphan.py` counted 250 — the exact divergence
unification had just closed. It was visible within one run **because** the tools now share an authority;
before v1.8 it would have been two numbers in two reports that nobody diffed. **Adding a requirement family
is a change to the node set, not to the document alone.**

**And one report of that unification was wrong when it was made.** `orphan.py` was described as pointed at
the shared extractor when the edit applying it had aborted; the two tools agreeing at 247 was a coincidence
of two different regexes, not shared code. **Agreement between independent implementations is not evidence
that they share a definition** — which is the whole argument for `defs.py`, arrived at the hard way.

---

### CR-54 — two requirements depended on undefined or improperly normative internal concepts · **CLOSED in v1.8**

One defect class, **opposite repairs**. Each requirement leaned on an internal concept that no requirement
supplied: `LEARN-18` on a session lifetime nothing defined, `OSD-18` on a task model nothing defines. One is
fixed by **adding the missing semantics**, the other by **subtracting leaked architecture** — and telling
them apart is the whole content of this CR.

**`LEARN-18` — add.** It requires abandonment when *"the client disconnects, the page is closed, or the user
cancels"*, and says plainly that *"a closed browser leaves nobody to send a cancel"*. Nothing said how the
bridge knows. **`LEARN-19`** now supplies it: a client-held lease refreshed at **≤ 1.0 s**, abandoned after
**3.0 s** without a valid refresh, with transport unspecified — WebSocket, SSE or heartbeat all satisfy it.

*Why a lease and not a connection.* **No network-layer fact reports a closed page.** A phone stays
associated to the access point; a socket may outlive the tab. Only something the client must keep *doing*
distinguishes a live page from a closed one. An earlier draft of this repair proposed Wi-Fi station
association as the session signal — that was **wrong**, and the ELRS precedent cited for it was wrong too:
`esp_wifi_ap_get_sta_list()` appears once in 4.1.0, inside `wifi_GetClientRssi()`, reading **RSSI**.

**`OSD-18` — subtract.** It read *"the MSP task **shall** be able to interleave an OSD write between control
polls"* — a scheduling mechanism made normative, leaving the requirement dependent on a task model no
requirement defines. **The clause is demoted to non-normative rationale.** The obligation is the observable
one, changed content submitted within **100 ms**, and `VAL-FUNC-115` already measures exactly that at
`MSP-01`'s worst-case load. The dangling dependency closes with **nothing added**.

*Both earlier positions on this were wrong.* One review round asserted `OSD-18` did **not** require an
interleave; it did, in those words. The repair drafted in response would have added a cooperative-scheduling
obligation — importing more architecture to support architecture that should not have been there.
**Subtraction was available the whole time and neither side proposed it.**

### Delta-impact reread, and the control totals

Both endpoints changed normatively, so their surviving edges were re-read rather than assumed (CR-38):

| Edge | Verdict after re-reading |
| --- | --- |
| `VAL-FAIL-55` → `LEARN-18` | **holds, and is stronger.** Now runs four ways including explicit cancel — previously untested, though `LEARN-18` names it — and **measures both `LEARN-19` bounds** rather than only the abandonment outcomes |
| `VAL-FUNC-115` → `OSD-18` | **holds unchanged.** Only rationale was demoted; the 100 ms obligation the case measures is untouched |

`MSP-07` lost one PRS cross-reference with the demoted sentence and retains its others, so no edge moved.

**Movement against the pre-CR-54 control totals, all of it predicted:**

```
requirements   250 → 251     LEARN-19
cases          207 → 207     VAL-FAIL-55 extended, not added
relations      488 → 489     VAL-FAIL-55 → LEARN-19
strict orphans   0 →   0
dangling refs    0 →   0
vague terms      0 →   0
```

---

### CR-50 — a correct citation was overwritten from an unverified source · **CLOSED in v1.8**

Raised as *"the current Evidence silently departs from the v1.0 source it was compressed from"* — three
`MSP_RC` locations differed between `action-camera-bridge-prs-source-v1.0.md` and `PF-BF-23`, and a full
sweep of every `path:line` citation confirmed those three were **the only ones in the set that moved**.

**The direction of the defect is the reverse of the one raised.** Resolved from shallow checkouts at
`2025.12.5` and `8.0.1`, read out of the git object store:

| Location | v1.0 source | `PF-BF-23` as written | web fetch | **checkout** |
| --- | --- | --- | --- | --- |
| BF `msp_protocol.h` `MSP_RC` | `:176` ✅ | `:163` ❌ | `:155` ❌ | **`:176`** |
| BF `msp.c` `case MSP_RC:` | `:1317-1321` ✅ | `:1209-1213` ❌ | `:2545` ❌ | **`:1317-1321`** |
| INAV `fc_msp.c` `case MSP_RC:` | `:621-625` ✅ | `:588-592` ❌ | `:1102` ❌ | **`:621-625`** |
| INAV `msp_protocol.h` `MSP_RC` | not cited | `:215` ❌ | `:209` ❌ | **`:238`** |

**The v1.0 source was right on all three it carried. `PF-BF-23` was wrong on all four.** The `.c` ranges are
exact: `1317-1321` and `621-625` are the five-line blocks through `break;`. v1.7 did not correct a stale
citation — **it overwrote three correct ones and added a fourth**, from numbers supplied in review and
recorded while stating openly that they could not be verified.

**Four confident readings, one checkout, and only the checkout was right.** The v1.0 source, `PF-BF-23`,
a review that reported having *"checked the pinned raw sources directly"*, and a web fetch of the raw files
at the pinned tags — the last two wrong on all four locations. A markdown-rendered fetch with a model
counting lines is not a line-number oracle, and it is not one merely because it is your own.

**Nothing in the conclusion changes.** Both loop bodies were confirmed verbatim at the corrected lines:
`rcData[i]` over `rxRuntimeState.channelCount`, `rxGetChannelValue(i)` over `rxRuntimeConfig.channelCount`,
both `sbufWriteU16`, **no count field in either payload**. `CTRL-13`'s derivation stands. Only the line
numbers were wrong — which is exactly the failure mode Evidence's standing rule already predicted:
*"the part most likely to be regenerated from memory rather than read."*

**This CR carries three consequences, and the third is the one most easily lost.**

**1 · Immediate repair.** `PF-BF-23`'s four coordinates are corrected to the checkout-derived values, with a
version note recording what they were, what they are, and that v1.0's were right.

**2 · Prospective rule.** From v1.8 onward every new or changed citation is generated from a pinned checkout
with a line-numbering tool, **and the invocation is recorded**. Evidence's standing rule already required
*"opening the file at the pinned tag"* and already forbade *"silently re-pointing a citation"* —
**`PF-BF-23` broke both.** The rule was not missing; its **checkable half** was, so compliance was asserted
rather than verifiable.

**3 · The existing corpus is now known to be at risk, and its pre-release re-verification is load-bearing.**
This incident demonstrated that **hand-supplied wrong coordinates survive multiple deltas and multiple
reviews** — `PF-BF-23` passed three audits, a structural verifier and three review rounds with all four
numbers wrong. **Prospective hygiene does not close that.** It governs citations written from v1.8 onward
and says nothing about the 38 platform facts already recorded, every one of which was produced by a process
that was never named. Four `grep -n` runs settle one fact and establish nothing about the others.

So the pre-release re-verification of current Evidence citations is **not ceremonial and shall not be
treated as a formality**: it is the only control that covers the existing corpus, and this CR is the
demonstration that it is needed. Whether that sweep runs now or as the release step is a scheduling
decision; **treating it as already satisfied is not.**

### CR-51 — CR-39 was never allocated · **CLOSED in v1.8**

The change-record sequence runs 38 → 40. `CR-39` appears in no artifact and is referenced nowhere; a number
was skipped while CR-38 and CR-40 were written in the same pass.

**Recorded as never allocated; the gap is intentionally preserved after discovery.** The skip was
accidental — what is deliberate is the refusal to renumber. Every existing `CR-4x` reference would move, and
historical blocks are never edited in place.

`verify.py` gains a **change-record sequence check**. v1.7 added a *section*-numbering check to that same
verifier in response to CR-48, and a CR-numbering check is one door along from it; that door was not opened
until the gap was found by reading rather than by tooling.

---

### CR-55a — the delta introduced three body-only mentions while adjudicating body-only mentions

`spikegap2.py` moved from **9 to 12** between CR-49 and the end of v1.8, with no spike edited after CR-49.
The three are all on `VAL-SPIKE-01`, and all three were written by **CR-52's own new pass criterion**:

| Mention | In the criterion as | Ruling |
| --- | --- | --- |
| `SAFE-03` | *"every command in `SAFE-03`"* | **cross-reference** — the allowlist enumerates *what to measure*; the spike does not test the allowlist |
| `MSP-01` | *"at `MSP-01`'s worst supported load"* | **cross-reference** — a test condition, the same shape as `RF-04` on spike 7 |
| `REL-02` | *"the delegated-vagueness defect `REL-02` exists to prevent"* | **cross-reference** — rationale for the criterion's design, not a thing it exercises |

**No edges. The adjudicated total is 12, and `VAL-SPIKE-01`'s declared count stays at 41.**

**This is the fourth instance in one delta of the class CR-49 exists to fix**, after a hand-authored
`Verifies:` line, a script with unchecked bounds, and a wrapped declaration. The pattern is now clear and
worth naming: **prose that cites a requirement is written far more readily than a declaration that
constitutes a relation**, so any delta that adds spike prose adds candidate mentions — including a delta
whose subject is that exact problem. `spikegap2.py` is therefore a **standing** check rather than a
one-off repair, and its number is re-derived at the end of every delta, not only when spikes are edited.

**It also caught itself the right way round.** The movement was noticed because the pre-regeneration counts
were compared against recorded control totals rather than accepted as the new truth — 9 → 12 had no
semantic explanation until one was found, and the rule that produced it was *investigate unexplained
movement, do not adopt it*.

---

### CR-55 — audit 7 re-run whole over v1.8 · **two findings, both closed**

Audit 7 was re-run across the whole PRS rather than over the changed cluster, because v1.8 altered the FC
state model (CR-11), added timing and failure semantics (CR-52), added a state and an indicator (CR-53) and
a session lease (CR-54). Both findings sit at seams v1.8 created.

**Finding A — `OSD-19`'s terms were not stated as requiring positive establishment, and `FC-15` now depends
on that reading.** `OSD-19` computes `osdBackendEnabled` from three terms and calls them **exhaustive**.
`FC-15` then forbids OSD writes in `FC_NOT_IDENTIFIED`, a state in which **none of the three has been
determined at all** — the variant is unknown, the floor untested, the capability unprobed.

Both requirements are satisfiable together, but **only under an unstated reading**: that an unestablished
term is false. Under the opposite reading — each term true unless disproved — the backend would be enabled
before the FC is known, and `OSD-09`'s owned-slot initialisation, which `VAL-FUNC-119` calls *"otherwise
unconditional"*, would fire against an unidentified FC. **Two requirements agreeing only by interpretation is
what audit 7 exists to remove.** `OSD-19` now states it in one sentence; no fourth term is needed, and
`OSD-09`'s *"as soon as the FC backend is up"* is the gate that makes the two agree.

**Finding B — `LEARN-04`'s sample arithmetic reads as unconditional and is not.** *"250 ms at 20 Hz →
5 samples"* is a guarantee derived from the poll rate alone, written when a poll was assumed to return.
**`MSP-09` makes a timed-out transaction a first-class outcome**, and a timed-out poll yields **no sample** —
`MSP-07` forbids synthesizing one and discards an incomplete bracket.

This matters more than it would elsewhere because the same requirement records that **`CTRL-19`'s 250 ms and
`LEARN-06`'s five samples meet with no margin at all.** At zero margin, **one lost transaction leaves the
window a sample short.** The behaviour is safe — `LEARN-06` declines to characterise rather than proposing a
range from four, which is the correct direction of failure — but the condition was unstated, and a computing
sentence with an unstated condition is a measurement with an unstated method (CR-42). The condition is now
written down.

**Neither finding changed the graph:** 251 requirements, 207 cases, 489 relations before and after. Both were
clarifications of existing text, which is what a clean audit-7 finding usually looks like — the defect is in
what two requirements *jointly imply*, not in either one read alone.

**One observation about the pairing.** Both findings are the same shape as CR-54's: a requirement leaning on
something no requirement supplies. CR-54's were *dangling internal dependencies*, visible because a term had
no definition anywhere. These two were invisible by comparison, because the missing piece was not a term but
a **reading** — `OSD-19`'s terms had a definition, just not a polarity, and `LEARN-04`'s arithmetic had a
method, just not a precondition. **A cross-reference audit finds the first kind; only reading requirements
against each other finds the second.**

---

## 5i. v1.8 delta — applied 2026-08-27

**What triggered it.** Four findings and two gaps raised against v1.7, then four further review rounds
against the repairs themselves. Every round found something; several found defects of the exact class the
document under review had just diagnosed.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `FC-09` — an unsupported variant enters `UNSUPPORTED_FC_VARIANT` with **camera control suppressed** | specification | CR-11 |
| 2 | `FC-08` — four post-identification outcomes replace *"the two degraded states"* | specification | CR-11 |
| 3 | `FC-05`/`FC-06` — INAV gated on `major == 8` **and** `>= 8.0.1`; `SCOPE-02` ordering rule | specification | CR-11 |
| 4 | `VAL-FUNC-83`, `VAL-FUNC-119`, `VAL-FUNC-02`, `VAL-SPIKE-01` item 21 revised | validation | CR-11 |
| 5 | All 7 spike `Verifies:` lines re-derived from pass criteria; 22 mentions adjudicated | validation | CR-49 |
| 6 | Two `CAM-08` **misattributions** removed — the requirement does not state the rule cited | validation | CR-49 |
| 7 | `VAL-REV-14` (`SAFE-01`), `VAL-REV-15` (`CAM-01`), `VAL-FUNC-125` (`RES-06`) added | validation | CR-49 |
| 8 | `SCOPE-01` normalised to canonical form; `defs.py` becomes the single definition authority | tooling | CR-49 |
| 9 | `PF-BF-23` — all four line numbers corrected from pinned checkouts; version note added | evidence | CR-50 |
| 10 | Evidence standing rule — how a citation is produced, with the invocation recorded | evidence | CR-50 |
| 11 | CR-39 recorded as never allocated; `verify.py` gains a change-record sequence check | ledger | CR-51 |
| 12 | `MSP-09` — transaction deadline `OPEN`, post-terminal 1.0 s backoff, one-outstanding invariant | specification | CR-52 |
| 13 | `REL-02` — deadline row, with **measurement and closure named separately** | specification | CR-52 |
| 14 | `FC-15` — `FC_NOT_IDENTIFIED`, entered on failure to *complete* within 2.0 s | specification | CR-53 |
| 15 | `IND-01` — on-board indicator, mapping keyed on asserted diagnostic | specification | CR-53 |
| 16 | `FAIL-01` 26th entry, stated as an observable; `VAL-FAIL-60` added | validation | CR-53 |
| 17 | `LEARN-19` — 3.0 s client-held lease, refresh <= 1.0 s; `VAL-FAIL-55` extended to four ways | specification | CR-54 |
| 18 | `OSD-18` — the *"MSP task shall interleave"* clause demoted to non-normative rationale | specification | CR-54 |
| 19 | `OSD-19` — each term false until positively established; `LEARN-04` arithmetic given its precondition | specification | CR-55 |
| 20 | Compression rule §6.11 — a unit definition falsifies figures measured under the old unit | method | CR-49 |

**251 requirements, 207 validation cases, 39 platform facts, 489 relations.** Four requirements added, four
cases added, no requirement or case removed.

### What this delta cost to get right

**Every review round found a defect of the class the document under review had just diagnosed.** Round 2
found two decorative edges inside the repair for decorative edges. Round 3 found that CR-49's own reading
instruction contradicted CR-38. Rounds 4 and 5 found unresolved meta-options — *"a literal or a `REL-02`
row"* — in a document claiming to have none. Round 6 found the replacement claim was false too.

**Four separate instances of CR-49's own defect appeared while fixing it:** a hand-authored `Verifies:`
line, an extractor with unchecked block bounds, a wrapped declaration that silently dropped three edges, and
three new body-only mentions written by CR-52's pass criterion (CR-55a). The tooling now fails closed on the
second and third; the fourth is why `spikegap2.py` is a standing check.

**Three defects were found by neither the reviews nor the tooling, but by trying to apply the result:**
`PF-BF-23`'s four wrong citations (CR-50), the one-outstanding invariant that `MSP-01` never contained
(CR-52), and `OSD-19`'s terms having no stated polarity (CR-55). **Application is an audit**, and it is the
only one that runs against what the document actually says rather than against what everyone remembers it
saying.

---

### CR-56 — the corpus sweep's first find: `PF-BF-22` was wrong on every coordinate · **CLOSED**

CR-50's third consequence held that prospective citation hygiene does not cover the 38 platform facts
already recorded, and that the pre-release sweep is the only control that does. **The sweep found a second
fact with entirely wrong coordinates on its first run.**

| Claim | Recorded | Actual at `2025.12.5` | What the recorded line actually is |
| --- | --- | --- | --- |
| `BOXARM` … `permanentId = 0` | `msp_box.c:43-46` | **`:49`** | an `#include` block |
| `serializeBoxPermanentIdFn()` | `:161-168` | **`:169-176`** | the tail of an unrelated function |
| `serializeBoxReply()` | `:171-186` | **`:180-198`** | a bounds check |
| `packFlightModeFlags()` | `:377-395`, `:381-390` | **`:402-414`**, `:408-414` | a `bitArrayClr` branch |
| `MSP_BOXIDS` dispatch | `msp.c:2171-2175` | **`:2336-2340`** | **RTC code** |

**Six of six wrong, in a fact whose interlock `SETUP-09` depends on** to refuse a maintenance entry while
the aircraft is armed.

**The claim was correct throughout.** `BOXARM.permanentId` *is* 0, and `packFlightModeFlags()` *does*
traverse the same active-box iteration — its comment at the corrected `:408-414` reads *"index of active
boxId (matches sent permanentId and boxNames)"*, which is the correspondence the fact rests on. So this is a
**coordinate repair, not a content repair**, and it is the second demonstration that those are separate
controls.

**Same provenance as `PF-BF-23`, and that is the pattern.** Both facts were added to close a change record
held open for a missing platform fact — CR-24 and CR-44. Both were written from coordinates supplied in a
review message. **Both were recorded while stating openly that they could not be verified**, and both were
wrong on every coordinate. The discipline that produced the facts was sound; the step that recorded numbers
nobody had opened was not.

**What the sweep is, now that it has paid for itself.** 39 facts, **134 citations**, every one resolved
against the pinned commits `7348054f268f` (BF `2025.12.5`) and `ae47bcba0182` (INAV `8.0.1`), read from the
git object store rather than a working tree. The two totals are reported separately because **one fact may
carry several citations** — 39 alone would have hidden `PF-BF-22`'s six.

**Three tool defects were fixed before the sweep could be trusted**, each of which would have produced a
false result:

- platform resolved per line rather than per position, so a side-by-side `/* [BF] */ /* [INAV] */` fence
  reported a Betaflight path as an unresolved INAV one;
- platform context not carried across a line break, so `... ([BF]` ending one line and its citation
  beginning the next resolved to the wrong repo;
- blockquoted **version notes** parsed as live citations, so the sweep tried to verify the superseded
  coordinates a version note exists to record as wrong — a category error, and the reason live citations
  are now 134 rather than 141.

**A false FATAL is worse than no check**, because it teaches the reader to skim past the word.

### What remains unverified, stated plainly

The sweep establishes that **every citation resolves and every coordinate lands on content consistent with
its claim**, read row by row. It does **not** establish that each claim is the best reading of that code —
that is audit 5b's job and the pre-release re-verification's. `content_match` is adjudicated by reading the
extract the tool emits; the tool does not decide it, because a tool that scored semantics would have scored
`PF-BF-22` as passing for two deltas.

### CR-57 — a declined boot request outlived the boot that declined it · **CLOSED in v1.9**

**Raised from the implementation, not from an audit round.** Phase 1 implemented `BOOT-03` literally, as a
pure function over `(buttonHeld, resetReason, retainedRequest)`, and the literal reading has a hole.

`BOOT-06` invalidates the request on **one** path — the `ESP_RST_SW` path that actually consumes it. The
other two say nothing about the request's fate:

| Path | What the requirement said | What it left behind |
| --- | --- | --- |
| `ESP_RST_SW` + valid request | copy, invalidate, execute (`BOOT-06`) | nothing — correct |
| watchdog / panic / external reset | selects `RUN`, declines *"without inspecting it"* (`BOOT-07`) | **a still-valid request** |
| Setup button held | `SETUP`, *"depends on nothing persistent"* (`BOOT-04`) | **a still-valid request** |

Both survivors fire the same way: the next `ESP_RST_SW` consumes them. And `BOOT-06` itself names a routine
source of one — *"completing a firmware update restarts the same way (`UPD-05`)"*. The second path is the
worse of the two, because the restart that exits Setup Mode is an ordinary `ESP_RST_SW`, so a `PAIR` request
the button overrode fires as soon as the user leaves Setup.

**Power-on and brownout were never affected.** Retained RAM does not survive either, so those two close
themselves physically. The three that do carry a request forward — watchdog, panic, external reset — are
exactly the three that indicate something went wrong, which is when an unattended maintenance boot is least
wanted.

**The validation did not cover it, and looked as though it did.** `VAL-FUNC-35` tests replay, but only of a
*consumed* request. `VAL-FUNC-33` and `VAL-FAIL-40` test the held-button override, but only that it selects
`SETUP` — neither follows the retained request past the boot in which it was overridden. Every case asserted
**immediate selection**; none asserted a **lifetime property**.

**The failure catalogue was stronger than the requirements it indexes.** `FAIL-01` has listed *"a retained
boot request surviving into an unrelated software restart"* since v1.2, and `VAL-REV-11` checks that every
`FAIL-01` entry has a `VAL-FAIL-*` case — which it had, in `VAL-FAIL-39` (corrupt retained memory) and
`VAL-FAIL-37` (power removed during `PAIR`). Coverage was satisfied by cases that address a *different*
survival path from the one the entry names. **A coverage check that counts cases per hazard cannot see that
the cases miss the hazard**, and that is the reusable finding here, not the boot defect.

**The repair is one invariant, stated once.** `BOOT-03` gains it — *after boot selection, no retained request
that was not selected for execution remains valid* — and `BOOT-04`, `BOOT-06` and `BOOT-07` each point at it
rather than restating it. Retiring is specified as a **write and not a read**, which is what preserves
`BOOT-04`'s independence from persistent state: clearing the validity marker requires knowing neither what
was requested nor whether anything was. `BOOT-05`'s NVS fallback is allowed to read the marker first to avoid
an erase per boot, since reading the marker is not interpreting the request.

**`VAL-FAIL-61` and `VAL-FAIL-62` test the lifetime, not the selection.** Each drives a *sequence* — place a
valid `PAIR` request, force the boot, then perform an unrelated restart and assert `RUN` — because a
single-boot assertion is what missed this for seven deltas.

| | before | after |
| --- | --- | --- |
| requirements | 251 | 251 |
| validation cases | 207 | **209** |
| relations | 489 | **495** |
| platform facts | 39 | 39 |

Four requirements amended in place, no ID added or removed. `verify.py`, `orphan.py`, `v16_edges.py` and
`spikegap2.py` re-run clean: 0 orphans, 0 dangling refs, 0 vague terms, body-only mentions unchanged at 12.

---

## 5j. v1.9 delta — applied 2026-08-27

**What triggered it.** One finding, raised by implementing `BOOT-03` rather than by reviewing it.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `BOOT-03` — selection retires every request; retiring specified as a write, not a read | specification | CR-57 |
| 2 | `BOOT-04` — the held-button path retires the request **blindly** | specification | CR-57 |
| 3 | `BOOT-06` — recast as `BOOT-03`'s obligation on the one path that also reads the request | specification | CR-57 |
| 4 | `BOOT-07` — declining and retiring separated; the three reset reasons that carry a request forward named | specification | CR-57 |
| 5 | `VAL-FAIL-61`, `VAL-FAIL-62` added — replay **sequences**, not single-boot selection | validation | CR-57 |

**251 requirements, 209 validation cases, 39 platform facts, 495 relations.** No requirement added or
removed; four amended in place.

### What this delta says about the method

**Seven audit rounds did not find it, and one implementation did.** The audits check the graph — orphans,
dangling refs, coverage per hazard, entailment between requirements. This defect is invisible to all four,
because every requirement involved was well-formed, referenced, covered and internally consistent. It is
only visible from the question an implementer is forced to ask and a reader is not: *after this function
returns, what is left in that struct?*

`CR-52` remains the only open change record, and remains release-blocking.

### CR-58 — `epoch` was carried in every sample and read by nothing · **CLOSED in v1.10**

`ControlSample` has carried `{values, valid, epoch}` since the seam was drawn, and `RCV-14` makes the
epoch meaningful: every re-qualification is a re-baseline boundary. But `RCV-14` speaks only to **level
control reconciliation**, and it was the only requirement that mentioned the field. Nothing said what an
epoch change meant to the two consumers whose predicates are **multi-sample claims**.

| Predicate | Requirement | Spans a discontinuity? |
| --- | --- | --- |
| median of 5 qualified samples | `LEARN-06` | **yes, before this** |
| moved for 2 consecutive qualified samples | `LEARN-06` | **yes** |
| stable over 5 consecutive qualified samples | `LEARN-06` | **yes** |
| four confirmed transitions | `SETUP-12` | **yes** |

So `qualified, qualified, re-qualification, qualified, qualified, qualified` satisfied a five-sample
predicate over observations that never formed one continuous stream. `SETUP-13` resets recognition on
RC-validity **loss**, which catches the case only when the recogniser happens to observe the unqualified
sample itself — the epoch is how the adapter *reports* the discontinuity, and it is the robust signal.

**`RCV-20` states the rule once** — all multi-sample interpretations are contained within one epoch;
transient state derived from prior samples is discarded before the new epoch's first qualified sample is
consumed; **time bounds belonging to the enclosing operation are not restarted.** `SETUP-13`, `LEARN-06`
and `LEARN-17` specialise it rather than restating it.

**Discarding the evidence is not abandoning the operation**, and the existing failure semantics are what
decided that. `VAL-FAIL-56` already fixes the outcome of validity loss during learning as *reaches the
timeout, changes no configuration* — not *cancels*. Treating an epoch change as a cancel would have
strengthened the contract beyond what any requirement asked and made a short transient dropout destroy a
user operation. The unconfirmed proposal is the one piece of transient state that is discarded even
though it looks like a result: committing it would commit a mapping from a stream that no longer exists.

**The unrestarted deadline is the load-bearing half.** A `LEARN-17` timeout that restarted on
re-qualification would make a bounded acquisition unbounded under repeated link interruptions — the
failure mode is a setup page that never terminates on a marginal link.

**Implementing it found a second defect.** `LEARN-17`'s timeout was only reachable from the *acquiring*
state, so an operation whose input never qualified sat in baselining forever. `VAL-FAIL-57` runs exactly
that case — learning attempted with the transmitter off — and expects an end. The timeout now bounds the
operation rather than its second half. **This is the second time in two deltas that implementing a
requirement literally found a hole an audit did not** (`CR-57`).

| | before | after |
| --- | --- | --- |
| requirements | 251 | **252** |
| validation cases | 209 | **211** |
| relations | 495 | **501** |

---

## 5k. v1.10 delta — applied 2026-08-27

**What triggered it.** A review finding that neither new phase-1 control consumer read `ControlSample.epoch`.

| # | Change | Kind | CR |
| --- | --- | --- | --- |
| 1 | `RCV-20` — the qualification epoch bounds every multi-sample interpretation | specification | CR-58 |
| 2 | `SETUP-13` — an epoch change has the same effect as RC-validity loss; the window does not restart | specification | CR-58 |
| 3 | `LEARN-06` — baseline, runs, windows, candidate state and any unconfirmed proposal do not span epochs | specification | CR-58 |
| 4 | `LEARN-17` — the acquisition timeout is anchored to operation start | specification | CR-58 |
| 5 | `VAL-FUNC-126`, `VAL-FUNC-127` added — the gesture and the learner across a discontinuity | validation | CR-58 |

**252 requirements, 211 validation cases, 39 platform facts, 501 relations.** One requirement added, three
amended in place; none removed.

`CR-52` remains the only open change record, and remains release-blocking.

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
