#include "blocking_counter.h"

#include <chrono>
#include <thread>

#include "check_macros.h"
#include "time.h"

namespace ruy {

static constexpr float kBlockingCounterMaxBusyWaitSeconds = 2e-3f;

void BlockingCounter::Reset(std::size_t initial_count) {
  std::size_t old_count_value = count_.load(std::memory_order_relaxed);
  RUY_DCHECK_EQ(old_count_value, 0);
  (void)old_count_value;
  count_.store(initial_count, std::memory_order_release);
}

bool BlockingCounter::DecrementCount() {
  std::size_t old_count_value = count_.fetch_sub(1, std::memory_order_acq_rel);
  RUY_DCHECK_GT(old_count_value, 0);
  std::size_t count_value = old_count_value - 1;
  return count_value == 0;
}

void BlockingCounter::Wait() {
  // Busy-wait until the count value is 0.
  const std::int64_t wait_duration = static_cast<std::int64_t>(
      TimeFrequency() * kBlockingCounterMaxBusyWaitSeconds);
  std::int64_t wait_end = TimeNowRelaxed() + wait_duration;

  while (count_.load(std::memory_order_acquire)) {
    if (TimeNowRelaxed() > wait_end) {
      // If we are unlucky, the blocking thread (that calls DecrementCount)
      // and the blocked thread (here, calling Wait) may be scheduled on
      // the same CPU, so the busy-waiting of the present thread may prevent
      // the blocking thread from resuming and unblocking.
      // If we are even unluckier, the priorities of the present thread
      // might be higher than that of the blocking thread, so just yielding
      // wouldn't allow the blocking thread to resume. So we sleep for
      // a substantial amount of time in that case. Notice that we only
      // do so after having busy-waited for kMaxBusyWaitNOPs, which is
      // typically several milliseconds, so sleeping 1 more millisecond
      // isn't terrible at that point.
      //
      // How this is mitigated in practice:
      // In practice, it is well known that the application should be
      // conservative in choosing how many threads to tell gemmlowp to use,
      // as it's hard to know how many CPU cores it will get to run on,
      // on typical mobile devices.
      // It seems impossible for gemmlowp to make this choice automatically,
      // which is why gemmlowp's default is to use only 1 thread, and
      // applications may override that if they know that they can count on
      // using more than that.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      wait_end = TimeNowRelaxed() + wait_duration;
    }
  }
}

}  // namespace ruy