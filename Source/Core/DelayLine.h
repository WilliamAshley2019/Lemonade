#pragma once
// Whole-sample stereo delay used for plugin delay compensation. Length 0 is a pass-through.
// Allocation only happens in setLength() (message thread); process() never allocates.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace lemon::core
{
class StereoDelay
{
public:
    void setLength(int n)
    {
        n = std::max(0, n);
        if (n == len_)
            return;
        len_ = n;
        l_.assign(static_cast<std::size_t>(n), 0.0f);
        r_.assign(static_cast<std::size_t>(n), 0.0f);
        pos_ = 0;
    }

    int length() const noexcept { return len_; }

    void clear() noexcept
    {
        std::fill(l_.begin(), l_.end(), 0.0f);
        std::fill(r_.begin(), r_.end(), 0.0f);
        pos_ = 0;
    }

    // In place.
    void process(float* l, float* r, int n) noexcept
    {
        if (len_ == 0)
            return;
        int p = pos_;
        for (int i = 0; i < n; ++i)
        {
            const float il = l[i], ir = r[i];
            l[i] = l_[static_cast<std::size_t>(p)];
            r[i] = r_[static_cast<std::size_t>(p)];
            l_[static_cast<std::size_t>(p)] = il;
            r_[static_cast<std::size_t>(p)] = ir;
            if (++p == len_)
                p = 0;
        }
        pos_ = p;
    }

private:
    std::vector<float> l_, r_;
    int len_ = 0;
    int pos_ = 0;
};
}
