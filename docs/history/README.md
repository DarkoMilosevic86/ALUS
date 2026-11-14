# ALUS development history

These are the reports written as ALUS was built, one per phase, **in
Serbian**. They are kept because the code cites them: a comment that says
`N79` or `phase-9.md O7` points here. Each report lists what the phase
delivered, how it deviated from the plan (the `O…` items, *odstupanja*) and
what live measurement found (the `N…` items, *nalazi*). Findings are numbered
once across the whole project.

The design documents, in English, are in [`../design/`](../design/vision.md).

| Report | Phase | Findings | Summary |
|---|---|---|---|
| [`phase-plan.md`](phase-plan.md) | 0–9 | — | the original phase plan, with an executable exit criterion and tests for every phase |
| [`../../spikes/RESULTS.md`](../../spikes/RESULTS.md) | 0 | N1–N19 | three spikes (Wayland keyboard input, speech latency, quick navigation through `Collection`) and the three decisions they forced |
| [`phase-2.md`](phase-2.md) | 2 | N20–N25 | speech: the speech manager, SSIP, index marks that get lost, deadlines |
| [`phase-3.md`](phase-3.md) | 3 | N26–N35 | input and scripts: talking to the compositor's keyboard monitor directly instead of through libatspi (the diagnosis of N19), the generated command table |
| [`phase-4.md`](phase-4.md) | 4 | N36–N44 | the object model: focus, ancestors, roles and states, object navigation; two defects that would have made the reader mute without an error |
| [`phase-5.md`](phase-5.md) | 5 | N45–N51 | text and review: lines, words, characters, the review cursor, say all, terminals, formatting |
| [`phase-6.md`](phase-6.md) | 6 | N52–N61, N88–N95 | browse mode: `getTextWithFields` over the tree, quick navigation, the virtual caret, tables |
| [`phase-7.md`](phase-7.md) | 7 | N62–N65 | braille: liblouis, BrlAPI, regions, routing |
| [`phase-8.md`](phase-8.md) | 8 | N66–N74 | app modules and the add-on ABI; all four modules rewritten after measuring real applications (N68: Calc reports 2147483647 children) |
| [`phase-9.md`](phase-9.md) | 9 | N75–N106 | the settings window (its own process, N79), the gestures dialog, gettext with Serbian Cyrillic and Latin, autostart |
| [`phase-10.md`](phase-10.md) | 10 | N107–N128 | the commands that were announced but not written (29 written), GTK 4 read as disabled (N111), browse mode in Chrome (N112–N116), codepoint offsets (N113), and the fixes from daily use since |
| [`orca-comparison.md`](orca-comparison.md) | — | — | the protocol and results of a side-by-side measurement against the other screen reader |

## Reading a citation

| In the code | Where |
|---|---|
| `N113` | the finding of that number; the table above says which report |
| `phase-9.md O7` | deviation 7 in the phase 9 report |
| `T5.7` | test 7 of phase 5's exit criteria (`phase-plan.md`), and the Catch2 test tagged `[T5.7]` |
| `architecture.md §7.3` | [`../design/architecture.md`](../design/architecture.md), section 7.3 |
| `R14` | risk 14 in [`../design/risks.md`](../design/risks.md) |
| `H6` | hard requirement 6 in [`../design/vision.md`](../design/vision.md) |
