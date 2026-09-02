# Writing guidance

Read this reference only when changing repository writing rules. Recheck the sources before turning a mutable style
signal into policy.

## Rules to preserve

- Write for the intended reader and task. Put the needed fact or action first.
- Use familiar, precise words. Keep a technical term when it names the concept more accurately than an everyday word;
  explain it only when the intended reader may not know it.
- Keep one idea per sentence where practical. Remove words, openings, conclusions, and repeated points that add no
  information.
- Use one term for one concept. Do not rename a cache, copy, derived view, or other concrete mechanism with a broader
  metaphor.
- Match the file's language and existing voice. Prefer natural paragraphs or short lists over a repeated bold-label and
  explanation template.
- During review, check repetition, awkward or over-formal phrasing, audience mismatch, generic filler, assistant-style
  introductions or wrap-ups, and factual support.

Do not maintain a general blacklist of "AI words." Vocabulary markers change as models and writers adapt, and a word
can be correct in one technical context but vague in another. Terms such as `delve`, `robust`, 补强, 投影, or 水位 are review
signals only: keep them when they are precise and familiar to the audience; otherwise name the concrete action or
mechanism.

## Sources

- [Google developer documentation: Voice and tone](https://developers.google.com/style/tone), last reviewed
  2026-08-30. It recommends direct, concise, conversational technical writing and warns against jargon, clichés,
  placeholder phrases, long-winded sentences, and claims that a procedure is simple or easy.
- [Microsoft Style Guide: Use simple words, concise sentences](https://learn.microsoft.com/en-us/style-guide/word-choice/use-simple-words-concise-sentences),
  last reviewed 2026-08-30. It recommends precise words, removing empty modifiers, and using one term per concept.
- [GOV.UK Functional Standards writing style guide](https://www.gov.uk/government/publications/handbook-for-standard-managers/functional-standards-writing-style-guide),
  published 2024-09-30 and reviewed 2026-08-30. It recommends reader-focused, outcome-based rules with one idea per
  sentence and no content likely to age quickly.
- [Beemo: Benchmark of Expert-edited Machine-generated Outputs](https://aclanthology.org/2025.naacl-long.357/), NAACL
  2025. Its expert-editing rubric targets repetition, awkward wording, tone mismatch, unnecessary assistant framing,
  irrelevant content, and unsupported facts.
- [Human-LLM Coevolution: Evidence from Academic Writing](https://aclanthology.org/2025.findings-acl.657/), Findings of
  ACL 2025. It shows that individual vocabulary markers change as people adapt their use of LLMs, so word-frequency
  heuristics should not become permanent writing rules.
