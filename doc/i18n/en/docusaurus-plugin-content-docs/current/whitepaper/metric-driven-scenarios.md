---
title: Metric-Driven Scenarios
---

# Metric-Driven Scenarios

The following three examples illustrate metric-driven dynamic policy management. Users integrate the business
metrics and decision logic for their projects.

## Metric-Driven Dynamic Policy Management

These requirements aggregate metrics across services, let a controller make decisions, and distribute policies
to the relevant services. The framework reuses OpenTelemetry metric reporting, Prometheus Query aggregation,
controller selection, and etcd policy distribution.

The main steps for a custom policy are:

1. Configure metric semantics.
2. Register metric reporting.
3. Register the decision algorithm.
4. Listen for policy changes.

See [Observability and Dynamic Policies](observability-policy) for collection and policy APIs, and
[HPA Controller](hpa-controller) for service replica control.

## Scenario One: Scale Order Matching by Order Volume

Trading order volume changes the load on order-matching services. Businesses can use order volume as a metric
to adjust the number of matching replicas.

This example concerns the relationship between order volume and service capacity. The business defines order
counting and replica-adjustment policies.

## Scenario Two: Adjust Indexes and Views by Search Distribution

Trading searches combine tags, ranges, and sorting conditions, making it difficult to index every combination.
Search behavior distributions help identify popular conditions and adjust index and dynamic-view counts.

This example balances cache hit rates against view overhead, and global-index search cost against view-index
maintenance during order updates. The business determines adjustments from search behavior metrics.

## Scenario Three: Adjust Matchmaking and Room Control by Battle Entry

The frequency and number of players entering battles can inform matchmaking service distribution, battle-room
queuing, and room precreation.

Matchmaking node adjustments and room control concern service capacity and business parameters respectively.
Each can integrate its decision logic through dynamic policy management. The business defines metric semantics
and control rules.
