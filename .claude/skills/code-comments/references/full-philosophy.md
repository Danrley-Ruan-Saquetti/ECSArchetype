# Code Comments: Principles and Criteria

## The central principle

Code expresses what happens; the comment exists only for what does not fit in the syntax. That is the dividing line, and it is not a matter of style or taste — it is a matter of what the language is able to represent.

Name, type, signature, structure and test are ways of expressing behaviour. All of them are checked by some tool: compiler, interpreter, type checker, linter, test suite. When the information fits in one of these forms, putting it in a comment is a worse choice, because it trades a checked declaration for a sentence nobody checks.

The problem is that there is a whole class of information that no language can represent:

- **Why this solution and not the obvious alternative.** The code records the path chosen; it is structurally unable to record the rejected paths.
- **Where a number came from.** A measured limit, a calibrated value, a constant derived from hardware or from an external system.
- **What ties this part to something distant.** Invariants between parts of the system that no local type can express.
- **What is deliberate and looks like a mistake.** Convention violations made on purpose, workarounds for third-party bugs.

No renaming solves these cases, because there is nothing to rename. The information simply does not exist in the file until someone writes it.

Hence the operational statement of the principle: **every comment is either a failure of the code to express itself, or information the language has no way to carry.** The first case is fixed by refactoring. The second is solved by commenting. Confusing the two is what produces both polluted code and mute code.

## The verification asymmetry

The comment is the only artifact in the repository with no mechanical check, and that is what makes it dangerous. Everything else breaks when it diverges from reality: a wrong type does not compile, an outdated test fails, a call with the wrong arity blows up. The comment does not break. It lies silently, indefinitely, with the same look of authority it had on the day it was true.

This creates a practical cost asymmetry. A correct comment saves the reader a few minutes. An outdated comment costs hours, because the reader trusts it, starts from a false premise and only finds the mistake after debugging the wrong symptom. The expected value of a comment therefore depends heavily on the probability that it stays true.

And that probability varies a lot with the kind of comment:

| Kind of information | Coupled to what | Risk of rotting |
| --- | --- | --- |
| Paraphrase of the code line | Every character of the code | Very high |
| Signature description | The signature | High |
| Local invariant | The nearby implementation | Medium |
| Reason for the choice, trade-off | The decision, not the code | Low |
| External constraint (hardware, protocol, neighbouring system) | The world outside the repository | Low |
| Measured number with date and context | A dated measurement | Low, if dated |

The pattern is clear: **the closer the comment is to what the code already says, the faster it rots.** A paraphrase breaks at every edit. The reason for a choice survives whole refactorings, because it describes something that stays true even when every line around it has changed.

Two consequences follow that guide the rest of this document:

1. **A comment is a last resort, never a first option.** Before writing one, exhaust the verifiable ways of expressing the same thing.
2. **A comment that survives is a comment that does not depend on the details of the code.** Write at the level of the decision, not at the level of the instruction.

## Comments that must be deleted

Every comment that describes what the code does is unchecked duplication and must go. Below are the recurring patterns, in any language.

**Paraphrase of the line.** The comment repeats the instruction in English.

```
// increments the counter
++count;
```

There is no new information. Delete it.

**Paraphrase of the function.** The comment describes the behaviour the name should describe.

```
// returns true if the user is of legal age
bool check(int a) { return a >= 18; }
```

This does not call for a comment, it calls for `is_adult(age)`. Comments in this family are a symptom that a function extraction, a named constant, an enum or a type was not done. The comment is compensating for a bad name — fix the name.

**Signature redundancy.** Documentation blocks that repeat what the type system already declares (`@param int $x` above a parameter already typed as `int`, `@returns {string}` above an already typed return) are pure duplication in any language with types in the signature. The checker checks the signature and ignores the comment; when the two diverge, it is the comment that is wrong, and nobody warns you.

**Commented-out code.** Version history exists exactly for that. Commented-out code does not compile, is not tested, is not refactored along with the rest and ages until nobody knows whether it still makes sense. Always delete it.

**Ceremonial noise.** Headers with the author's name and creation date (the history knows), ASCII dividers separating sections of a file (if the file needs dividers, it needs to be split into files), `// end of if`, `// constructor`, inline changelog at the top of the file.

**Empty markers.** `// NOTE:`, `// IMPORTANT:` or `// TODO` with no actionable content — no what to do, no why, no reference to a ticket. A TODO nobody can carry out is just a confession of doubt.

**The test common to all of them:** if deleting the comment removes no information from the repository, it was not information — it was echo.

## Comments that must exist

These are the ones that record information that exists nowhere else in the file. Deleting them is loss of information, not cleanup.

**Reason for the choice.** The code shows the adopted solution and hides the rejected ones. When the obvious alternative was discarded for a non-obvious reason, that reason needs to be there — otherwise the next person reintroduces the alternative thinking they are improving the code.

```
// Sequential processing on purpose: the parallel version was tested and
// lost, because the synchronization cost dominates for batches < 10k items.
```

**Measured number.** Every empirically calibrated constant carries a measurement the value alone does not communicate. Record the number, the condition under which it was measured and when — a number without a date is a number without validity.

```
// 64 = saturation point measured in 2026-03; above it lock contention
// cancels the gain. Redo the measurement if the worker pool changes.
```

**Code that looks worse than the naive alternative.** Optimization is often deliberately less readable: a structure decomposed into parallel arrays, an unrolled loop, alignment padding, a linear search where a map would fit. Without a record of the measurement, someone "cleans up" the structure, goes back to the readable form and causes a performance regression no test catches — performance loss is precisely the class of regression continuous integration does not detect.

**External constraint.** Hardware limits, processor errata, the behaviour of a protocol, a ceiling imposed by a neighbouring service, a regulatory requirement. These are facts that live outside the repository and that no local artifact can express.

**Non-local invariant.** A property that must hold between distant parts of the system and that no local type captures: the required order of a vector because another module does binary search on it, the pairing between a write and a read in concurrent code, the mandatory order of two calls. When possible, turn the invariant into an executable assertion; when not possible, comment.

**Workaround.** A detour caused by a third-party bug, a specific dependency version or divergent behaviour between platforms. Include the reference to the bug and the removal condition — a workaround without an exit criterion becomes permanent by omission.

```
// Workaround for bug #4821 in lib X (breaks in 2.1.0, fixed in 2.3.0).
// Remove when the minimum version goes up to 2.3.
```

**Deliberate violation of a convention.** When the code breaks a project pattern on purpose, the comment is what separates "decision" from "carelessness". Without it, the reader has to choose between fixing something that is not broken and letting through something that is.

## The decision test

Before writing a comment, ask whether the information can be moved into the code. If it can, move it — you trade an unchecked sentence for a declaration some tool checks. If it cannot, the comment is the only place where that information exists.



The ways to internalize, in the order worth trying:

1. **Rename.** The most common case. A precise name makes the sentence that explained it unnecessary.
2. **Extract a function or method.** A block that needed an introductory comment becomes a named function, and the comment becomes the signature.
3. **Name the constant.** `86400` with a comment next to it becomes `SECONDS_PER_DAY`.
4. **Strengthen the type.** Replacing primitives with specific types eliminates comments about unit, range and meaning — a dedicated type for milliseconds makes it impossible to pass seconds by mistake.
5. **Use an enum instead of magic values.** The possible states become enumerable by the compiler, not listed in a comment.
6. **Write an assertion.** An invariant checked at run time is worth more than the same invariant written in prose.
7. **Write a test.** A test with a descriptive name documents behaviour and fails when the behaviour changes.

When none of these forms works, it is not your failure: it is a limit of the language. Then the comment stops being pollution and becomes the only possible vehicle.

## Public API is another category

Public interface documentation is not an implementation comment — it is a contract, and the rules above do not apply to it. The difference is the reader: the implementation comment is read by whoever opens the file; API documentation is read by whoever **will never open the file**.

That changes everything. Whoever calls your function sees the documentation in the editor, on the generated page, in autocomplete, in the interpreter's `help()`. For that person, the documentation is not an addition to the code — it is the only visible surface. Being against it is not being against pollution, it is being against documenting the API.

What belongs to that contract and does not fit in the signature:

- **Error semantics.** What the function throws, under which conditions, and what the caller should do about it.
- **Null and empty contract.** What an empty return means: absence, silent error, legitimate result.
- **Side effects.** Writing to disk, network calls, argument mutation, use of global state.
- **Preconditions and postconditions.** What must hold before and what holds after.
- **Performance and concurrency guarantees.** Asymptotic complexity when it is part of the contract, safety for concurrent use, reentrancy.
- **Resource ownership.** Who frees what — critical in languages with manual memory management.
- **Usage example**, when the correct way to call is not obvious from the signature.

Two common-sense restrictions still hold. First, document the **public interface**, not every private function: documenting everything produces the same noise as documenting nothing, and hides what matters. Second, do not repeat the signature — if the type already says it, the documentation should say what the type does not say.

## Configuration files

Configuration is the ground where the "self-explanatory code" thesis fails most visibly, because there is no code there. There is no function to extract, no type to strengthen, no test that explains the choice. There are only key-value pairs, and the value is arbitrary until someone says where it came from.

The key `max_connections` already has the best possible name. Still, `64` does not explain itself: the number came from a measurement, an infrastructure limit, an agreement with a neighbouring system or a guess nobody revisited — and the difference between those four cases is exactly what decides whether someone may touch it.

Configurations concentrate precisely the information with the lowest syntactic density and the highest risk: calibrated numbers, ceilings imposed by third parties, invisible couplings between services, values that only make sense in a specific environment.

**The correct answer has two parts, and only one of them is a comment.**

First, move the *field description* into a schema validated at load — JSON Schema with descriptions, a typed schema validator, a configuration type with explicit default values. That is internalizing in the sense of the previous section: it turns documentation into something verifiable, which fails at startup instead of failing in production. Direct gains: type, valid range, whether required and default value stop being prose.

Second, accept that the schema covers *what the field is* and does not cover *why this value*. That part remains a comment, and it should record origin and change condition:

```
# 64 = ceiling of the database service pool in production. Raising it here without
# raising it there makes the application queue instead of failing fast.
max_connections = 64
```

Two practical notes. Plain JSON accepts no comments at all — that is one of the reasons ecosystems migrated to formats that do (TOML, YAML, commentable JSON variants) for configuration meant for humans; when the format is strict JSON, the reason has to go into a neighbouring documentation file or into the schema. And values that change per environment call for the reason in the source file, not replicated in each environment.

## Common objections

**"That is in the version history."** It is, and still it does not reach whoever needs it. History degrades: mass reformatting, file renames, moving functions between modules and style changes erase the trail. More important than the technical degradation is the behavioural one — almost nobody investigates the history before deleting three lines that look ugly. Physical proximity is what makes the information arrive at the moment of the decision. History is a searchable dead archive; the comment is a warning at the point of risk.

**"It is documented in the architecture decision."** Decision documents serve structural choices: which database, which protocol, which boundary between services. They do not serve, and do not scale, to explain why this line has an off-by-one adjustment or why this constant is 64. Wrong granularity. When a relevant document exists, the right practice is a short comment that points to it, not the absence of a comment.

**"Good code does not need comments."** True for what the code can say, false for the rest. This objection treats "what the code does" as if it were all the relevant information, when the expensive information is typically the other kind: the reason, the measurement, the constraint. No code quality makes an alternative that was tested and discarded appear in the file.

**"A comment is an excuse for bad code."** It often is, and that is the legitimate part of the objection. The correct answer is the discipline of the decision test: if it can be internalized, internalize it — the comment must never be the easier path to avoid a refactoring. But the existence of abuse does not invalidate the use.

**"Comments become outdated, so it is better not to have them."** The argument proves too much. Tests also become outdated, and the conclusion is not to abandon tests. The correct conclusion is to write comments that rot slowly (reason, not paraphrase) and to treat them as part of the code when changing and reviewing.

**"People can ask whoever wrote it."** They cannot. In two years, whoever wrote it is either no longer on the project or does not remember. Code is read far more often than it is written, and almost always by someone without access to the original context — including yourself.

## Maintenance and review

A comment is part of the code it accompanies: changing one without changing the other leaves the file in an inconsistent state. That is the only maintenance rule that matters, and it needs to hold in review, not in goodwill.

**How to write so it lasts**

- Write at the level of the decision, not of the instruction. "Sequential because synchronization dominates below 10k items" survives refactorings; "iterates over the list" does not survive the next edit.
- Date what is a measurement. A number without a date cannot be audited or invalidated.
- Give an exit criterion to what is temporary. A workaround without a removal condition becomes permanent by omission.
- Prefer pointing to duplicating. Reference the ticket, the document or the bug report instead of copying their content.
- Place it at the point of risk. The comment protects the part someone will want to "clean up" — it needs to be there, not at the top of the file.

**In code review**

- [ ] Was every comment that describes what the code does removed or converted into a name, type, constant or test?
- [ ] Would every remaining comment survive a rewrite of the implementation?
- [ ] Do the comments touched by this change remain true?
- [ ] Are optimized code, non-obvious structure or deliberate convention violations justified?
- [ ] Do calibrated numbers carry origin and date?
- [ ] Do workarounds carry a reference and a removal condition?
- [ ] Does the new or changed public interface document errors, side effects and preconditions?
- [ ] Is there leftover commented-out code, divider, authorship header or empty marker?

**Rule of thumb for whoever is about to delete a comment:** if you cannot say where else that information is recorded, you are not cleaning up the file — you are deleting the only copy.