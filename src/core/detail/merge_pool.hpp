// A barrier thread pool with fixed slices.
//
// Lifted out of profile_dp_blocked.cpp, where it was the largest block naming
// no DP type at all. It is the only concurrency primitive in src/, and keeping
// it beside the sweep made a 409 line file look like it was about threading.
//
// The rung's own tuning, kRadixBits and kParallelFloor, stays in the rung. This
// knows nothing about buckets beyond their count.
//
// Internal to src/core.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mayflower::detail {

// Workers created once per sweep rather than once per cell.
//
// The first version of this rung spawned a thread per bucket range per cell.
// Thread creation measured several milliseconds here, and a sweep has a hundred
// cells, so V3 was reliably SLOWER than V2: the standard instance scaled to
// 0.56x at ten threads, and 6x6 took 4.5 s where one thread took 0.008. The
// work was never the problem. The pool below creates each worker once, and every
// cell costs one barrier instead of a fresh set of threads.
//
// Each worker owns a fixed slice of the bucket range for the whole sweep, so
// there is no queue and no stealing, and no two workers ever touch one bucket.
class MergePool {
public:
    // `slices` is how many units of work the range divides into. The caller
    // owns that number: this only cuts [0, slices) into one contiguous piece
    // per worker, which is what makes the partition argument hold.
    MergePool(int workers, std::size_t slices,
              std::function<void(std::size_t, std::size_t)> job)
        : job_(std::move(job)) {
        for (int t = 0; t < workers; ++t) {
            const std::size_t from = slices * static_cast<std::size_t>(t) /
                                     static_cast<std::size_t>(workers);
            const std::size_t to = slices * static_cast<std::size_t>(t + 1) /
                                   static_cast<std::size_t>(workers);
            threads_.emplace_back([this, from, to] { loop(from, to); });
        }
        live_ = workers;
    }

    ~MergePool() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
            ++generation_;
        }
        work_.notify_all();
        for (auto& t : threads_) t.join();
    }

    // Run the job over every slice and return once all of them are finished.
    void runAll() {
        {
            std::lock_guard<std::mutex> lock(m_);
            pending_ = live_;
            ++generation_;
        }
        work_.notify_all();
        std::unique_lock<std::mutex> lock(m_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void loop(std::size_t from, std::size_t to) {
        std::uint64_t seen = 0;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_);
                work_.wait(lock, [this, seen] { return generation_ != seen; });
                seen = generation_;
                if (stop_) return;
            }
            job_(from, to);
            {
                std::lock_guard<std::mutex> lock(m_);
                --pending_;
            }
            done_.notify_one();
        }
    }

    std::function<void(std::size_t, std::size_t)> job_;
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable work_, done_;
    std::uint64_t generation_ = 0;
    int pending_ = 0;
    int live_ = 0;
    bool stop_ = false;
};

}  // namespace mayflower::detail
