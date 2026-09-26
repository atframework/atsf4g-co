# Writing guidance

Read this reference when changing repository writing rules or applying them in a terminology cleanup. Recheck the
sources before turning a mutable style signal into policy.

## Rules to preserve

- Write for the intended reader and task. Put the needed fact or action first in instructions and decision notes;
  preserve the necessary reasoning or event order in explanations and retrospectives.
- Use familiar, precise words. Keep a technical term when it names the concept more accurately than an everyday word;
  explain it only when the intended reader may not know it.
- Keep one idea per sentence where practical. Remove words, openings, conclusions, and repeated points that add no
  information.
- Use one term for one concept. Do not rename a cache, copy, derived view, or other concrete mechanism with a broader
  metaphor.
- Match the file's language and existing voice. Prefer natural paragraphs or short lists over a repeated bold-label and
  explanation template.
- Preserve the author's supported judgments, first-person observations, doubts, and tradeoffs. Do not turn a
  retrospective into a neutral report or invent experiences, opinions, or emotions.
- During review, check repetition, awkward or over-formal phrasing, audience mismatch, generic filler, assistant-style
  introductions or wrap-ups, and factual support. Match structure to content rather than forcing three points,
  equally sized sections, or a summary after every paragraph.
- Treat writing quality and authorship detection as different questions. Do not manufacture errors, slang, random
  sentence lengths, or extra first-person statements to look human; detector scores are not acceptance criteria.

## Words to avoid outside their precise meaning

Use the following list when reviewing Chinese comments, docs, and agent guidance. Avoid these words when they replace
a concrete action, object, count, or duration; keep established technical terms when they express the intended meaning.
The Chinese replacements are editorial choices based on project text and the style guides below. They are not a
statistically validated list of Chinese words that identify AI authorship.

| Word | Prefer when this is the intended meaning |
| --- | --- |
| 补强 | 修复、补充检查、增加测试; name the action being performed. |
| 投影 | 缓存、副本、视图; name the actual data structure or relationship. |
| 水位 | 最大值、上限、已处理的日志序号; identify the value being compared. |
| 预算 | 重试次数上限、剩余重试次数、超时时间、剩余时间、数量上限; name what is counted or timed. |
| 赋能、加持 | 提供什么功能、让谁能做什么; describe the capability instead of praising it. |
| 抓手 | 方法、工具、检查项; identify the specific means. |
| 底座 | 公共库、运行时、基础服务; name the component and its responsibility. |
| 打通 | 连接两个服务、接入接口、传递数据; state the endpoints and operation. |
| 沉淀 | 记录结论、保存数据、提取共用代码; say what is kept and where. |
| 落地 | 实现、部署、写入数据库; distinguish implementation, deployment, and persistence. |
| 闭环 | 修复并验证、请求处理后返回结果; state the actual steps and completion condition. |
| 对齐、拉齐 | 保持一致、按某规范修改; name the reference and what must match. |
| 收敛 | 合并冲突、统一状态、缩小范围; state the operation or result when no convergence property is meant. |
| 口径 | 计算规则、统计范围、时间来源; identify the rule or source being used. |
| 维度 | 检查项、条件、指标; prefer 按日志数量、按保留时长 over 按数量维度、按时间维度. |
| 颗粒度 | 按事务、按消息、按批次处理; specify the actual unit. |
| 链路 | 调用顺序、处理流程、请求经过的服务; identify which path is meant. |
| 兜底 | 失败时重试、缺省时取某值、超时后清理; name the actual fallback and its trigger. |
| 护栏、门禁 | 参数校验、权限检查、合入前必须通过的检查; state what is checked and when it blocks an action. |
| 钳制 | 限制在某范围、超过上限时取上限值; describe the actual bounds and behavior. |
| 重新武装定时器 | 重新设置定时器、再次注册定时器; use the wording that matches the timer API. |
| 秘密 | 密钥、凭据、口令、敏感信息; when the term translates “Secret”, name the key or credential and keep a resource type such as Kubernetes Secret. |
| 夹具 | 测试数据、测试样本、样本数据; when 夹具 names test data, use the data term and keep 测试夹具 only for a genuine test fixture such as setup/teardown scaffolding. |

For example, replace “耗尽重试预算” with “重试次数达到上限”, “不消耗预算” with “不增加重试计数”, and
“整个测试进程的总预算” with “整个测试进程的超时时间”. Rewrite the sentence when a word swap would remain awkward:
“真实耗时耗尽离线期限预算” can become “实际经过的时间也计入离线时长”.

Keep 预算 for financial planning or an established technical term such as SRE 错误预算. A technical topic alone does not
make it a useful name for a retry count or timeout. Check the code before choosing 上限, 已用次数, or 剩余次数; preserve
whether the count includes the first attempt and when it resets. Keep identifiers, configuration keys, and quoted
protocol text unchanged during a wording cleanup. When translating “Secret”, name the security concept the code means:
密钥 for a cryptographic or API key, 凭据 or 口令 for a credential, or 敏感信息 for sensitive data in general; keep a named
resource such as Kubernetes Secret. Do not use 秘密, which reads as an everyday secret and hides which credential is meant.

Preserve terms such as 内存对齐、张量维度、网络链路、链路追踪、算法收敛 and 闭环控制 when used in those technical senses.
Keep mathematical or database 投影, stream-processing 水位, and other defined terms when they name the actual concept.
Do not replace a documented convergence guarantee with a one-time assignment or merge. For 兜底 and 钳制, distinguish
retrying, returning a default, rejecting input, and limiting a value; these are different behaviors. Do not invent a
fallback or change a boundary just to make the sentence shorter.

This is a list of context-dependent editing cues, not a general blacklist of "AI words". The same applies to English
words such as `delve` and `robust`: replace them when they add no precise information. Read each match in context and
keep established terminology; do not make a search hit alone a lint failure.

## Empty modifiers and formulaic sentences

Review whole sentences as well as individual words. Remove the following expressions when they add no facts, evidence,
or useful structure; keep necessary warnings, real contrasts, and summaries that help the reader decide what to do.

| Expression | Prefer when the expression adds no information |
| --- | --- |
| 值得注意的是、需要指出的是 | State the relevant fact or condition directly. |
| 显然、不难发现、毋庸置疑 | Give the reason or evidence; do not use certainty as a substitute for either. |
| 本质上、从某种意义上说、从某个角度来看 | State the cause, scope, or perspective if it matters; otherwise remove the preamble. |
| 全面、全方位、深度、系统性 | Name the files, cases, or method covered instead of implying unspecified thoroughness. |
| 无缝、开箱即用、强大、优雅、高效、稳健 | State the setup requirements or demonstrated behavior; remove unsupported praise. |
| 不仅……更是……、不是……而是…… | State the change and its effect directly unless the comparison explains a real distinction. |
| 综上所述、总而言之、通过以上分析 | Remove repeated conclusions; retain a summary only if it adds a decision or useful synthesis. |
| 对……进行……处理、开展……工作 | Use the specific verb, such as 加载配置、检查参数、删除记录. |

Do not invent measurements or guarantees to replace an adjective. Keep a qualifier when it has a precise role, such as
统计显著性, and keep numbered steps or 首先／然后 when order matters. Natural writing does not require slang or a casual
tone; preserve the file's audience, technical accuracy, and useful structure.

## Sentence structure and semantic preservation

- Make the actor, action, object, and relevant condition clear. Replace nominalized actions such as “对配置进行加载”
  with “加载配置”; split nested clauses when their subjects or conditions become hard to follow.
- Resolve ambiguous pronouns and explain actual causal relationships. Do not add “因此” or “从而” when the evidence
  only shows two events or an association. Keep necessary transitions, passive constructions, and clear subject
  omission in Chinese; natural prose does not require every sentence to be short or active.
- In English, check noun chains and repeated participial endings such as “enabling …, ensuring …”. State the actual
  action or result. Keep ordinary definitions with “is” when accurate, and do not replace terms with synonyms solely
  to vary vocabulary. Translate for the target language rather than copying the source sentence structure.
- Preserve negation, quantifiers, modality, causality, units, and scope. “May fail” must not become “will fail”;
  “observed in this test” must not become a general guarantee. Keep real contrasts, necessary warnings, quoted text,
  and punctuation that serves a purpose.
- Check the implementation before changing descriptions of attempt counts, counter resets, inclusive bounds, time
  sources, defaults, cancellation, ownership, or completion. For a contract allowing three total attempts, write
  “总共最多尝试 3 次” or “首次失败后最多再重试 2 次”, not “最多重试 3 次”.
- Comments should explain non-obvious reasons, invariants, and constraints. API documentation should describe the
  applicable inputs, outputs, errors, and side effects. Do not restate obvious code or invent behavior to make a
  sentence more concrete.
- Keep signatures, identifiers, configuration keys, documentation tags, protocol text, and machine-consumed strings
  unchanged during a wording edit. Log messages, test expectations, and runtime-read docstrings may be contracts;
  handle changes to them as behavior changes rather than silently including them in prose cleanup.

## Editing and review

1. Read the relevant text and its evidence. Identify factual, structural, or wording problems and the facts, author
   judgments, terminology, and format that must survive the edit.
2. Fix facts and reasoning before structure, grammar, and vocabulary. Change the affected passages; scan the whole
   document for the same problem without rewriting passages that already work.
3. Compare the original and revised meaning, especially conditions, counts, timing, negation, and certainty.
   Review natural flow and author voice separately. Read difficult sentences aloud if useful, but do not force
   written technical prose to imitate speech.
4. Stop or undo a revision that has no identifiable benefit or loses supported facts or author characteristics.
   Repeated whole-document “make it more natural” rewrites are not a substitute for diagnosing a problem.
5. Validate the affected format, links, examples, and any executable content with the relevant workflow. Report what
   actually ran; a documentation build does not establish that a code example works.

## Examples from this repository

These illustrate wording choices, not changes to runtime behavior. Recheck the surrounding code before applying them.

- In `doc/docs/components/dtmq.md`, “DB 落地” can be written as “数据库存储”.
- In `src/component/distributed_transaction/dtcoordsvr/logic/transaction_manager.cpp`, “使用 client 时间口径” can be
  written as “使用 client 提供的时间戳”.
- In `src/teamsvr/service/room/logic/room/team_room.h`, “按数量维度压缩” can be written as “按日志数量压缩”, and
  “按时间维度压缩” as “按日志保留时长压缩”.
- In test acceptance notes, “这两个用例是 P0 门禁” can be written as
  “这两个 P0 用例必须通过”.

## Updating this guidance

Recheck the sources when writing models or supported genres change, recurring editing failures appear, or a cited
study is revised. Keep stable rules and revise the rule responsible for the failure.

- Search current primary research and editing guides for writing style, voice preservation, semantic drift,
  homogenization, and counterexamples. Distinguish new drafting from editing and English findings from Chinese use.
- Record publication/version dates, the part actually read, language, genre, models, and limitations. Separate a
  study's finding from an editorial inference; an abstract does not establish that its methods were reviewed.
- Compare old and proposed rules on representative comments, docs, and prose under the same model/version and
  inputs. Include accurate technical terms, real contrasts, and necessary warnings as cases that must remain.
- Judge factual and semantic preservation before readability, author voice, repetition, and remaining editing work.
  When feasible, hide which rule produced each output from reviewers. Do not substitute detector scores or universal
  word-frequency/length thresholds for these checks, and report evaluations that were not performed.
- Merge or remove redundant rules, update examples and exceptions together, and keep research details here rather
  than expanding always-on prompts or creating a dependency on another repository or a personal note.

These are editorial procedures, not a claim that this rule set has passed a multi-model writing evaluation.

## Sources

- [Google developer documentation: Voice and tone](https://developers.google.com/style/tone), last reviewed
  2026-09-25. It recommends direct, concise, conversational technical writing and warns against jargon, clichés,
  placeholder phrases, long-winded sentences, and claims that a procedure is simple or easy.
- [Microsoft Style Guide: Use simple words, concise sentences](https://learn.microsoft.com/en-us/style-guide/word-choice/use-simple-words-concise-sentences),
  last reviewed 2026-09-25. It recommends precise words, removing empty modifiers, and using one term per concept.
- [Microsoft Style Guide: Avoid jargon](https://learn.microsoft.com/en-us/style-guide/word-choice/avoid-jargon), last
  reviewed 2026-09-25. It distinguishes useful technical shorthand from unfamiliar jargon and advises avoiding
  business and marketing jargon when familiar terms work.
- [GOV.UK Functional Standards writing style guide](https://www.gov.uk/government/publications/handbook-for-standard-managers/functional-standards-writing-style-guide),
  published 2024-09-30 and reviewed 2026-09-25. It recommends reader-focused, outcome-based rules with one idea per
  sentence and no content likely to age quickly.
- [Beemo: Benchmark of Expert-edited Machine-generated Outputs](https://aclanthology.org/2025.naacl-long.357/), NAACL
  2025, Appendix B reviewed 2026-09-25. Its English-text editing rubric targets repetition, awkward wording, tone
  mismatch, unnecessary assistant framing, irrelevant content, and unsupported facts; it is not a Chinese word list.
- [Human-LLM Coevolution: Evidence from Academic Writing](https://aclanthology.org/2025.findings-acl.657/), Findings of
  ACL 2025, reviewed 2026-09-25. It reports changing word frequencies in arXiv abstracts as authors adapt their use of
  LLMs. This supports caution about fixed vocabulary markers, not labeling particular Chinese words as AI-generated.

### Recent research and limits

The following sources were checked on 2026-09-25 at the reading depth stated below. Findings describe the studied
models and tasks; recommendations above are editorial applications, not proven universal fixes. Abstract-only
checks do not establish review of full methods or experimental replication.

- [Do LLMs write like humans?](https://www.pnas.org/doi/10.1073/pnas.2422455122), PNAS, 2025-02-18; abstract and
  public results checked. The studied instruction-tuned models favored noun-heavy prose and struggled with genre
  variation. This motivates sentence-level review rather than vocabulary replacement alone.
- [Can You Make It Sound Like You?](https://aclanthology.org/2026.acl-long.2030/), ACL, July 2026; abstract checked.
  In the reported 81-person study, post-editing improved personal-style similarity but did not fully restore
  unassisted writing characteristics. Human review or a few style samples do not guarantee restored author voice.
- [The shrinking landscape of linguistic diversity](https://pubmed.ncbi.nlm.nih.gov/42637911/), Nature Human
  Behaviour, 2026-08-24; author abstract and [journal briefing](https://www.nature.com/articles/s41562-026-02549-7)
  checked, not the paywalled methods. The reported homogenization motivates preserving author differences during
  revision; it is not a reason to inject random variation.
- [How LLMs Distort Our Written Language](https://arxiv.org/abs/2603.18161v2), preprint v2, 2026-08-26; abstract
  checked. The reported meaning changes even under grammar-only requests motivate explicit before/after semantic
  review. No effect size is adopted as a general rule.
- [Style as a Confound](https://arxiv.org/abs/2608.26710v1), arXiv v1, 2026-08-27; abstract checked. Professional
  editing changed different detectors' scores in different directions; scores do not establish writing quality
  or authorship.
- [AI Writers Have a Consistent Stylometric Footprint, but AI Editors Do Not](https://arxiv.org/abs/2608.27855v2),
  arXiv v2, 2026-09-22; abstract and version history checked. Generation and editing showed different feature
  patterns. Evaluate them separately; distinguish vocabulary diversity within a text from stylistic diversity
  across authors. Conference remarks on arXiv alone do not verify formal publication status.
