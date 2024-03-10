#include "modules/congestion_controller/bbr/rtt_stats.h"

#include <algorithm>
#include <string>
#include <type_traits>

#include "rtc_base/logging.h"

namespace webrtc {
namespace bbr {
namespace {

// 在收到任何样本之前使用的默认初始RTT。
const int kInitialRttMs = 100;
// 平滑RTT更新的加权因子。
const double kAlpha = 0.125;
// 1 - kAlpha的快捷计算，用于RTT更新公式。
const double kOneMinusAlpha = (1 - kAlpha);
// 均值偏差更新的加权因子。
const double kBeta = 0.25;
// 1 - kBeta的快捷计算，用于均值偏差更新公式。
const double kOneMinusBeta = (1 - kBeta);
// 毫秒到微秒的转换因子。
const int64_t kNumMicrosPerMilli = 1000;
}  // namespace

// 构造函数初始化所有RTT统计信息为0，并设置初始RTT。
RttStats::RttStats()
    : latest_rtt_(TimeDelta::Zero()),
      min_rtt_(TimeDelta::Zero()),
      smoothed_rtt_(TimeDelta::Zero()),
      previous_srtt_(TimeDelta::Zero()),
      mean_deviation_(TimeDelta::Zero()),
      initial_rtt_us_(kInitialRttMs * kNumMicrosPerMilli) {}

// 过期平滑度量，如果最新RTT大于平滑RTT或者最新偏差大于均值偏差，则更新它们。
void RttStats::ExpireSmoothedMetrics() {
  mean_deviation_ =
      std::max(mean_deviation_, (smoothed_rtt_ - latest_rtt_).Abs());
  smoothed_rtt_ = std::max(smoothed_rtt_, latest_rtt_);
}

// 根据新的样本更新RTT。
void RttStats::UpdateRtt(TimeDelta send_delta,
                         TimeDelta ack_delay,
                         Timestamp now) {
  // 忽略无效的send_delta值。
  if (send_delta.IsInfinite() || send_delta <= TimeDelta::Zero()) {
    RTC_LOG(LS_WARNING) << "Ignoring measured send_delta, because it's is "
                           "either infinite, zero, or negative.  send_delta = "
                        << ToString(send_delta);
    return;
  }

  // 首先更新min_rtt_，不考虑ack_delay的修正，因为客户端的时钟粒度可能导致高ack_delay从而低估min_rtt_。
  if (min_rtt_.IsZero() || min_rtt_ > send_delta) {
    min_rtt_ = send_delta;
  }

  // 如果有正的RTT样本，则修正ack_delay。否则，使用send_delta作为合理的平滑RTT度量。
  TimeDelta rtt_sample = send_delta;
  previous_srtt_ = smoothed_rtt_;

  if (rtt_sample > ack_delay) {
    rtt_sample = rtt_sample - ack_delay;
  }
  latest_rtt_ = rtt_sample;
  // 第一次调用时的特殊处理。
  if (smoothed_rtt_.IsZero()) {
    smoothed_rtt_ = rtt_sample;
    mean_deviation_ = rtt_sample / 2;
  } else {
    // 更新平滑RTT和均值偏差。
    mean_deviation_ = kOneMinusBeta * mean_deviation_ +
                      kBeta * (smoothed_rtt_ - rtt_sample).Abs();
    smoothed_rtt_ = kOneMinusAlpha * smoothed_rtt_ + kAlpha * rtt_sample;
    RTC_LOG(LS_VERBOSE) << " smoothed_rtt(us):" << smoothed_rtt_.us()
                        << " mean_deviation(us):" << mean_deviation_.us();
  }
}

// 当连接迁移时调用，重置所有RTT统计信息。
void RttStats::OnConnectionMigration() {
  latest_rtt_ = TimeDelta::Zero();
  min_rtt_ = TimeDelta::Zero();
  smoothed_rtt_ = TimeDelta::Zero();
  mean_deviation_ = TimeDelta::Zero();
  initial_rtt_us_ = kInitialRttMs * kNumMicrosPerMilli;
}

}  // namespace bbr
}  // namespace webrtc
