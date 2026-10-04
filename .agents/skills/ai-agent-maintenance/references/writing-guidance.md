# Writing guidance

Read when revising writing rules or terminology in comments, docs, or agent guidance.

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
  states a required boundary. Use context-based judgment without phrase bans or fixed frequency limits.

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

1. Read the text and relevant source. Fix facts and reasoning, then structure and wording; change only affected passages.
2. Compare meaning before and after, especially conditions, counts, timing, certainty, and necessary contrasts.
   Undo edits that lose information or have no clear benefit.
3. Validate format, links, and executable content as applicable. Report checks actually run.

When updating these rules, merge duplicates and check representative prose, technical terms, real contrasts, and
necessary warnings. Verify new external claims with primary sources; record reading scope and limits. Keep detailed
rules here and the always-on entrypoint short.

## References

- [晋中学院《第八节 复句》](https://wxy.jzxy.edu.cn/uploads/zwx/file/20180410/3g5np6z83k.pdf): PDF pages 2, 5, 8 read
  2026-10-04; Chinese teaching classification of clauses and 对举, publication date unstated.
- [Microsoft: concise sentences](https://learn.microsoft.com/en-us/style-guide/word-choice/use-simple-words-concise-sentences):
  guidance/examples read 2026-10-04; updated 2022-06-24, English technical writing.
- [Google: voice and tone](https://developers.google.com/style/tone): reviewed 2026-09-25; technical documentation.
- [Beemo](https://aclanthology.org/2025.naacl-long.357/): Appendix B read 2026-09-25; English editing rubric.
