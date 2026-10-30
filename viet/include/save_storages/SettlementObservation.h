#pragma once

namespace Viet::SaveStorageDetail {
// Read failure after activity: a finishing operation may have latched an error.
// Readers are injectable to test that interleaving without scheduler timing.
template <class ReadOperations, class ReadWorker, class ReadFailure>
bool NoActiveOrFailedOperations(ReadOperations operations, ReadWorker worker,
                                ReadFailure failure)
{
  return operations() == 0 && !worker() && !failure();
}
}
