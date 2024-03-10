/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_CONGESTION_CONTROLLER_BBR_WINDOWED_FILTER_H_
#define MODULES_CONGESTION_CONTROLLER_BBR_WINDOWED_FILTER_H_

// 来自 Chromium 中的 Quic BBR 实现

// 实现了 Kathleen Nichols 的算法，用于在固定时间间隔内跟踪一系列样本的最小（或最大）估计值。
// （例如，过去五分钟内的最小 RTT。）该算法跟踪最佳、次佳和第三佳的最小（或最大）估计值，
// 维持一个不变量，即第 n 个最佳的测量时间 >= 第 n-1 个最佳的测量时间。

// 该算法的工作原理如下。在重置时，所有三个估计值都设置为相同的样本。然后在窗口的第二个四分之一中记录第二佳估计值，
// 在窗口的后半部分记录第三佳估计值，从而限制了当真实最小值在窗口内单调增加（或真实最大值单调减少）时的最坏情况误差。

// 一个新的最佳样本会替换所有三个估计值，因为新的最佳值比窗口中的其他所有值都低（或高），并且它是最近的。
// 因此，每个新的最小值都会有效地重置窗口。对于第二佳和第三佳估计值也适用相同的属性。具体来说，当一个样本到达时，
// 如果它比第二佳估计值更好但不比最佳估计值更好，它会替换第二佳和第三佳估计值，但不会替换最佳估计值。
// 同样地，一个比第三佳估计值更好但不比其他估计值更好的样本只会替换第三佳估计值。

// 最后，当最佳估计值过期时，它会被第二佳估计值替换，而第二佳估计值又会被第三佳估计值替换。最新的样本会替换第三佳估计值。

namespace webrtc {
namespace bbr {

// 比较两个值，如果第一个值小于或等于第二个值，则返回 true。
template <class T>
struct MinFilter {
  bool operator()(const T& lhs, const T& rhs) const { return lhs <= rhs; }
};

// 比较两个值，如果第一个值大于或等于第二个值，则返回 true。
template <class T>
struct MaxFilter {
  bool operator()(const T& lhs, const T& rhs) const { return lhs >= rhs; }
};

// 使用以下方法构造类型为 T 的窗口化过滤器对象。
// 例如，使用 Timestamp 作为时间类型的最小过滤器：
//   WindowedFilter<T, MinFilter<T>, Timestamp, TimeDelta>
//   ObjectName;
// 使用 64 位整数作为时间类型的最大过滤器：
//   WindowedFilter<T, MaxFilter<T>, uint64_t, int64_t> ObjectName;
// 具体来说，这个模板接受四个参数：
// 1. T -- 被过滤的测量值的类型。
// 2. Compare -- 根据所需的过滤器类型，使用 MinFilter<T> 或 MaxFilter<T>。
// 3. TimeT -- 用于表示时间戳的类型。
// 4. TimeDeltaT -- 用于表示两个时间戳之间的连续时间间隔的类型。如果 |a| 和 |b| 都是 TimeT 类型，则必须是 (a - b) 的类型。
template <class T, class Compare, typename TimeT, typename TimeDeltaT>
class WindowedFilter {
 public:
  // |window_length| 是最佳估计值过期后的时间周期。
  // |zero_value| 用作 T 对象的未初始化值。
  // 重要的是，|zero_value| 应该是真实样本的无效值。
  WindowedFilter(TimeDeltaT window_length, T zero_value, TimeT zero_time)
      : window_length_(window_length),
        zero_value_(zero_value),
        estimates_{Sample(zero_value_, zero_time),
                   Sample(zero_value_, zero_time),
                   Sample(zero_value_, zero_time)} {}

  // 更改窗口长度。不会更新任何当前样本。
  void SetWindowLength(TimeDeltaT window_length) {
    window_length_ = window_length;
  }

  // 使用 |sample| 更新最佳估计值，并在必要时过期并更新最佳估计值。
  void Update(T new_sample, TimeT new_time) {
    // 如果所有估计值尚未初始化，或者新样本是新的最佳值，或者最新记录的估计值太旧，则重置所有估计值。
    if (estimates_[0].sample == zero_value_ ||
        Compare()(new_sample, estimates_[0].sample) ||
        new_time - estimates_[2].time > window_length_) {
      Reset(new_sample, new_time);
      return;
    }

    if (Compare()(new_sample, estimates_[1].sample)) {
      estimates_[1] = Sample(new_sample, new_time);
      estimates_[2] = estimates_[1];
    } else if (Compare()(new_sample, estimates_[2].sample)) {
      estimates_[2] = Sample(new_sample, new_time);
    }

    // 必要时过期并更新估计值。
    if (new_time - estimates_[0].time > window_length_) {
      // 最佳估计值在整个窗口中都没有更新，因此提升第二佳和第三佳估计值。
      estimates_[0] = estimates_[1];
      estimates_[1] = estimates_[2];
      estimates_[2] = Sample(new_sample, new_time);
      // 需要再次迭代。检查新的最佳估计值是否也在窗口外，因为它可能也是很久以前记录的。
      // 不需要再次迭代，因为我们在方法开始时就覆盖了这种情况。
      if (new_time - estimates_[0].time > window_length_) {
        estimates_[0] = estimates_[1];
        estimates_[1] = estimates_[2];
      }
      return;
    }
    if (estimates_[1].sample == estimates_[0].sample &&
        new_time - estimates_[1].time > window_length_ >> 2) {
      // 窗口的四分之一已经过去，没有更好的样本，因此第二佳估计值取自窗口的第二四分之一。
      estimates_[2] = estimates_[1] = Sample(new_sample, new_time);
      return;
    }

    if (estimates_[2].sample == estimates_[1].sample &&
        new_time - estimates_[2].time > window_length_ >> 1) {
      // 窗口的一半已经过去，没有更好的估计值，因此第三佳估计值取自窗口的后半部分。
      estimates_[2] = Sample(new_sample, new_time);
    }
  }

  // 将所有估计值重置为新样本。
  void Reset(T new_sample, TimeT new_time) {
    estimates_[0] = estimates_[1] = estimates_[2] =
        Sample(new_sample, new_time);
  }

  T GetBest() const { return estimates_[0].sample; }
  T GetSecondBest() const { return estimates_[1].sample; }
  T GetThirdBest() const { return estimates_[2].sample; }

 private:
  struct Sample {
    T sample;
    TimeT time;
    Sample(T init_sample, TimeT init_time)
        : sample(init_sample), time(init_time) {}
  };

  TimeDeltaT window_length_;  // 窗口的时间长度。
  T zero_value_;              // T 的未初始化值。
  Sample estimates_[3];       // 最佳估计值是元素 0。
};

}  // namespace bbr
}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_BBR_WINDOWED_FILTER_H_
