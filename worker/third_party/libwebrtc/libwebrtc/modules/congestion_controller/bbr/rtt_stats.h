/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// A convenience class to store RTT samples and calculate smoothed RTT.
// From the Quic BBR implementation in Chromium.

#ifndef MODULES_CONGESTION_CONTROLLER_BBR_RTT_STATS_H_
#define MODULES_CONGESTION_CONTROLLER_BBR_RTT_STATS_H_

#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace bbr {

// RttStats类用于跟踪和更新往返时间（RTT）的统计信息。
class RttStats {
 public:
  // 构造函数
  RttStats();

  // 根据接收到的确认（ack）更新RTT信息。确认是在数据包发送后|send_delta|时间收到的，
  // 对端报告确认被延迟了|ack_delay|。
  void UpdateRtt(TimeDelta send_delta, TimeDelta ack_delay, Timestamp now);

  // 如果最新的RTT大于平滑后的RTT(smoothed_rtt)，则将平滑后的RTT增加到最新的RTT。
  // 如果最新的偏差大于平均偏差(mean deviation)，则将平均偏差增加到最新的偏差。
  void ExpireSmoothedMetrics();

  // 当连接迁移时调用，需要重置RTT测量。
  void OnConnectionMigration();

  // 返回连接的平滑后的RTT（指数加权移动平均RTT）。
  // 如果没有有效的更新发生，可能返回Zero。
  TimeDelta smoothed_rtt() const { return smoothed_rtt_; }

  // 返回最近一次RTT样本之前的平滑后的RTT。
  TimeDelta previous_srtt() const { return previous_srtt_; }

  // 返回初始RTT的微秒值。
  int64_t initial_rtt_us() const { return initial_rtt_us_; }

  // 设置用于SmoothedRtt的初始RTT，在任何RTT更新之前使用。
  void set_initial_rtt_us(int64_t initial_rtt_us) {
    RTC_DCHECK_GE(initial_rtt_us, 0);
    if (initial_rtt_us <= 0) {
      RTC_LOG(LS_ERROR) << "Attempt to set initial rtt to <= 0.";
      return;
    }
    initial_rtt_us_ = initial_rtt_us;
  }

  // 返回最近的RTT测量值。
  // 如果没有有效的更新发生，可能返回Zero。
  TimeDelta latest_rtt() const { return latest_rtt_; }

  // 返回整个连接的最小RTT。
  // 如果没有有效的更新发生，可能返回Zero。
  TimeDelta min_rtt() const { return min_rtt_; }

  // 返回平均偏差值。
  TimeDelta mean_deviation() const { return mean_deviation_; }

 private:
  TimeDelta latest_rtt_; // 最近的RTT测量值。
  TimeDelta min_rtt_; // 整个连接的最小RTT。
  TimeDelta smoothed_rtt_; // 平滑后的RTT。
  TimeDelta previous_srtt_; // 最近一次RTT样本之前的平滑后的RTT。
  TimeDelta mean_deviation_; // 本次会话期间的平均RTT偏差。
  int64_t initial_rtt_us_; // 初始RTT的微秒值。

};

}  // namespace bbr
}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_BBR_RTT_STATS_H_
