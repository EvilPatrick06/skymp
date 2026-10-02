# Recover interrupted file save batches

FileDatabase formerly replaced actor files separately. A failed second write
could leave one side of a gold transfer saved and the other side unchanged.
The regression fixture reproduced balances 90 and 200 after submitting 90
and 210. The old total was 300.

A save now prepares `changeForms/.pending-batch` before replacing actor files.
It contains the complete batch, version 1 and a SHA256 checksum. Live snapshots
are validated before publishing it. Repeated actor snapshots use the last
submitted value. Windows filename comparison also coalesces case aliases.

Startup, saved-form enumeration and a later save finish a valid pending batch
first. Corrupt journals refuse recovery without publishing any actor record.
Unsafe filenames, Windows control characters, alternate streams, noncanonical
identities and duplicate destinations are rejected before replay. This file is
in the configured world database folder; it must stay outside served resources.

Actor data is flushed before successful acknowledgement. The journal remains
when any write fails. A large batch uses four file writers, all joined before
completion or failure; save-storage still serializes complete batches. Directory
sync is done once after actor replacements on POSIX. The journal is removed
only after the full batch succeeds. Journals are bounded at 256 MiB.

The verified scope is process interruption and file-write failure. These probes
do not establish Windows directory-metadata durability across a power loss.
Ordinary saved-file parsing outside the journal retains its existing policy.
This change alone does not enable merchant transfers: atomic inventory receipts,
exact saved-snapshot acknowledgement and the private merchant ledger controller
must still be integrated.

Validation uses the actual FileDatabase: interrupted replacement, restart,
failed journal preparation, older-batch retry, invalid live snapshots, duplicate
input, pending-batch reads, malformed journals and large-batch failure.
The Windows probes also cover case aliases and alternate-stream names.

The optional `Measure small and full population file save batches` Catch case
is hidden from normal runs and can be run with `[.merchant-save-benchmark]`.
On the shipping machine the old 25/5,087-record path measured 20.572/7,168.100 ms;
the first serial durable path measured 54.188/11,233.235 ms; bounded writes
measured 50.030/8,313.469 ms. These are worker disk timings using simple native
snapshots, not connected-session frame times. Durable writes still cost more
than the previous unflushed path.

The Windows flush operation is documented by Microsoft:
[CRT _commit](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/commit).
Windows ordinal case comparison uses
[CompareStringOrdinal](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal).
