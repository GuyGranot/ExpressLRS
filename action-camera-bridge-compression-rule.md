# Specification Compression Rule

**Status:** Canonical method. Applied once, to PRS v1.0 → the four-artifact set, 2026-08-26.
**Amended** by CR-01 (classification by contractual role) and by audit 5 (conformance-input), both arising
from that application; then by CR-07, CR-08 and CR-09 — audit 5's reverse leg, audit 6
(validation-entailment) and audit 7 (internal consistency) — arising from two independent external audits of
the result. **Every amendment so far was found by reading the output, not by reasoning about the method.**

---

## 1. Goal

The compression pass shall make the specification **shorter, easier to navigate, and easier for both
humans and coding/review agents to interpret**, without changing product behaviour, safety properties,
supported-platform assumptions, release scope, or acceptance criteria.

**Compression is a representation change, not a requirements change.**

The resulting document set shall optimise for:

- one authoritative statement of each requirement;
- clear separation between normative behaviour, platform evidence/rationale, and verification;
- minimal duplication and cross-section paraphrasing;
- preservation of exact state-machine, timing, threshold, persistence, safety and failure semantics
  wherever two otherwise reasonable implementations could behave differently;
- direct traceability from every load-bearing requirement to its supporting platform fact and validation
  coverage.

**The compression pass shall not simplify behaviour merely because the rationale for that behaviour is
lengthy.**

---

## 2. The four-artifact set

| # | Artifact | Holds |
| --- | --- | --- |
| 1 | **PRS** | the normative contract |
| 2 | **Platform Evidence** | source facts, citations, derivations, rationale |
| 3 | **Validation Specification** | tests, reviews, spikes |
| 4 | **Compression / Traceability Ledger** | provenance of every source statement and every transformation |

**A fifth artifact — this one — holds the method.** It is not part of the compressed set; it is what the set
was produced under.

**The ledger is not optional.** The claim *"no requirement changed"* is otherwise an assertion by whoever
performed the rewrite. The ledger makes semantic equivalence mechanically reviewable, and it is the
artifact that surfaced 53 unverified requirements on first application.

---

## 3. Classification

Every existing statement shall be classified into exactly **one** primary role.

| Role | Destination | Rule |
| --- | --- | --- |
| Normative requirement | PRS | defines externally observable behaviour, a safety invariant, an algorithmic semantic, supported scope, or an implementation constraint necessary for conformance |
| **Normative platform fact** | **PRS** | **a platform-derived value that directly constrains what a conforming implementation sends, parses, indexes, orders, masks or interprets** |
| Platform fact / source evidence | Platform Evidence | establishes *why* a normative requirement is correct for a pinned baseline |
| Rationale | Evidence, or a one- to two-sentence PRS note | explains a non-obvious decision but does not independently constrain implementation |
| Verification procedure | Validation | describes how conformity is measured or demonstrated |
| Example | keep only where useful | illustrates a rule; shall not introduce behaviour |
| Duplicate / paraphrase | remove | replaced by a reference to the authoritative requirement |

### 3.1 CR-01 — classification is by contractual role, not epistemic origin

> **Platform-derived statements are classified by their role in conformance, not by the fact that they
> came from source code.**
>
> A platform fact that **directly constrains what a conforming implementation sends, parses, indexes,
> orders, masks, or interprets remains normative in the PRS.** Its citation, derivation and justification
> live in Evidence.
>
> A platform fact used **only to justify why a normative requirement exists** may move entirely to
> Evidence.

```text
Normative platform facts            Evidence-only platform facts
────────────────────────            ────────────────────────────
MSP_RTC query 247, set 246          why BF rcData can lead the failsafe flag
MSP_FC_VERSION wire encoding        historical reason a feature appeared
INAV custom element = 28 bytes      why a particular guard is sufficient
ARMING_DISABLED_RC_LINK = bit 18    source-control history
BF custom-text subtype base = 7     comments contradicting defaults
STATUS₁ → RC → STATUS₂ ordering
```

**The decisive test overrides every category heuristic above:**

> **If deleting the fact enlarges or changes the set of conforming implementations, it is normative.**

**Why this amendment exists.** The original table sent every platform fact to Evidence. Applied
literally, it would have moved `MSP_RTC = 247/246` out of the normative document — and a conforming
implementation using a single shared constant **sets the flight controller's clock.** The rule
contradicted its own deletion test, and the table was the half that was wrong.

### 3.2 The three-way test

- If removing the sentence could allow **two conforming implementations to behave differently** → PRS.
- If removing it removes only the **proof that the requirement is valid** → Platform Evidence.
- If removing it removes only the **instructions for demonstrating compliance** → Validation.

---

## 4. Normative compression rule

A requirement may be shortened **only if** the compressed form preserves all behaviourally significant
dimensions of the original, including where applicable: preconditions and applicability; state and mode;
input qualification; transitions and ordering; timing and numeric thresholds; persistence/reboot
semantics; failure behaviour and fallback; explicit prohibitions; platform differences; release scope;
user confirmation or authorization requirements; and interaction with other requirements.

Explanatory derivation may be removed from the PRS **once these properties are expressed directly.**

### 4.1 One authoritative statement

Each requirement shall have one canonical normative definition. Other sections shall **reference its ID**
rather than restating it. This applies especially to requirements repeated across the main body, scope
list, acceptance matrix, failure matrix and spikes.

### 4.2 Requirement IDs

Behaviourally significant requirements receive stable IDs grouped by domain. **IDs are never reused and
never renumbered; a retired ID stays retired.** Platform-specific requirements carry a `[BF]` / `[INAV]`
marker in the text rather than in the ID, so a requirement that later diverges by platform does not force
a renumbering.

Every platform-dependent normative requirement forms a three-way chain:

```
platform fact  →  normative requirement  →  validation case
   PF-BF-10           RCV-03                  VAL-FUNC-24
```

**No load-bearing requirement should exist without this chain** unless it is explicitly a product
decision rather than a platform-derived behaviour.

### 4.3 Delegated parameters

A compressed requirement containing `sufficient`, `reasonable`, `adequate`, `appropriate`, `bounded`,
`stable` or `acceptable` **shall be rejected** unless the value is deliberately delegated to a **named
validation process that writes back a concrete result** — or unless the word is a *defined term* with its
defining number within sight of it.

Delegated parameters shall be listed in a single registry with an explicit status:

```
OPEN         not yet knowable; names who will know it; BLOCKS RELEASE
PROVISIONAL  a test target and implementation input now; becomes the release
             value only after its named validation writes back
CONDITIONAL  names the check that selects between a design and its fallback
```

> **No release may claim a feature whose normative parameter remains `OPEN`.**

**`OPEN` does not license an implementer to choose something sensible.**

---

## 5. Procedure

**Step 1 — freeze the source.** The current audited specification is the semantic baseline, snapshotted
with a content hash. No requirement changes, new assumptions, threshold changes or architecture changes
are permitted during compression.

**Step 2 — extract normative statements**, requirement-first rather than paragraph-by-paragraph. For each
section identify: MUST/SHALL, MUST NOT/SHALL NOT, state-machine transitions, numeric bounds, ordering
constraints, failure semantics, scope/gating, explicit implementation constraints. **Write the concise
canonical requirement first.**

**Step 3 — separate proof from contract.** Move source walkthroughs, historical explanation, platform-code
excerpts, why-naive-implementations-fail narratives and version investigations to Evidence — **subject to
CR-01.** The PRS retains a one- or two-sentence rationale where losing it would make a future engineer
likely to "simplify" the requirement incorrectly.

**Step 4 — separate verification from contract.** Move test matrices, hardware experiments, failure
injection sequences, parameter sweeps, expected measurements and spikes to Validation. The PRS retains only
the **required result**.

**Step 5 — remove duplicate prose.** Replace repeated explanations with requirement references.

**Step 6 — preserve exceptional behaviour explicitly.** Compression shall not generalise away platform
differences. The pattern is one common semantic plus per-platform mechanisms:

```
common:    RX loss shall not manufacture a camera-control transition
[BF]       guarded commit + requalification
[INAV]     last-good RC retention + startup qualification
```

**Step 7 — log what you found but must not fix.** Extraction is a more thorough read than any prior audit
and **will** surface defects. Each goes to the ledger's change-request log as a candidate for a subsequent
normative delta. **Silent repair under the guise of compression is the failure this step exists to
prevent.**

---

## 6. Audits

The compression is complete only after passing **seven**. Audits 1–5 run *source → destination* and answer
"was anything lost?". **Audits 6 and 7 answer two questions the first five cannot**, and both were added
after external review found defects that all five had passed:

```
1-5  did the destination faithfully receive the source?
 6   is every obligation the tests impose actually stated in the contract?
 7   is the contract consistent with itself?
```

A specification that perfectly preserved a contradiction passes 1–5. One did.

### 6.1 Requirement-equivalence audit

For every normative statement in the source, record exactly one of: retained as ID · merged into ID ·
moved to Evidence as non-normative · moved to Validation as test-only · deleted as exact duplication ·
**removed by reviewed product decision**.

**The last disposition is not optional bookkeeping.** Without it the only way to record a removal is as
duplication, which asserts something false — or not at all, which makes a deliberate decision look exactly
like an oversight. **A removal shall name the change request that decided it, and shall be recorded against
the source statement**, not only in the change log, so the source inventory stays complete.

**There shall be no unexplained deletion.** This is the strongest protection against losing a requirement
while shortening.

### 6.2 Reverse audit

Read the compressed PRS independently and ask: **could an implementation satisfy this document while
violating behaviour required by the original?** This catches cases where every source sentence was
technically accounted for but several were merged into wording that is too broad.

### 6.3 Boundary-value audit

For every requirement involving state, timing or values, verify the compressed text still specifies the
boundaries, and that each value has **exactly one canonical home**. Run the vague-term scan of §4.3
mechanically.

### 6.4 Traceability audit

```
0 orphan requirements        (every requirement has ≥ 1 validation case)
0 orphan tests               (every case names ≥ 1 requirement)
0 unresolved platform assertions
0 duplicate normative definitions
0 dangling references, in any artifact
```

The requirement↔test relation is **many-to-many**. The audit asserts non-emptiness in both directions,
**not a bijection** — a rule read as requiring 1:1 causes tests to be split artificially or requirements
to be merged wrongly.

### 6.5 Conformance-input audit *(added after first application)*

After the normative PRS is extracted, enumerate every literal an implementation consumes:

```
command IDs · subtypes · bit numbers · payload lengths and layouts · state values
timings · thresholds · ordering rules · reset reasons · mode-transition rules
```

Then ask:

> **Can an implementation be written correctly without retrieving any additional semantic fact from
> Evidence?**

The answer shall be **yes**. **Evidence explains and proves the contract; it shall never contain a value
an implementer must discover before they can implement correctly.**

This audit catches CR-01-class defects **directly**, rather than indirectly through the deletion test.

#### 6.5b CR-07 — audit 5 runs in both directions

The forward leg above asks *is every conformance input present in the PRS?* It passed on every input it enumerated and still
missed two defects, because it can only check literals someone thought to enumerate. **The reverse leg is
mechanical and complete:**

> **Evidence shall contain no statement that constrains a conforming implementation.** Scan Evidence for
> deontic verbs — `shall`, `shall not`, `must`, `is required to`. Every hit shall be either a **verbatim
> restatement of a cited PRS requirement**, or rationale carrying no obligation of its own. **A `shall` in
> Evidence with no PRS home is a lost requirement**, not a stylistic matter.

This is what CR-01 was raised to prevent, and it recurred anyway: two body-prose `shall` statements from
source §20 were dispositioned `E` and left the contract entirely. The deletion test would have caught both;
nobody applied it to them, which is precisely why the check must be mechanical rather than a matter of
attention.

### 6.6 Validation-entailment audit *(CR-08)*

The traceability audit proves `requirement → ≥ 1 test` and `test → ≥ 1 requirement`. **It does not prove that
what the test requires is entailed by the requirements it cites** — and that gap is where five lost
requirements hid. A case citing three requirements is not an orphan, so nothing flagged it, even though none
of the three said what the case checked.

**Run it in both directions.**

> **Forward — no hidden requirements.** For every validation case, strip the stimulus, the test mechanics and
> the measurement procedure. Every remaining expected system behaviour, prohibition, state transition,
> recovery action, persistence rule or documentation obligation **shall be directly entailed by one or more
> cited PRS requirements.** Anything left over is a requirement hiding in a test. It shall be **restored to
> the PRS**, or **removed as a separately reviewed requirement change** — never left where it is.
>
> **Reverse — no decorative citations.** Every requirement a case cites **shall actually be exercised by that
> case.** A citation is not coverage. A case citing a requirement it does not test inflates the traceability
> audit and hides an untested requirement behind a green count.

**The classification trap this exists to catch.** Where the source makes an acceptance matrix a release
condition — *"a release shall not claim platform support until §24 passes"* — **its rows are normative by
reference**, whatever they look like. A row is safe to classify `V` only when the behaviour it expects is
stated somewhere else. Where the row is the *only* statement of that behaviour, it is `R+V`, and classifying
the section wholesale is how 145 rows get dispositioned in one line and five requirements disappear.

### 6.7 Internal-consistency audit *(CR-09)*

Audits 1–6 all run between documents. **None asks whether the resulting contract contradicts itself**, and a
compression that faithfully preserves a contradiction passes every one of them.

> Read the PRS as an implementation contract, with no reference to the source. For each requirement ask:
> **is there another requirement in this document I cannot simultaneously satisfy?** Check in particular:
> closed allowlists against every other requirement that mandates traffic; unconditional obligations against
> prohibitions stated elsewhere; `may` permissions whose non-exercise reaches an undefined state; algorithms
> defined only for their steady state and not for their initial one; and bounds with no stated floor.

**This audit finds defects the compression did not cause.** Its output is a change-request list against the
*source specification*, not a repair to the compression — and it belongs to the method precisely because
extraction is the most thorough read the specification will ever get. Nine inherited contradictions surfaced
this way, having survived every audit v1.0 ever had.

---

### 6.8 What a delta shall regenerate *(CR-23, added after the v1.3 re-audit)*

**A ledger contains two kinds of block and they obey opposite rules.**

**Historical blocks** — dispositions, change-request logs, delta tables — record how the current state was
reached and are **never edited in place**. A correction carries its prior value and names the change request
that made it.

**Measured blocks** — boundary-value tables, traceability counts, conformance-input inventories, entailment
figures — state the condition of the artifact *now*. **A delta is not complete until every measured block
has been regenerated from the current artifacts, and each shall carry the date it was regenerated.**

Carrying a measured block forward is not conservatism; it publishes a false measurement while looking like
diligence. The v1.2 freeze declared seven audits clean on four blocks that had not been re-run, and the
figures were wrong in both directions — a threshold that had been tightened still shown as a range, and a
delegated-parameter count that had grown still shown as 4.

**Re-enumerate; do not increment.** A carried-forward total cannot be corrected by arithmetic, because a
delta replaces inputs as well as adding them.

**And regenerate the whole block, by a method that covers the block's stated contract** *(added v1.4,
CR-30 / CR-31)*. Two failures found one delta after the rule above was written, both inside blocks the
delta itself reported as regenerated:

- A boundary table whose contract reads *"every numeric value, threshold, **count** and boundary"* was
  regenerated by extracting numeric literals carrying units. Closure constraints — *"there are three and no
  others"* — carry no unit, so the method could not see them, and one of the six it missed had been created
  by that same delta.
- An inventory's headline was re-enumerated while the category breakdown beneath it was left untouched.
  The two had not agreed in any version, which nobody had noticed because **nothing ever added the rows up**.

**A measured block shall be internally checkable.** Where it states a total and a breakdown, the breakdown
shall sum to the total, and the check shall be part of producing it. Where it states a count of things, the
things shall be enumerable from what is published — a bare number is a claim, not a measurement.

**A regeneration that covers part of a block is not a regeneration of that block.** It is a partial pass,
and a delta that reports it as complete has published a false measurement by a second route.

### 6.9 What a mechanical screen may and may not conclude *(added after the v1.3 re-audit; completed v1.4)*

Audit 6's reverse leg was once screened by lexical overlap between each case and each requirement it cites.
**The figures below are dated snapshots of one artifact generation and are not comparable to each other or to
the current edge count** — each was taken under a different counting unit, which is the defect CR-45 records.

```
v1.3   116 flagged of 413 counted;  16 read;   3 defects reported
v1.4   110 flagged of 414 counted; 110 read;  17 defects total after repair
v1.5   the unflagged remainder read for the first time; 4 of 5 survivors were never flagged
```

**Within a single generation the comparison holds, and that is all it needs to hold for:** on the v1.5 run,
the screen's flagged set contained one of the five surviving defects and the unflagged set contained four.

**A screen orders the reading. It does not perform the audit, and its output shall not be published as a
finding count.** Entailment is a semantic relation and low lexical overlap is routine in a correct citation —
`VAL-FUNC-01`/`FC-03` shares no content words and is exactly entailed. Where a screen is used, the record
shall state how many flagged pairs were actually read, and **shall not imply the remainder were cleared.**

**And the reading shall be finished before the audit is reported.** The two figures above are the argument:
the same screen over substantially the same artifacts yielded three defects when a seventh of it had been
read and seventeen when all of it had. **A partial read does not produce a smaller version of the true
result — it produces an unrelated number**, because which defects it finds depends on which pairs happened
to sort to the top. Two of the three found by the partial run were also *repaired* partially, each leaving an
identical defect on the same validation case (CR-32).

**The reading set is every current relation, not every flagged relation** *(amended v1.5, CR-38)*. This
follows from the two sentences above it and had to be stated anyway, because the delta that wrote them read
only what the screen selected and called the audit complete.

```
the screen may decide     the order in which relations are read
the screen may not decide which relations are read
```

The measurement that settles it, taken on one artifact generation and quoted in that generation's unit: the
screen's 110 flagged relations contained **one** of five surviving defects and the 293 unflagged contained
**four**. Lexical overlap is not weakly correlated with
entailment failure here; it is **anti**-correlated, because the citations most likely to be decorative are
the ones whose subject matter matches — which is exactly what produces a high overlap score.

**Two further rules fall out of the same pass, and neither is about screens.**

**A citation is a claim about a requirement's current text.** Moving normative content between requirements
invalidates every citation that pointed to where it used to be, and those citations still name a real ID, so
nothing mechanical reports them. A delta that relocates a clause **shall re-read every relation citing either
end**. In practice the same delta that moved a clause left a case citing its former home (CR-38).

**A requirement about stored state needs a case that reads the store.** Where a requirement governs what is
*persisted* rather than what is *observed*, a behavioural case can pass against an implementation that
violates it, because the violation is masked by the very layer under test — and it surfaces later, when a
default changes. `CTRL-17` was covered by one case for four deltas on exactly this basis.

**And the reading itself has a false-negative rate.** One of 110 previously-read relations was misjudged: a
phrase in the case matched a phrase in the requirement and the claims were different. **A relation read once
is evidence, not proof**; where a delta repairs the surrounding text, prior verdicts are re-derived rather
than inherited.

### 6.9.1 A relation count is a measurement *(added v1.6, CR-45)*

CR-31 required a reported total to have a stated unit and enumerable membership. That was written for
conformance inputs and not applied to relations, because a relation count looks like a count of visible
things. It is not: **three consecutive deltas reported relation totals produced by three different implicit
units**, and none of the three was written down.

**One relation is one explicit `(case ID, requirement ID)` pair**, enumerated from the case's `Verifies`
cell or a spike's `**Verifies:**` line, and from nowhere else.

- **No semantic ellipsis in the canonical list.** `X-01`…`X-05` and `X-*` are prohibited, because a counter
  either expands them — silently deciding what the author meant — or tokenises them, silently counting five
  relations as two. One range shorthand in one cell understated a total by three for four deltas.
- **The counter refuses rather than guesses.** Where shorthand is present it emits no number at all. A
  counting tool that degrades gracefully produces a plausible wrong total, which is worse than no total.
- **Every case contributes edges or is explicitly outside the audit.** Prose-form cases with no `Verifies`
  line were described as "read directly" for two deltas, which was true of their text and produced nothing
  anyone could check. Seven such cases held 58 relations that no count included.

**The general form, and it is the third time this rule has been learned in a different costume:** a measured
block states its unit, publishes its membership, and is checkable against both. CR-31 learned it for
inventories, CR-37 for group membership, CR-45 for graph edges. **Each time it was rediscovered rather than
applied, because the previous statement named the artifact it was about instead of the property.**

### 6.10 The third audit-5 outcome *(CR-24)*

Audit 5 was specified with two failure modes: an input reachable only from Evidence, and an input present in
neither document. **Only the first had ever occurred, and the second was treated as theoretical.** The v1.3
re-enumeration produced one: a platform constant a requirement's own wording depends on, recorded in neither
artifact.

**An `absent from both` result shall be left open rather than filled in from memory.** A platform constant
written down without reading it at the pinned tag is indistinguishable from one that was verified, which is
the failure mode the whole citation discipline exists to prevent.

---

### 6.11 A unit definition can falsify claims it was never applied to

**Added in v1.8, from CR-49.**

v1.6 defined the relation unit — *one relation is one explicit `(case ID, requirement ID)` pair* — to repair
a relation **count** that had been produced by an undefined token-counting method (CR-45). It did that
correctly. What nobody checked is that **other published figures had been measured under the loose reading
the new unit replaced.**

Audit 4's *"requirements with no validation case: 0"* was one of them. Under the loose reading — any mention
of a requirement anywhere in a validation case counts — it was true. Under the strict unit it was **false by
five**, and it stayed on the published header for two deltas after the unit that falsified it was adopted.

**The rule.** When a delta defines or narrows a **unit of measurement**, every figure previously measured
under the old unit is **withdrawn and re-derived**, not carried forward. A unit definition is not a local
repair to the one figure that motivated it; it silently re-scopes every claim expressed in that unit.

**The failure is invisible by construction**, which is why it needs a rule rather than attention. Nothing
about the old figure changes when the unit changes — the number still sums, the block still regenerates, the
verifier still passes. Only the *meaning* of the number moved, and no tool reads meaning.

**What this obliges at each delta.** Before regenerating measured blocks, list the units that changed in the
delta, and for each one enumerate every published figure expressed in it. Figures on that list are marked
withdrawn until re-derived from disk under the new unit. **A figure that is merely still arithmetically
consistent is not evidence** that it survived the change.

## 7. Final acceptance

> **Compression succeeds only when the shorter specification admits exactly the same conforming
> implementations as the audited source specification.**

**This criterion applies to the compression boundary alone.** Deltas applied afterwards deliberately change
the conforming set, and **the relationship between the source and the current baseline shall be stated as
what it is** — the boundary result, whatever it was, plus an itemised list of subsequent changes — rather
than re-asserted as equivalence. A later baseline being *better* than the source is an engineering
judgement, not a property this criterion certifies.

**And a failure at the boundary is permanent.** A later delta may repair every loss it caused and still not
satisfy this criterion, because the criterion is about what the compression did, not about what the current
baseline contains. **"Not met, then met" is not an available outcome**; the only honest forms are *met* and
*not met, with the repairs itemised* (v1.3, CR-22).

A reduction in words is valuable **only** if it preserves that set. If a shortened statement admits
behaviour previously prohibited, prohibits behaviour previously permitted, removes a platform distinction,
weakens a safety invariant, or makes a formerly testable requirement subjective, **the compression is
incorrect.**

> **No information is discarded solely to shorten the PRS.** Material necessary to justify or verify the
> contract is relocated to Evidence or Validation; only genuine duplication is deleted.

**Measure the right thing.** The metric is the **normative reading path** — what an implementer must read
to know what to build. The *total* across four artifacts will usually grow, because IDs, traceability
columns and the ledger are new text. That is the expected outcome, not a failure.
