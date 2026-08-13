# UnknownPass lifecycle contract

`UnknownPass` preserves a pipeline node whose concrete runtime descriptor is
temporarily unavailable. It is a real no-op framegraph node, not a permissive
serialization fallback.

The placeholder retains the original runtime type and inspect payload, exact
pipeline position and name, enabled/passthrough state, graph reads and writes,
aliases, resource specifications and debug symbols. Keeping this contract lets
callers preserve topology while making the missing implementation explicit.

Restoration constructs an unattached candidate from the original descriptor
and swaps it into the pipeline only after the complete payload is accepted.
Schema drift or conversion failure leaves the placeholder and payload intact.
Execution never pretends that an unavailable pass produced valid pixels.
