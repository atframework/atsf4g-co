# Writing guidance

Read when revising writing rules, titles, promotional/navigation copy, or
terminology in comments, docs, or agent guidance.

## Core rules

- Write for the reader and task, using the file's language and voice. Lead with the needed fact or action;
  preserve reasoning and event order when they explain the result.
- Use familiar, precise words and one term per concept. Keep established technical terms and explain unfamiliar ones.
- Make the actor, action, object, and conditions clear. Split clauses when they become hard to follow; retain useful
  transitions and natural Chinese subject omission.
- Remove repeated setup, conclusions, empty modifiers, unsupported praise, and formulaic label-plus-explanation lists.
  Choose paragraphs, lists, and tables according to the content.
- Preserve supported judgments, uncertainty, tradeoffs, and author voice. Do not invent experiences or use authorship
  detector scores as writing-quality criteria.
- Comments explain reasons, invariants, and constraints; API docs describe inputs, outputs, errors, and side effects.

## Titles, promotional copy, and navigation

- Use short, formal topic phrases that name the section's subject or function.
  Put conditions, steps, and explanations in the body. Remove empty adjectives,
  two-clause slogans, and final periods; do not retain a long title with forced
  line breaks. Set no universal character limit: check recognition in both
  languages and on narrow screens.
- Do not use “从……到……” / “From … to …” coverage slogans on homepages, feature
  cards, or other promotional surfaces. Do not use “先……再……” / “First … then …”
  constructions in titles. Rewording them as “由……走向……” keeps the same problem.
  Avoid invitations or abstract actions such as “看一眼”, “跑通”, “看清”, and
  “掌握” in formal titles. Name the subject or function instead.
- Preserve actual operation order, input/output relationships, and numeric
  ranges in the body, with their objects and conditions. “先校验配置，再重启服务”,
  “从 JSON 输入生成 YAML 输出”, and “from 1 ms to 5 ms” can convey necessary
  meaning. Preserve quotations, identifiers, and original output verbatim.
- Make capability descriptions identify a function. Check what “直接表达”
  expresses, which actions “清晰流程” includes, and what supports “可靠”. State
  concrete structures, inputs, operations, and results; swapping adjectives
  does not supply missing information. Summaries must not imply unverified
  performance or comprehensive support.
- When reviewing a reported problem, inspect the page's titles, summaries,
  feature cards, buttons, and captions, plus recurring patterns in adjacent
  docs, navigation, and footers. Write natural topic phrases and explanations
  in each language. Do not retain English slogans for word-for-word alignment
  or batch-rewrite normal sentences that explain technical order.

## Sentence relationships

- Prefer direct statements. Use corrective contrasts to resolve a relevant misunderstanding or consequential
  distinction; remove negative alternatives introduced only to emphasize a positive claim.
- “不是 A，而是 B” commonly forms 对举 in 并列复句 when connecting clauses: it rejects A and affirms B.
  Check meaning and context before editing; the connective alone does not determine the sentence's structure.
- If A and B can coexist, state their roles or relative importance. For cause, sequence, prerequisites, or differing
  conditions, name that relationship directly. Do not invent mutual exclusion, causality, or progression.
- Apply the same review to reversed contrasts, split sentences, and English forms such as “not A but B” or
  “B rather than A”. Replacing connectives does not remove unnecessary contrast.
- Retain necessary negation, warnings, interface distinctions, and quotations. For example, “缺失指标不能按零处理”
  states a required boundary. Outside the scoped title/promotional restrictions,
  judge phrases in context without word bans or fixed frequency limits.

## Formulaic prose

Treat “AI 味” as a cue to inspect generic, repetitive, or context-inappropriate
writing. A sentence pattern cannot establish authorship. The examples below are
editorial review cues; they do not claim measured frequency in Chinese or every
model. Keep informative uses and the author's voice.

- Generic openings: “在当今……浪潮中”, “随着……不断发展”, or “In today's rapidly
  evolving landscape”. Start with the actual problem or a relevant change.
  Keep background when its date, condition, or effect explains the topic.
- Announcing the writing: “本文将深入探讨”, “接下来让我们”, or “Let's dive in”.
  Remove announcements that only promise an explanation. Retain a short scope
  or prerequisite statement when it helps the reader choose or use the page.
- Empty emphasis: “值得注意的是”, “显然”, “毋庸置疑”, or “It is important to note”.
  State the fact or risk directly. Keep warnings and the supported level of
  certainty; do not strengthen a claim while deleting its introduction.
- Artificial progression: “不仅……更……”, “既……又……还……”, or “not only … but also …”.
  Check whether the clauses add separate facts, a comparison, or a stronger
  claim. Preserve both facts when simplifying; do not invent progression or
  replace this wording with an unnecessary “不是……而是……” contrast.
- Staged questions: “问题来了”, “答案很简单”, or “The question is … The answer is …”.
  State the explanation directly in continuous prose. Keep real questions in
  FAQs, troubleshooting, and teaching when the question serves the reader.
- Causal chains: “通过……实现……从而……确保……” or repeated “enabling …, ensuring …”.
  Name who does what, under which conditions, and with which result. Split long
  chains and verify every causal link; an intended benefit is not a guarantee.
- Abstract noun chains: “进行……的实现与优化”, “实现能力提升”, or “the facilitation
  of …”. Use an accurate verb and name its object. Keep defined nouns such as
  serialization, admission, and transaction coordination when they are needed.
- Effortless promises: “只需”, “轻松”, “一站式”, “无缝”, or “simply”. Give the
  actual prerequisites, actions, and supported scope. Keep “only” when it
  states a verified count, exclusive condition, or other necessary restriction.
- Inflated significance or consensus: “开启新篇章”, “为……注入活力”, “业界普遍认为”,
  or “a game changer”. Remove unsupported praise; name the actual change.
  For consensus or research claims, provide a source and its scope. Preserve
  attributed opinions and analogies that explain a specific mechanism.
- Forced symmetry: “三大核心”, “一方面……另一方面……”, or a repeated three-part
  outline. Group content by its actual relationships. Keep genuine steps and
  parallel facts; do not invent a third item or pad sections to equal length.
- Repeated takeaways: “综上所述”, “归根结底”, “真正的关键在于”, or “Ultimately”.
  Remove summaries that repeat the paragraph or add only a grand conclusion.
  Keep synthesis that derives a result, identifies a tradeoff, or gives a next
  action. Do not turn each paragraph's last sentence into a slogan.

Check recurring sentence openings, label-plus-explanation lists, and chains of
near-synonyms across the text. Keep terminology stable. Do not manufacture
variation, ban punctuation, remove all transitions, or make every sentence short.

## Chinese wording cues

Use these as contextual editing cues. Keep defined technical meanings, such as 内存对齐、数据库投影、流处理水位、
算法收敛、闭环控制 and SRE 错误预算. Preserve identifiers and named resource types such as Kubernetes Secret.

| Wording | Prefer when this is the intended meaning |
| --- | --- |
| 补强、赋能、加持 | Name the actual fix, check, or capability. |
| 投影、水位 | Name the 缓存/副本/视图 or 最大值/上限/已处理日志序号. |
| 预算 | 重试次数上限、剩余次数、超时时间、数量上限. |
| 合同 | Name the 设计规范、接口约定、字段规则、平台协议 or 验收标准; retain legal uses. |
| 缺证、待证、未证；证据 | Name the missing input, unfinished check, source, logs, or test result. |
| 门槛、护栏、门禁 | State the prerequisite, role/count bounds, validation, or required acceptance check. |
| 抓手、底座 | Name the method, tool, library, runtime, or service. |
| 打通、沉淀、落地、闭环 | State the connection, saved content, implementation, deployment, persistence, or completion steps. |
| 对齐、拉齐、收敛 | Name the reference, merge, state update, or scope reduction. |
| 终态 | 结束状态、最终状态 or 最终决议; choose by context. |
| 口径、维度、颗粒度、链路 | State the calculation, scope, time source, processing unit, or request path. |
| 兜底、钳制 | State the trigger and actual retry, default, rejection, or limiting behavior. |
| 重新武装定时器 | 重新设置定时器、再次注册定时器. |
| 秘密 | 密钥、凭据、口令 or 敏感信息, according to the security concept. |
| 夹具 | 测试数据、测试样本; retain 测试夹具 for setup/teardown infrastructure. |
| 值得注意的是、显然、本质上、全面、强大、无缝 | State the fact, scope, cause, prerequisite, or measured behavior. |

Rewrite the sentence when a word substitution stays awkward. Keep legal uses of 合同/证据 and accurate uses of 门槛.
Use 结束状态 for a lifecycle that has ended, including failure, cancellation,
and timeout; ending does not imply success. Use 最终状态 for the result after a
described sequence of operations or replay; the object may still change later.
Use 最终决议 for a transaction's commit/reject decision; distinguish it from
completed local actions and successful ACK. Keep formal terminology required by
a definition or quotation, and preserve identifiers.
Translate English prose naturally; avoid long noun chains, repeated “enabling …, ensuring …”, and needless synonyms.

## Semantic preservation

- Preserve negation, modality, quantifiers, causality, scope, versions, units, and comparison direction.
  “May fail” must not become “will fail”; observations from one test must not become general guarantees.
- Keep “尚未验证”, “缺少输入”, and “验证未通过” distinct. Preserve validation dates, commands, results, and coverage;
  a successful build does not establish runtime behavior.
- Check the implementation before changing counts, resets, defaults, time sources, cancellation, ownership, or
  completion. Three total attempts means “总共最多尝试 3 次” or “首次失败后最多再重试 2 次”.
- State whether a condition is required or recommended. Name the actual 操作角色下限、人数/比例上下限 or
  开局所需最少人数; preserve inclusive bounds and rounding.
- Keep signatures, identifiers, config keys, protocol text, quotations, and machine-consumed strings unchanged.
  Treat changes to runtime logs, test expectations, or runtime-read docstrings as behavior changes.

## Review

1. Mark facts, terms, positions, quotations, and formatting that must survive.
   Distinguish verified information, inference, and unknowns. With incomplete
   material, write the supported part or list missing questions; do not invent
   numbers, sources, experiences, or check results.
2. Fix facts and reasoning, then structure, then grammar and wording. Decisions
   may lead with the judgment; tutorials need prerequisites, steps, and results.
   Retain useful process in derivations, retrospectives, and articles. Do not
   force three points or equally long sections.
3. State objects, actions, conditions, and results. Use precise verbs to reduce
   vague nouns. Remove openings, transitions, inflated judgments, and repeated
   summaries that add no information. Use lists for steps or parallel items,
   tables for comparisons, and paragraphs for continuous explanations.
4. Review wording and sentence patterns in context, without word blacklists or
   global replacements. Apply the title/promotional restrictions above; remove
   casual invitations, empty promises, and needless negative contrasts.
   Preserve technical order, ranges, corrections, exclusions, both facts in
   simplified progression, and consistent terms.
5. Compare negation, conditions, quantities, units, timing, causality, commitment
   strength, and verification status before and after. Express the same complete
   technical meaning in each language's natural order. When translating example
   labels, verify the keywords the tool accepts and synchronize inputs and
   accompanying resources. Preserve identifiers, config keys, commands, paths,
   protocol text, original output, and required formatting. Handle functional
   defects separately from prose edits. Validate format, links, and executable
   content as applicable; report only checks actually run.
6. Read the affected paragraphs and recurring patterns throughout the text;
   change only problematic passages. Stop or revert a polishing pass when it
   has no explainable benefit or starts to lose meaning or author voice. Do not
   manufacture naturalness with typos, random sentence lengths, or detector
   scores.

When updating these rules, merge duplicates and check representative prose, technical terms, real contrasts, and
necessary warnings. Verify new external claims with primary sources; record reading scope and limits. Keep detailed
rules here and the always-on entrypoint short.

## References

- [晋中学院《第八节 复句》](https://wxy.jzxy.edu.cn/uploads/zwx/file/20180410/3g5np6z83k.pdf): PDF pages 2, 5, 8 read
  2026-10-04; Chinese teaching classification of clauses and 对举, publication date unstated.
- [Microsoft: concise sentences](https://learn.microsoft.com/en-us/style-guide/word-choice/use-simple-words-concise-sentences):
  guidance/examples read 2026-10-06; updated 2022-06-24, English technical writing.
- [Microsoft: headings](https://learn.microsoft.com/en-us/style-guide/scannable-content/headings):
  heading and responsive-layout guidance read 2026-10-06; updated 2022-06-24.
- [Google: voice and tone](https://developers.google.com/style/tone):
  tone, repetition, and procedure guidance read 2026-10-06; technical docs.
- [Reinhart et al.: stylistic variation](https://arxiv.org/html/2410.16107v2):
  abstract, methods, results, and discussion read 2026-10-06; v2, 2025-08-21.
  English parallel corpora and GPT-4o/Llama 3 variants show model- and
  genre-dependent differences, including noun-heavy prose and participial
  clauses. This does not validate a Chinese phrase blacklist or establish
  authorship from one sentence. The review cues above adapt the findings and
  writing guides to this repository; no reader study or model comparison was
  run for this update.
- [Beemo](https://aclanthology.org/2025.naacl-long.357/): Appendix B read 2026-09-25; English editing rubric.
