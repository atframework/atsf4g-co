---
title: Technical Whitepapers
---

# Technical Whitepapers

This section explains problems, design choices, recovery, and limitations for developers customizing algorithms,
storage, or control policies. For routine RPC, service, component, and configuration additions, start with the
[development quick start](../development/overview) and [component integration guides](../components/overview).

## Reading Paths

| Problem | Document | Topics |
| --- | --- | --- |
| Synchronize an object across subscribers and repair state after disconnection | [WAL and State Replication](wal-replication) | Ordering, incremental synchronization, snapshots, compaction, persistence, migration |
| Complete an operation involving several resource owners | [Distributed Transactions](distributed-transactions) | Global decisions, conflicts, idempotent execution, recovery, cleanup |
| Turn runtime metrics into executable business policies | [Observability and Dynamic Policies](observability-policy) | Metric semantics, collection, queries, publication, application |
| Preserve service capacity during stateful scaling | [HPA Controller](hpa-controller) | Replica recommendations, staged Target/Ready changes, Kubernetes integration |
| Apply business metrics to trading and battles | [Metric-Driven Scenarios](metric-driven-scenarios) | Order matching, search indexes, matchmaking, room preparation |

## Implementation and Design Scope

“Current implementation” refers to protocols, SDKs, services, and configuration in this repository. “Integration
design” and “recommendation” describe logic the business must implement and validate.
The three scenarios illustrate uses of business metrics; businesses define and integrate their metrics and decision logic.
Trading-related discovery selectors in the repository do not establish that a complete trading system exists.

| Existing capability | Integration responsibility |
| --- | --- |
| WAL publishers, subscribers, snapshot/hash callbacks; usage in dtmq, rank, and teams | Define log/snapshot semantics, storage durability, replayable actions |
| Transaction coordinator, client, participant, recovery protocols | Save business data consistently with participant snapshots; implement idempotent actions and lock scopes |
| RPC traces, metric export, Prometheus queries, custom policies, etcd watches | Define business metrics, input validity, decision algorithms, executors |
| HPA recommendations, Target/Ready selection, migration checks | Deploy metric adapters, register business checks, execute resource creation and object transfers |

“Notification sent,” “state cached,” “replica created,” and “migration complete” describe different stages.
Define completion conditions, retries, and queryable failure state for each. A timeout alone cannot determine
whether an operation happened.

## Design and Validation

Start with invariants: one valid writer per resource, an irreversible confirmed transaction decision, and no
scale-down past nodes that still own state. Then identify failures before/after writes, lost responses, restarts,
partitions, stale metrics, policy publication, and object transfers. Define idempotency keys, version checks,
recovery entry points, and metrics for each.

Papers include implementation pointers, public references, and validation checklists as applicable. These are integration
checks to perform, not substitutes for fault testing in the real environment. Existing unit-test coverage is
distinguished from deployment validation.
