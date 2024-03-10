/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#define MS_CLASS "webrtc::BitrateEstimator"
// #define MS_LOG_DEV_LEVEL 3

#include "modules/congestion_controller/goog_cc/bitrate_estimator.h"

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "api/units/data_rate.h"

namespace webrtc {

namespace {
constexpr int kInitialRateWindowMs = 500;
constexpr int kRateWindowMs = 150;
constexpr int kMinRateWindowMs = 150;
constexpr int kMaxRateWindowMs = 1000;

const char kBweThroughputWindowConfig[] = "WebRTC-BweThroughputWindowConfig";

}  // namespace

BitrateEstimator::BitrateEstimator(const WebRtcKeyValueConfig* key_value_config)
    : sum_(0),
      initial_window_ms_("initial_window_ms", kInitialRateWindowMs,
                         kMinRateWindowMs, kMaxRateWindowMs),
      noninitial_window_ms_("window_ms", kRateWindowMs, kMinRateWindowMs,
                            kMaxRateWindowMs),
      uncertainty_scale_("scale", 10.0),
      uncertainty_scale_in_alr_("scale_alr", 10.0),
      uncertainty_symmetry_cap_("symmetry_cap", DataRate::Zero()),
      estimate_floor_("floor", DataRate::Zero()),
      current_window_ms_(0),
      prev_time_ms_(-1),
      bitrate_estimate_kbps_(-1.0f),
      bitrate_estimate_var_(50.0f) {
  // 例如 WebRTC-BweThroughputWindowConfig/initial_window_ms:350,window_ms:250/
  ParseFieldTrial({&initial_window_ms_, &noninitial_window_ms_,
                   &uncertainty_scale_, &uncertainty_scale_in_alr_,
                   &uncertainty_symmetry_cap_, &estimate_floor_},
                  key_value_config->Lookup(kBweThroughputWindowConfig));
}

BitrateEstimator::~BitrateEstimator() = default;

void BitrateEstimator::Update(Timestamp at_time, DataSize amount, bool in_alr) {
  int rate_window_ms = noninitial_window_ms_;
  // 在开始时使用较大的窗口以获得更稳定的样本，该样本可用于初始化估计值。
  if (bitrate_estimate_kbps_ < 0.f) rate_window_ms = initial_window_ms_;
  float bitrate_sample_kbps =
      UpdateWindow(at_time.ms(), amount.bytes(), rate_window_ms);
  if (bitrate_sample_kbps < 0.0f) return;
  if (bitrate_estimate_kbps_ < 0.0f) {
    // 这是我们得到的第一个样本。使用它来初始化估计值。
    bitrate_estimate_kbps_ = bitrate_sample_kbps;
    return;
  }
  // 将样本不确定性定义为其与当前估计值相差的函数。对于较低的uncertainty_symmetry_cap_值，我们对增加添加更多的不确定性，而对于减少则较少。对于较高的值，我们接近对称性。
  float scale = uncertainty_scale_;
  if (in_alr && bitrate_sample_kbps < bitrate_estimate_kbps_) {
    // 可选择在ALR期间获得的样本使用更高的不确定性。
    scale = uncertainty_scale_in_alr_;
  }
  float sample_uncertainty =
      scale * std::abs(bitrate_estimate_kbps_ - bitrate_sample_kbps) /
      (bitrate_estimate_kbps_ +
       std::min(bitrate_sample_kbps,
                uncertainty_symmetry_cap_.Get().kbps<float>()));

  float sample_var = sample_uncertainty * sample_uncertainty;
  // 更新比特率的贝叶斯估计，如果样本不确定性较大，则赋予较低的权重。
  // 每次更新都会增加比特率估计的不确定性，以模拟比特率随时间变化。
  float pred_bitrate_estimate_var = bitrate_estimate_var_ + 5.f;
  bitrate_estimate_kbps_ = (sample_var * bitrate_estimate_kbps_ +
                            pred_bitrate_estimate_var * bitrate_sample_kbps) /
                           (sample_var + pred_bitrate_estimate_var);
  bitrate_estimate_kbps_ =
      std::max(bitrate_estimate_kbps_, estimate_floor_.Get().kbps<float>());
  bitrate_estimate_var_ = sample_var * pred_bitrate_estimate_var /
                          (sample_var + pred_bitrate_estimate_var);
}

float BitrateEstimator::UpdateWindow(int64_t now_ms, int bytes,
                                     int rate_window_ms) {
  // 如果时间倒退，则重置。
  if (now_ms < prev_time_ms_) {
    prev_time_ms_ = -1;
    sum_ = 0;
    current_window_ms_ = 0;
  }
  if (prev_time_ms_ >= 0) {
    current_window_ms_ += now_ms - prev_time_ms_;
    // 如果超过一个完整窗口时间没有收到任何数据，则重置。
    if (now_ms - prev_time_ms_ > rate_window_ms) {
      sum_ = 0;
      current_window_ms_ %= rate_window_ms;
    }
  }
  prev_time_ms_ = now_ms;
  float bitrate_sample = -1.0f;
  if (current_window_ms_ >= rate_window_ms) {
    bitrate_sample = 8.0f * sum_ / static_cast<float>(rate_window_ms);
    current_window_ms_ -= rate_window_ms;
    sum_ = 0;
  }
  sum_ += bytes;
  return bitrate_sample;
}

absl::optional<DataRate> BitrateEstimator::bitrate() const {
  if (bitrate_estimate_kbps_ < 0.f) return absl::nullopt;
  return DataRate::kbps(bitrate_estimate_kbps_);
}

absl::optional<DataRate> BitrateEstimator::PeekRate() const {
  if (current_window_ms_ > 0)
    return DataSize::bytes(sum_) / TimeDelta::ms(current_window_ms_);
  return absl::nullopt;
}

void BitrateEstimator::ExpectFastRateChange() {
  // 通过提高比特率估计的方差，我们允许比特率在接下来的几个样本中快速变化。
  bitrate_estimate_var_ += 200;
}
}  // namespace webrtc
